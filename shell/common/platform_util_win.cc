// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/platform_util.h"

#include <windows.h>  // windows.h must be included first

#include "base/win/shlwapi.h"  // NOLINT(build/include_order)

#include <atlbase.h>
#include <comdef.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/win/registry.h"
#include "base/win/scoped_co_mem.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/windows_version.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "shell/common/electron_paths.h"
#include "ui/base/win/shell.h"
#include "url/gurl.h"

namespace {

// Required COM implementation of IFileOperationProgressSink so we can
// precheck files before deletion to make sure they can be move to the
// Recycle Bin.
class DeleteFileProgressSink : public IFileOperationProgressSink {
 public:
  DeleteFileProgressSink();
  virtual ~DeleteFileProgressSink() = default;

 private:
  // IFileOperationProgressSink
  ULONG STDMETHODCALLTYPE AddRef(void) override;
  ULONG STDMETHODCALLTYPE Release(void) override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           LPVOID* ppvObj) override;
  HRESULT STDMETHODCALLTYPE StartOperations(void) override;
  HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override;
  HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override;
  HRESULT STDMETHODCALLTYPE
  PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override;
  HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD,
                                        IShellItem*,
                                        IShellItem*,
                                        LPCWSTR) override;
  HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,
                                         IShellItem*,
                                         IShellItem*,
                                         LPCWSTR,
                                         HRESULT,
                                         IShellItem*) override;
  HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD,
                                        IShellItem*,
                                        IShellItem*,
                                        LPCWSTR) override;
  HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD,
                                         IShellItem*,
                                         IShellItem*,
                                         LPCWSTR,
                                         HRESULT,
                                         IShellItem*) override;
  HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) override;
  HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD,
                                           IShellItem*,
                                           HRESULT,
                                           IShellItem*) override;
  HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override;
  HRESULT STDMETHODCALLTYPE PostNewItem(DWORD,
                                        IShellItem*,
                                        LPCWSTR,
                                        LPCWSTR,
                                        DWORD,
                                        HRESULT,
                                        IShellItem*) override;
  HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override;
  HRESULT STDMETHODCALLTYPE ResetTimer(void) override;
  HRESULT STDMETHODCALLTYPE PauseTimer(void) override;
  HRESULT STDMETHODCALLTYPE ResumeTimer(void) override;

  ULONG m_cRef;
};

DeleteFileProgressSink::DeleteFileProgressSink() {
  m_cRef = 0;
}

HRESULT DeleteFileProgressSink::PreDeleteItem(DWORD dwFlags, IShellItem*) {
  if (!(dwFlags & TSF_DELETE_RECYCLE_IF_POSSIBLE)) {
    // TSF_DELETE_RECYCLE_IF_POSSIBLE will not be set for items that cannot be
    // recycled.  In this case, we abort the delete operation.  This bubbles
    // up and stops the Delete in IFileOperation.
    return E_ABORT;
  }
  // Returns S_OK if successful, or an error value otherwise. In the case of an
  // error value, the delete operation and all subsequent operations pending
  // from the call to IFileOperation are canceled.
  return S_OK;
}

HRESULT DeleteFileProgressSink::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  // Always set out parameter to nullptr, validating it first.
  if (!ppvObj)
    return E_INVALIDARG;
  *ppvObj = nullptr;
  if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
    // Increment the reference count and return the pointer.
    *ppvObj = reinterpret_cast<IUnknown*>(this);
    AddRef();
    return NOERROR;
  }
  return E_NOINTERFACE;
}

ULONG DeleteFileProgressSink::AddRef() {
  InterlockedIncrement(&m_cRef);
  return m_cRef;
}

ULONG DeleteFileProgressSink::Release() {
  // Decrement the object's internal counter.
  ULONG ulRefCount = InterlockedDecrement(&m_cRef);
  if (0 == m_cRef) {
    delete this;
  }
  return ulRefCount;
}

HRESULT DeleteFileProgressSink::StartOperations() {
  return S_OK;
}

HRESULT DeleteFileProgressSink::FinishOperations(HRESULT) {
  return S_OK;
}

HRESULT DeleteFileProgressSink::PreRenameItem(DWORD, IShellItem*, LPCWSTR) {
  return S_OK;
}

HRESULT DeleteFileProgressSink::PostRenameItem(DWORD,
                                               IShellItem*,
                                               __RPC__in_string LPCWSTR,
                                               HRESULT,
                                               IShellItem*) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::PreMoveItem(DWORD,
                                            IShellItem*,
                                            IShellItem*,
                                            LPCWSTR) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::PostMoveItem(DWORD,
                                             IShellItem*,
                                             IShellItem*,
                                             LPCWSTR,
                                             HRESULT,
                                             IShellItem*) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::PreCopyItem(DWORD,
                                            IShellItem*,
                                            IShellItem*,
                                            LPCWSTR) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::PostCopyItem(DWORD,
                                             IShellItem*,
                                             IShellItem*,
                                             LPCWSTR,
                                             HRESULT,
                                             IShellItem*) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::PostDeleteItem(DWORD,
                                               IShellItem*,
                                               HRESULT,
                                               IShellItem*) {
  return S_OK;
}

HRESULT DeleteFileProgressSink::PreNewItem(DWORD dwFlags,
                                           IShellItem*,
                                           LPCWSTR) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::PostNewItem(DWORD,
                                            IShellItem*,
                                            LPCWSTR,
                                            LPCWSTR,
                                            DWORD,
                                            HRESULT,
                                            IShellItem*) {
  return E_NOTIMPL;
}

HRESULT DeleteFileProgressSink::UpdateProgress(UINT, UINT) {
  return S_OK;
}

HRESULT DeleteFileProgressSink::ResetTimer() {
  return S_OK;
}

HRESULT DeleteFileProgressSink::PauseTimer() {
  return S_OK;
}

HRESULT DeleteFileProgressSink::ResumeTimer() {
  return S_OK;
}

std::string OpenExternalOnWorkerThread(
    const GURL& url,
    const platform_util::OpenExternalOptions& options) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  // Quote the input scheme to be sure that the command does not have
  // parameters unexpected by the external program. This url should already
  // have been escaped.
  std::wstring escaped_url =
      L"\"" + base::UTF8ToWide(base::EscapeExternalHandlerValue(url.spec())) +
      L"\"";
  std::wstring working_dir = options.working_dir.value();

  SHELLEXECUTEINFO info = {};
  info.cbSize = sizeof(SHELLEXECUTEINFO);
  info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"open";
  info.lpFile = escaped_url.c_str();
  info.lpDirectory = working_dir.empty() ? nullptr : working_dir.c_str();
  info.nShow = SW_SHOWNORMAL;

  if (options.log_usage) {
    info.fMask |= SEE_MASK_FLAG_LOG_USAGE;
  }

  if (!ShellExecuteEx(&info)) {
    return "Failed to open: " +
           logging::SystemErrorCodeToString(logging::GetLastSystemErrorCode());
  }
  return "";
}

void ShowItemInFolderOnWorkerThread(const base::FilePath& full_path) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  base::win::ScopedCOMInitializer com_initializer;
  if (!com_initializer.Succeeded())
    return;

  base::FilePath dir = full_path.DirName().AsEndingWithSeparator();
  // ParseDisplayName will fail if the directory is "C:", it must be "C:\\".
  if (dir.empty())
    return;

  Microsoft::WRL::ComPtr<IShellFolder> desktop;
  HRESULT hr = SHGetDesktopFolder(&desktop);
  if (FAILED(hr))
    return;

  base::win::ScopedCoMem<ITEMIDLIST> dir_item;
  hr = desktop->ParseDisplayName(nullptr, nullptr,
                                 const_cast<wchar_t*>(dir.value().c_str()),
                                 nullptr, &dir_item, nullptr);
  if (FAILED(hr))
    return;

  base::win::ScopedCoMem<ITEMIDLIST> file_item;
  hr = desktop->ParseDisplayName(
      nullptr, nullptr, const_cast<wchar_t*>(full_path.value().c_str()),
      nullptr, &file_item, nullptr);
  if (FAILED(hr))
    return;

  const ITEMIDLIST* highlight[] = {file_item};
  hr = SHOpenFolderAndSelectItems(dir_item, std::size(highlight), highlight, 0);
  if (FAILED(hr)) {
    // On some systems, the above call mysteriously fails with "file not
    // found" even though the file is there.  In these cases, ShellExecute()
    // seems to work as a fallback (although it won't select the file).
    if (hr == ERROR_FILE_NOT_FOUND) {
      ShellExecute(nullptr, L"open", dir.value().c_str(), nullptr, nullptr,
                   SW_SHOW);
    } else {
      LOG(WARNING) << " " << __func__ << "(): Can't open full_path = \""
                   << full_path.value() << "\""
                   << " hr = " << logging::SystemErrorCodeToString(hr);
    }
  }
}

std::string OpenPathOnThread(const base::FilePath& full_path) {
  // May result in an interactive dialog.
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  bool success;
  if (base::DirectoryExists(full_path))
    success = ui::win::OpenFolderViaShell(full_path);
  else
    success = ui::win::OpenFileViaShell(full_path);

  return success ? "" : "Failed to open path";
}

}  // namespace

namespace platform_util {

void ShowItemInFolder(const base::FilePath& full_path) {
  base::ThreadPool::CreateCOMSTATaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING})
      ->PostTask(FROM_HERE,
                 base::BindOnce(&ShowItemInFolderOnWorkerThread,
                                full_path.NormalizePathSeparators()));
}

void OpenPath(const base::FilePath& full_path, OpenCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  base::ThreadPool::CreateCOMSTATaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING})
      ->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(&OpenPathOnThread,
                         full_path.NormalizePathSeparators()),
          std::move(callback));
}

void OpenExternal(const GURL& url,
                  const OpenExternalOptions& options,
                  OpenCallback callback) {
  base::ThreadPool::CreateCOMSTATaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING})
      ->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&OpenExternalOnWorkerThread, url, options),
          std::move(callback));
}

bool MoveItemToTrashWithError(const base::FilePath& path,
                              bool delete_on_fail,
                              std::string* error) {
  Microsoft::WRL::ComPtr<IFileOperation> pfo;
  if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&pfo)))) {
    *error = "Failed to create FileOperation instance";
    return false;
  }

  // Elevation prompt enabled for UAC protected files.  This overrides the
  // SILENT, NO_UI and NOERRORUI flags.

  if (FAILED(pfo->SetOperationFlags(
          FOF_NO_UI | FOFX_ADDUNDORECORD | FOF_NOERRORUI | FOF_SILENT |
          FOFX_SHOWELEVATIONPROMPT | FOFX_RECYCLEONDELETE))) {
    *error = "Failed to set operation flags";
    return false;
  }

  // Create an IShellItem from the supplied source path.
  Microsoft::WRL::ComPtr<IShellItem> delete_item;
  if (FAILED(SHCreateItemFromParsingName(path.value().c_str(), nullptr,
                                         IID_PPV_ARGS(&delete_item)))) {
    *error = "Failed to parse path";
    return false;
  }

  Microsoft::WRL::ComPtr<IFileOperationProgressSink> delete_sink(
      new DeleteFileProgressSink);
  if (!delete_sink) {
    *error = "Failed to create delete sink";
    return false;
  }

  BOOL pfAnyOperationsAborted;

  // Processes the queued command DeleteItem. This will trigger
  // the DeleteFileProgressSink to check for Recycle Bin.
  if (!SUCCEEDED(pfo->DeleteItem(delete_item.Get(), delete_sink.Get()))) {
    *error = "Failed to enqueue DeleteItem command";
    return false;
  }

  if (!SUCCEEDED(pfo->PerformOperations())) {
    *error = "Failed to perform delete operation";
    return false;
  }

  if (!SUCCEEDED(pfo->GetAnyOperationsAborted(&pfAnyOperationsAborted))) {
    *error = "Failed to check operation status";
    return false;
  }

  if (pfAnyOperationsAborted) {
    *error = "Operation was aborted";
    return false;
  }

  return true;
}

namespace internal {

bool PlatformTrashItem(const base::FilePath& full_path, std::string* error) {
  return MoveItemToTrashWithError(full_path, false, error);
}

}  // namespace internal

bool GetFolderPath(int key, base::FilePath* result) {
  wchar_t system_buffer[MAX_PATH];

  switch (key) {
    case electron::DIR_RECENT:
      if (FAILED(SHGetFolderPath(nullptr, CSIDL_RECENT, nullptr,
                                 SHGFP_TYPE_CURRENT, system_buffer))) {
        return false;
      }
      *result = base::FilePath(system_buffer);
      break;
  }

  return true;
}

void Beep() {
  MessageBeep(MB_OK);
}

}  // namespace platform_util
