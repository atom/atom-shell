// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/browser.h"

#include <fcntl.h>
#include <stdlib.h>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/process/launch.h"
#include "electron/electron_version.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/native_window.h"
#include "shell/browser/window_list.h"
#include "shell/common/application_info.h"
#include "shell/common/gin_converters/login_item_settings_converter.h"
#include "shell/common/thread_restrictions.h"

#if BUILDFLAG(IS_LINUX)
#include "shell/browser/linux/unity_service.h"
#include "ui/gtk/gtk_util.h"  // nogncheck
#endif

namespace electron {

namespace {

const char kXdgSettings[] = "xdg-settings";
const char kXdgSettingsDefaultSchemeHandler[] = "default-url-scheme-handler";

// The use of the ForTesting flavors is a hack workaround to avoid having to
// patch these as friends into the associated guard classes.
class [[maybe_unused, nodiscard]] LaunchXdgUtilityScopedAllowBaseSyncPrimitives
    : public base::ScopedAllowBaseSyncPrimitivesForTesting {};

bool LaunchXdgUtility(const std::vector<std::string>& argv, int* exit_code) {
  *exit_code = EXIT_FAILURE;
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0)
    return false;

  base::LaunchOptions options;
  options.fds_to_remap.emplace_back(devnull, STDIN_FILENO);

  base::Process process = base::LaunchProcess(argv, options);
  close(devnull);

  if (!process.IsValid())
    return false;
  LaunchXdgUtilityScopedAllowBaseSyncPrimitives allow_base_sync_primitives;
  return process.WaitForExit(exit_code);
}

std::optional<std::string> GetXdgAppOutput(
    const std::vector<std::string>& argv) {
  std::string reply;
  int success_code;
  ScopedAllowBlockingForElectron allow_blocking;
  bool ran_ok = base::GetAppOutputWithExitCode(base::CommandLine(argv), &reply,
                                               &success_code);

  if (!ran_ok || success_code != EXIT_SUCCESS)
    return std::optional<std::string>();

  return std::make_optional(reply);
}

bool SetDefaultWebClient(const std::string& protocol) {
  auto env = base::Environment::Create();

  std::vector<std::string> argv = {kXdgSettings, "set"};
  if (!protocol.empty()) {
    argv.emplace_back(kXdgSettingsDefaultSchemeHandler);
    argv.emplace_back(protocol);
  }
  std::string desktop_name;
  if (!env->GetVar("CHROME_DESKTOP", &desktop_name)) {
    return false;
  }
  argv.emplace_back(desktop_name);

  int exit_code;
  bool ran_ok = LaunchXdgUtility(argv, &exit_code);
  return ran_ok && exit_code == EXIT_SUCCESS;
}

}  // namespace

void Browser::AddRecentDocument(const base::FilePath& path) {}

void Browser::ClearRecentDocuments() {}

bool Browser::SetAsDefaultProtocolClient(const std::string& protocol,
                                         gin::Arguments* args) {
  return SetDefaultWebClient(protocol);
}

bool Browser::IsDefaultProtocolClient(const std::string& protocol,
                                      gin::Arguments* args) {
  auto env = base::Environment::Create();

  if (protocol.empty())
    return false;

  std::string desktop_name;
  if (!env->GetVar("CHROME_DESKTOP", &desktop_name))
    return false;
  const std::vector<std::string> argv = {kXdgSettings, "check",
                                         kXdgSettingsDefaultSchemeHandler,
                                         protocol, desktop_name};
  // Allow any reply that starts with "yes".
  const std::optional<std::string> output = GetXdgAppOutput(argv);
  return output && output->starts_with("yes");
}

// Todo implement
bool Browser::RemoveAsDefaultProtocolClient(const std::string& protocol,
                                            gin::Arguments* args) {
  return false;
}

std::u16string Browser::GetApplicationNameForProtocol(const GURL& url) {
  const std::vector<std::string> argv = {
      "xdg-mime", "query", "default",
      std::string("x-scheme-handler/") + url.scheme()};

  return base::ASCIIToUTF16(GetXdgAppOutput(argv).value_or(std::string()));
}

bool Browser::SetBadgeCount(std::optional<int> count) {
  if (IsUnityRunning() && count.has_value()) {
    unity::SetDownloadCount(count.value());
    badge_count_ = count.value();
    return true;
  } else {
    return false;
  }
}

void Browser::SetLoginItemSettings(LoginItemSettings settings) {}

v8::Local<v8::Value> Browser::GetLoginItemSettings(
    const LoginItemSettings& options) {
  LoginItemSettings settings;
  return gin::ConvertToV8(JavascriptEnvironment::GetIsolate(), settings);
}

std::string Browser::GetExecutableFileVersion() const {
  return GetApplicationVersion();
}

std::string Browser::GetExecutableFileProductName() const {
  return GetApplicationName();
}

bool Browser::IsUnityRunning() {
  return unity::IsRunning();
}

bool Browser::IsEmojiPanelSupported() {
  return false;
}

void Browser::ShowAboutPanel() {
  const auto& opts = about_panel_options_;

  GtkWidget* dialogWidget = gtk_about_dialog_new();
  GtkAboutDialog* dialog = GTK_ABOUT_DIALOG(dialogWidget);

  const std::string* str;
  const base::Value::List* list;

  if ((str = opts.FindString("applicationName"))) {
    gtk_about_dialog_set_program_name(dialog, str->c_str());
  }
  if ((str = opts.FindString("applicationVersion"))) {
    gtk_about_dialog_set_version(dialog, str->c_str());
  }
  if ((str = opts.FindString("copyright"))) {
    gtk_about_dialog_set_copyright(dialog, str->c_str());
  }
  if ((str = opts.FindString("website"))) {
    gtk_about_dialog_set_website(dialog, str->c_str());
  }
  if ((str = opts.FindString("iconPath"))) {
    GError* error = nullptr;
    constexpr int width = 64;   // width of about panel icon in pixels
    constexpr int height = 64;  // height of about panel icon in pixels

    // set preserve_aspect_ratio to true
    GdkPixbuf* icon =
        gdk_pixbuf_new_from_file_at_size(str->c_str(), width, height, &error);
    if (error != nullptr) {
      g_warning("%s", error->message);
      g_clear_error(&error);
    } else {
      gtk_about_dialog_set_logo(dialog, icon);
      g_clear_object(&icon);
    }
  }

  if ((list = opts.FindList("authors"))) {
    std::vector<const char*> cstrs;
    for (const auto& authorVal : *list) {
      if (authorVal.is_string()) {
        cstrs.push_back(authorVal.GetString().c_str());
      }
    }
    if (cstrs.empty()) {
      LOG(WARNING) << "No author strings found in 'authors' array";
    } else {
      cstrs.push_back(nullptr);  // null-terminated char* array
      gtk_about_dialog_set_authors(dialog, cstrs.data());
    }
  }

  // destroy the widget when it closes
  g_signal_connect_swapped(dialogWidget, "response",
                           G_CALLBACK(gtk_widget_destroy), dialogWidget);

  gtk_widget_show_all(dialogWidget);
}

void Browser::SetAboutPanelOptions(base::Value::Dict options) {
  about_panel_options_ = std::move(options);
}

}  // namespace electron
