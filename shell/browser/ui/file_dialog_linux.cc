// Copyright (c) 2024 Microsoft, GmbH.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/file_dialog.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "ui/gtk/select_file_dialog_linux_gtk.h"  // nogncheck
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace file_dialog {

DialogSettings::DialogSettings() = default;
DialogSettings::DialogSettings(const DialogSettings&) = default;
DialogSettings::~DialogSettings() = default;

namespace {

ui::SelectFileDialog::Type GetDialogType(int properties) {
  if (properties & OPEN_DIALOG_OPEN_DIRECTORY)
    return ui::SelectFileDialog::SELECT_FOLDER;

  if (properties & OPEN_DIALOG_MULTI_SELECTIONS)
    return ui::SelectFileDialog::SELECT_OPEN_MULTI_FILE;

  return ui::SelectFileDialog::SELECT_OPEN_FILE;
}

ui::SelectFileDialog::FileTypeInfo GetFilterInfo(const Filters& filters) {
  ui::SelectFileDialog::FileTypeInfo file_type_info;

  for (const auto& [name, extension_group] : filters) {
    file_type_info.extension_description_overrides.push_back(
        base::UTF8ToUTF16(name));

    const bool has_all_files_wildcard = base::ranges::any_of(
        extension_group, [](const auto& ext) { return ext == "*"; });
    if (has_all_files_wildcard) {
      file_type_info.include_all_files = true;
    } else {
      file_type_info.extensions.emplace_back(extension_group);
    }
  }

  return file_type_info;
}

class FileChooserDialog : public ui::SelectFileDialog::Listener {
 public:
  enum class DialogType { OPEN, SAVE };

  FileChooserDialog() { dialog_ = ui::SelectFileDialog::Create(this, nullptr); }

  ~FileChooserDialog() override = default;

  ui::SelectFileDialogLinux::ExtraSettings GetExtraSettings(
      const DialogSettings& settings) {
    ui::SelectFileDialogLinux::ExtraSettings extra;
    extra.button_label = settings.button_label;
    extra.show_overwrite_confirmation =
        settings.properties & SAVE_DIALOG_SHOW_OVERWRITE_CONFIRMATION;
    extra.allow_multiple_selection =
        settings.properties & OPEN_DIALOG_MULTI_SELECTIONS;
    if (type_ == DialogType::SAVE) {
      extra.show_hidden = settings.properties & SAVE_DIALOG_SHOW_HIDDEN_FILES;
    } else {
      extra.show_hidden = settings.properties & OPEN_DIALOG_SHOW_HIDDEN_FILES;
    }

    return extra;
  }

  void RunSaveDialogImpl(const DialogSettings& settings) {
    type_ = DialogType::SAVE;
    ui::SelectFileDialog::FileTypeInfo file_info =
        GetFilterInfo(settings.filters);
    auto extra_settings = GetExtraSettings(settings);
    dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_SAVEAS_FILE,
        base::UTF8ToUTF16(settings.title), settings.default_path,
        &file_info /* file_types */, 0 /* file_type_index */,
        base::FilePath::StringType() /* default_extension */,
        settings.parent_window ? settings.parent_window->GetNativeWindow()
                               : nullptr,
        static_cast<void*>(&extra_settings));
  }

  void RunSaveDialog(gin_helper::Promise<gin_helper::Dictionary> promise,
                     const DialogSettings& settings) {
    promise_ = std::move(promise);
    RunSaveDialogImpl(settings);
  }

  void RunSaveDialog(base::OnceCallback<void(gin_helper::Dictionary)> callback,
                     const DialogSettings& settings) {
    callback_ = std::move(callback);
    RunSaveDialogImpl(settings);
  }

  void RunOpenDialogImpl(const DialogSettings& settings) {
    type_ = DialogType::OPEN;
    ui::SelectFileDialog::FileTypeInfo file_info =
        GetFilterInfo(settings.filters);
    auto extra_settings = GetExtraSettings(settings);
    dialog_->SelectFile(
        GetDialogType(settings.properties), base::UTF8ToUTF16(settings.title),
        settings.default_path, &file_info, 0 /* file_type_index */,
        base::FilePath::StringType() /* default_extension */,
        settings.parent_window ? settings.parent_window->GetNativeWindow()
                               : nullptr,
        static_cast<void*>(&extra_settings));
  }

  void RunOpenDialog(gin_helper::Promise<gin_helper::Dictionary> promise,
                     const DialogSettings& settings) {
    promise_ = std::move(promise);
    RunOpenDialogImpl(settings);
  }

  void RunOpenDialog(base::OnceCallback<void(gin_helper::Dictionary)> callback,
                     const DialogSettings& settings) {
    callback_ = std::move(callback);
    RunOpenDialogImpl(settings);
  }

  // ui::SelectFileDialog::Listener
  void FileSelected(const ui::SelectedFileInfo& file,
                    int index,
                    void* params) override {
    v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
    dict.Set("canceled", false);
    if (type_ == DialogType::SAVE) {
      dict.Set("filePath", file.file_path);
    } else {
      dict.Set("filePaths", std::vector<base::FilePath>{file.file_path});
    }

    if (callback_) {
      std::move(callback_).Run(dict);
    } else {
      promise_.Resolve(dict);
    }

    delete this;
  }

  void MultiFilesSelected(const std::vector<ui::SelectedFileInfo>& files,
                          void* params) override {
    v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
    dict.Set("canceled", false);
    dict.Set("filePaths", ui::SelectedFileInfoListToFilePathList(files));

    if (callback_) {
      std::move(callback_).Run(dict);
    } else {
      promise_.Resolve(dict);
    }

    delete this;
  }

  void FileSelectionCanceled(void* params) override {
    v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
    dict.Set("canceled", true);
    if (type_ == DialogType::SAVE) {
      dict.Set("filePath", base::FilePath());
    } else {
      dict.Set("filePaths", std::vector<base::FilePath>());
    }

    if (callback_) {
      std::move(callback_).Run(dict);
    } else {
      promise_.Resolve(dict);
    }

    delete this;
  }

 private:
  DialogType type_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  base::OnceCallback<void(gin_helper::Dictionary)> callback_;
  gin_helper::Promise<gin_helper::Dictionary> promise_;
};

}  // namespace

bool ShowOpenDialogSync(const DialogSettings& settings,
                        std::vector<base::FilePath>* paths) {
  v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
  gin_helper::Promise<gin_helper::Dictionary> promise(isolate);

  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  auto cb = base::BindOnce(
      [](base::RepeatingClosure cb, std::vector<base::FilePath>* file_paths,
         gin_helper::Dictionary result) {
        result.Get("filePaths", file_paths);
        std::move(cb).Run();
      },
      run_loop.QuitClosure(), paths);

  FileChooserDialog* dialog = new FileChooserDialog();
  dialog->RunOpenDialog(std::move(cb), settings);

  run_loop.Run();
  return !paths->empty();
}

void ShowOpenDialog(const DialogSettings& settings,
                    gin_helper::Promise<gin_helper::Dictionary> promise) {
  FileChooserDialog* dialog = new FileChooserDialog();
  dialog->RunOpenDialog(std::move(promise), settings);
}

bool ShowSaveDialogSync(const DialogSettings& settings, base::FilePath* path) {
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
  gin_helper::Promise<gin_helper::Dictionary> promise(isolate);
  auto cb = base::BindOnce(
      [](base::RepeatingClosure cb, base::FilePath* file_path,
         gin_helper::Dictionary result) {
        result.Get("filePath", file_path);
        std::move(cb).Run();
      },
      run_loop.QuitClosure(), path);

  FileChooserDialog* dialog = new FileChooserDialog();
  dialog->RunSaveDialog(std::move(promise), settings);
  run_loop.Run();
  return !path->empty();
}

void ShowSaveDialog(const DialogSettings& settings,
                    gin_helper::Promise<gin_helper::Dictionary> promise) {
  FileChooserDialog* dialog = new FileChooserDialog();
  dialog->RunSaveDialog(std::move(promise), settings);
}

}  // namespace file_dialog
