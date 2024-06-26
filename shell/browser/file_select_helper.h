// Copyright (c) 2021 Microsoft. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_FILE_SELECT_HELPER_H_
#define ELECTRON_SHELL_BROWSER_FILE_SELECT_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/base/directory_lister.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"
#include "ui/shell_dialogs/select_file_dialog.h"

namespace content {
class FileSelectListener;
class WebContents;
}  // namespace content

namespace ui {
struct SelectedFileInfo;
}

// This class handles file-selection requests coming from renderer processes.
// It implements both the initialisation and listener functions for
// file-selection dialogs.
//
// Since FileSelectHelper listens to observations of a widget, it needs to live
// on and be destroyed on the UI thread. References to FileSelectHelper may be
// passed on to other threads.
class FileSelectHelper : public base::RefCountedThreadSafe<
                             FileSelectHelper,
                             content::BrowserThread::DeleteOnUIThread>,
                         public ui::SelectFileDialog::Listener,
                         private content::WebContentsObserver,
                         private net::DirectoryLister::DirectoryListerDelegate {
 public:
  // disable copy
  FileSelectHelper(const FileSelectHelper&) = delete;
  FileSelectHelper& operator=(const FileSelectHelper&) = delete;

  // Show the file chooser dialog.
  static void RunFileChooser(
      content::RenderFrameHost* render_frame_host,
      scoped_refptr<content::FileSelectListener> listener,
      const blink::mojom::FileChooserParams& params);

  // Enumerates all the files in directory.
  static void EnumerateDirectory(
      content::WebContents* tab,
      scoped_refptr<content::FileSelectListener> listener,
      const base::FilePath& path);

 private:
  friend class base::RefCountedThreadSafe<FileSelectHelper>;
  friend class base::DeleteHelper<FileSelectHelper>;
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;

  FileSelectHelper();
  ~FileSelectHelper() override;

  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      blink::mojom::FileChooserParamsPtr params);
  void GetFileTypesInThreadPool(blink::mojom::FileChooserParamsPtr params);
  void GetSanitizedFilenameOnUIThread(
      blink::mojom::FileChooserParamsPtr params);

  void RunFileChooserOnUIThread(const base::FilePath& default_path,
                                blink::mojom::FileChooserParamsPtr params);

  // Cleans up and releases this instance. This must be called after the last
  // callback is received from the file chooser dialog.
  void RunFileChooserEnd();

  // SelectFileDialog::Listener overrides.
  void FileSelected(const ui::SelectedFileInfo& file,
                    int index,
                    void* params) override;
  void MultiFilesSelected(const std::vector<ui::SelectedFileInfo>& files,
                          void* params) override;
  void FileSelectionCanceled(void* params) override;

  // content::WebContentsObserver overrides.
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void WebContentsDestroyed() override;

  void EnumerateDirectoryImpl(
      content::WebContents* tab,
      scoped_refptr<content::FileSelectListener> listener,
      const base::FilePath& path);

  // Kicks off a new directory enumeration.
  void StartNewEnumeration(const base::FilePath& path);

  // net::DirectoryLister::DirectoryListerDelegate overrides.
  void OnListFile(
      const net::DirectoryLister::DirectoryListerData& data) override;
  void OnListDone(int error) override;

  void LaunchConfirmationDialog(
      const base::FilePath& path,
      std::vector<ui::SelectedFileInfo> selected_files);

  // Cleans up and releases this instance. This must be called after the last
  // callback is received from the enumeration code.
  void EnumerateDirectoryEnd();

#if BUILDFLAG(IS_MAC)
  // Must be called from a MayBlock() task. Each selected file that is a package
  // will be zipped, and the zip will be passed to the render view host in place
  // of the package.
  void ProcessSelectedFilesMac(const std::vector<ui::SelectedFileInfo>& files);

  // Saves the paths of |zipped_files| for later deletion. Passes |files| to the
  // render view host.
  void ProcessSelectedFilesMacOnUIThread(
      const std::vector<ui::SelectedFileInfo>& files,
      const std::vector<base::FilePath>& zipped_files);

  // Zips the package at |path| into a temporary destination. Returns the
  // temporary destination, if the zip was successful. Otherwise returns an
  // empty path.
  static base::FilePath ZipPackage(const base::FilePath& path);
#endif  // BUILDFLAG(IS_MAC)

  void ConvertToFileChooserFileInfoList(
      const std::vector<ui::SelectedFileInfo>& files);

  // Checks to see if scans are required for the specified files.
  void PerformContentAnalysisIfNeeded(
      std::vector<blink::mojom::FileChooserFileInfoPtr> list);

  // Finish the PerformContentAnalysisIfNeeded() handling after the
  // deep scanning checks have been performed.  Deep scanning may change the
  // list of files chosen by the user, so the list of files passed here may be
  // a subset of of the files passed to PerformContentAnalysisIfNeeded().
  void NotifyListenerAndEnd(
      std::vector<blink::mojom::FileChooserFileInfoPtr> list);

  // Schedules the deletion of the files in |temporary_files_| and clears the
  // vector.
  void DeleteTemporaryFiles();

  // Cleans up when the initiator of the file chooser is no longer valid.
  void CleanUp();

  // Calls RunFileChooserEnd() if the webcontents was destroyed. Returns true
  // if the file chooser operation shouldn't proceed.
  bool AbortIfWebContentsDestroyed();

  void SetFileSelectListenerForTesting(
      scoped_refptr<content::FileSelectListener> listener);

  // Helper method to get allowed extensions for select file dialog from
  // the specified accept types as defined in the spec:
  //   http://whatwg.org/html/number-state.html#attr-input-accept
  // |accept_types| contains only valid lowercased MIME types or file extensions
  // beginning with a period (.).
  static std::unique_ptr<ui::SelectFileDialog::FileTypeInfo>
  GetFileTypesFromAcceptType(const std::vector<std::u16string>& accept_types);

  // Check the accept type is valid. It is expected to be all lower case with
  // no whitespace.
  static bool IsAcceptTypeValid(const std::string& accept_type);

  // Get a sanitized filename suitable for use as a default filename.
  static base::FilePath GetSanitizedFileName(
      const base::FilePath& suggested_path);

  // The RenderFrameHost and WebContents for the page showing a file dialog
  // (may only be one such dialog).
  raw_ptr<content::RenderFrameHost> render_frame_host_;
  raw_ptr<content::WebContents> web_contents_;

  // |listener_| receives the result of the FileSelectHelper.
  scoped_refptr<content::FileSelectListener> listener_;

  // Dialog box used for choosing files to upload from file form fields.
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  std::unique_ptr<ui::SelectFileDialog::FileTypeInfo> select_file_types_;

  // The type of file dialog last shown. This is SELECT_NONE if an
  // instance is created through the public EnumerateDirectory().
  ui::SelectFileDialog::Type dialog_type_;

  // The mode of file dialog last shown.
  blink::mojom::FileChooserParams::Mode dialog_mode_;

  // The enumeration root directory for EnumerateDirectory() and
  // RunFileChooser with kUploadFolder.
  base::FilePath base_dir_;

  // Maintain an active directory enumeration.  These could come from the file
  // select dialog or from drag-and-drop of directories.  There could not be
  // more than one going on at a time.
  struct ActiveDirectoryEnumeration;
  std::unique_ptr<ActiveDirectoryEnumeration> directory_enumeration_;

  // Temporary files only used on OSX. This class is responsible for deleting
  // these files when they are no longer needed.
  std::vector<base::FilePath> temporary_files_;
};

#endif  // ELECTRON_SHELL_BROWSER_FILE_SELECT_HELPER_H_
