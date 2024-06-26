// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/message_box.h"

#include "base/containers/contains.h"
#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "electron/electron_gtk_stubs.h"
#include "shell/browser/browser.h"
#include "shell/browser/native_window_observer.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/gtk_util.h"
#include "ui/base/glib/scoped_gsignal.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gtk/gtk_ui.h"    // nogncheck
#include "ui/gtk/gtk_util.h"  // nogncheck

#if defined(USE_OZONE)
#include "ui/base/ui_base_features.h"
#endif

#define ANSI_FOREGROUND_RED "\x1b[31m"
#define ANSI_FOREGROUND_BLACK "\x1b[30m"
#define ANSI_TEXT_BOLD "\x1b[1m"
#define ANSI_BACKGROUND_GRAY "\x1b[47m"
#define ANSI_RESET "\x1b[0m"

namespace electron {

MessageBoxSettings::MessageBoxSettings() = default;
MessageBoxSettings::MessageBoxSettings(const MessageBoxSettings&) = default;
MessageBoxSettings::~MessageBoxSettings() = default;

namespace {

// <ID, messageBox> map
base::flat_map<int, GtkWidget*>& GetDialogsMap() {
  static base::NoDestructor<base::flat_map<int, GtkWidget*>> dialogs;
  return *dialogs;
}

class GtkMessageBox : private NativeWindowObserver {
 public:
  explicit GtkMessageBox(const MessageBoxSettings& settings)
      : id_(settings.id),
        cancel_id_(settings.cancel_id),
        parent_(static_cast<NativeWindow*>(settings.parent_window)) {
    // Create dialog.
    dialog_ =
        gtk_message_dialog_new(nullptr,                         // parent
                               static_cast<GtkDialogFlags>(0),  // no flags
                               GetMessageType(settings.type),   // type
                               GTK_BUTTONS_NONE,                // no buttons
                               "%s", settings.message.c_str());
    if (id_)
      GetDialogsMap()[*id_] = dialog_;
    if (!settings.detail.empty())
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog_),
                                               "%s", settings.detail.c_str());
    if (!settings.title.empty())
      gtk_window_set_title(GTK_WINDOW(dialog_), settings.title.c_str());

    if (!settings.icon.isNull()) {
      // No easy way to obtain this programmatically, but GTK+'s docs
      // define GTK_ICON_SIZE_DIALOG to be 48 pixels
      static constexpr int pixel_width = 48;
      static constexpr int pixel_height = 48;
      GdkPixbuf* pixbuf =
          gtk_util::GdkPixbufFromSkBitmap(*settings.icon.bitmap());
      GdkPixbuf* scaled_pixbuf = gdk_pixbuf_scale_simple(
          pixbuf, pixel_width, pixel_height, GDK_INTERP_BILINEAR);
      GtkWidget* w = gtk_image_new_from_pixbuf(scaled_pixbuf);
      gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(dialog_), w);
      gtk_widget_show(w);
      g_clear_pointer(&scaled_pixbuf, g_object_unref);
      g_clear_pointer(&pixbuf, g_object_unref);
    }

    if (!settings.checkbox_label.empty()) {
      GtkWidget* message_area =
          gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog_));
      GtkWidget* check_button =
          gtk_check_button_new_with_label(settings.checkbox_label.c_str());
      signals_.emplace_back(
          check_button, "toggled",
          base::BindRepeating(&GtkMessageBox::OnCheckboxToggled,
                              base::Unretained(this)));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button),
                                   settings.checkbox_checked);
      gtk_container_add(GTK_CONTAINER(message_area), check_button);
      gtk_widget_show(check_button);
    }

    // Add buttons.
    GtkDialog* dialog = GTK_DIALOG(dialog_);
    if (settings.buttons.size() == 0) {
      gtk_dialog_add_button(dialog, TranslateToStock(0, "OK"), 0);
    } else {
      for (size_t i = 0; i < settings.buttons.size(); ++i) {
        gtk_dialog_add_button(dialog, TranslateToStock(i, settings.buttons[i]),
                              i);
      }
    }
    gtk_dialog_set_default_response(dialog, settings.default_id);

    // Parent window.
    if (parent_) {
      parent_->AddObserver(this);
      static_cast<NativeWindowViews*>(parent_)->SetEnabled(false);
      gtk::SetGtkTransientForAura(dialog_, parent_->GetNativeWindow());
      gtk_window_set_modal(GTK_WINDOW(dialog_), TRUE);
    }
  }

  ~GtkMessageBox() override {
    gtk_widget_destroy(dialog_);
    if (parent_) {
      parent_->RemoveObserver(this);
      static_cast<NativeWindowViews*>(parent_)->SetEnabled(true);
    }
  }

  // disable copy
  GtkMessageBox(const GtkMessageBox&) = delete;
  GtkMessageBox& operator=(const GtkMessageBox&) = delete;

  GtkMessageType GetMessageType(MessageBoxType type) {
    switch (type) {
      case MessageBoxType::kInformation:
        return GTK_MESSAGE_INFO;
      case MessageBoxType::kWarning:
        return GTK_MESSAGE_WARNING;
      case MessageBoxType::kQuestion:
        return GTK_MESSAGE_QUESTION;
      case MessageBoxType::kError:
        return GTK_MESSAGE_ERROR;
      default:
        return GTK_MESSAGE_OTHER;
    }
  }

  const char* TranslateToStock(int id, const std::string& text) {
    const std::string lower = base::ToLowerASCII(text);
    if (lower == "cancel")
      return gtk_util::GetCancelLabel();
    if (lower == "no")
      return gtk_util::GetNoLabel();
    if (lower == "ok")
      return gtk_util::GetOkLabel();
    if (lower == "yes")
      return gtk_util::GetYesLabel();
    return text.c_str();
  }

  void Show() {
    gtk_widget_show(dialog_);
    gtk::GtkUi::GetPlatform()->ShowGtkWindow(GTK_WINDOW(dialog_));
  }

  int RunSynchronous() {
    Show();
    int response = gtk_dialog_run(GTK_DIALOG(dialog_));
    return (response < 0) ? cancel_id_ : response;
  }

  void RunAsynchronous(MessageBoxCallback callback) {
    callback_ = std::move(callback);

    signals_.emplace_back(dialog_, "delete-event",
                          base::BindRepeating(gtk_widget_hide_on_delete));
    signals_.emplace_back(dialog_, "response",
                          base::BindRepeating(&GtkMessageBox::OnResponseDialog,
                                              base::Unretained(this)));
    Show();
  }

  // NativeWindowObserver
  void OnWindowClosed() override {
    parent_->RemoveObserver(this);
    parent_ = nullptr;
  }

  void OnResponseDialog(GtkWidget* widget, int response);
  void OnCheckboxToggled(GtkWidget* widget);

 private:
  // The id of the dialog.
  std::optional<int> id_;

  // The id to return when the dialog is closed without pressing buttons.
  int cancel_id_ = 0;

  bool checkbox_checked_ = false;

  raw_ptr<NativeWindow> parent_;
  RAW_PTR_EXCLUSION GtkWidget* dialog_;
  MessageBoxCallback callback_;
  std::vector<ScopedGSignal> signals_;
};

void GtkMessageBox::OnResponseDialog(GtkWidget* widget, int response) {
  if (id_)
    GetDialogsMap().erase(*id_);
  gtk_widget_hide(dialog_);

  if (response < 0)
    std::move(callback_).Run(cancel_id_, checkbox_checked_);
  else
    std::move(callback_).Run(response, checkbox_checked_);
  delete this;
}

void GtkMessageBox::OnCheckboxToggled(GtkWidget* widget) {
  checkbox_checked_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

}  // namespace

int ShowMessageBoxSync(const MessageBoxSettings& settings) {
  return GtkMessageBox(settings).RunSynchronous();
}

void ShowMessageBox(const MessageBoxSettings& settings,
                    MessageBoxCallback callback) {
  if (settings.id && base::Contains(GetDialogsMap(), *settings.id))
    CloseMessageBox(*settings.id);
  (new GtkMessageBox(settings))->RunAsynchronous(std::move(callback));
}

void CloseMessageBox(int id) {
  auto it = GetDialogsMap().find(id);
  if (it == GetDialogsMap().end()) {
    LOG(ERROR) << "CloseMessageBox called with nonexistent ID";
    return;
  }
  gtk_window_close(GTK_WINDOW(it->second));
}

void ShowErrorBox(const std::u16string& title, const std::u16string& content) {
  if (Browser::Get()->is_ready()) {
    electron::MessageBoxSettings settings;
    settings.type = electron::MessageBoxType::kError;
    settings.buttons = {};
    settings.title = "Error";
    settings.message = base::UTF16ToUTF8(title);
    settings.detail = base::UTF16ToUTF8(content);

    GtkMessageBox(settings).RunSynchronous();
  } else {
    fprintf(stderr,
            ANSI_TEXT_BOLD ANSI_BACKGROUND_GRAY ANSI_FOREGROUND_RED
            "%s\n" ANSI_FOREGROUND_BLACK "%s" ANSI_RESET "\n",
            base::UTF16ToUTF8(title).c_str(),
            base::UTF16ToUTF8(content).c_str());
  }
}

}  // namespace electron
