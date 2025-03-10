// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/platform_util.h"

#include <fcntl.h>

#include <stdio.h>
#include <optional>
#include <string>
#include <vector>

#include <gdk/gdk.h>

#include "base/cancelable_callback.h"
#include "base/containers/contains.h"
#include "base/containers/map_util.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/nix/xdg_util.h"
#include "base/no_destructor.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "components/dbus/thread_linux/dbus_thread_linux.h"
#include "content/public/browser/browser_thread.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"

#include "shell/common/platform_util_internal.h"
#include "url/gurl.h"

#define ELECTRON_TRASH "ELECTRON_TRASH"

namespace platform_util {
void OpenFolder(const base::FilePath& full_path);
}

namespace {

const char kMethodListActivatableNames[] = "ListActivatableNames";
const char kMethodNameHasOwner[] = "NameHasOwner";

const char kFreedesktopFileManagerName[] = "org.freedesktop.FileManager1";
const char kFreedesktopFileManagerPath[] = "/org/freedesktop/FileManager1";

const char kMethodShowItems[] = "ShowItems";

const char kFreedesktopPortalName[] = "org.freedesktop.portal.Desktop";
const char kFreedesktopPortalPath[] = "/org/freedesktop/portal/desktop";
const char kFreedesktopPortalOpenURI[] = "org.freedesktop.portal.OpenURI";

const char kXdgCurrentDesktopEnvVar[] = "XDG_CURRENT_DESKTOP";
const char kOriginalXdgCurrentDesktopEnvVar[] = "ORIGINAL_XDG_CURRENT_DESKTOP";

const char kMethodOpenDirectory[] = "OpenDirectory";

class ShowItemHelper {
 public:
  static ShowItemHelper& GetInstance() {
    static base::NoDestructor<ShowItemHelper> instance;
    return *instance;
  }

  ShowItemHelper() = default;

  ShowItemHelper(const ShowItemHelper&) = delete;
  ShowItemHelper& operator=(const ShowItemHelper&) = delete;

  void ShowItemInFolder(const base::FilePath& full_path) {
    if (!bus_) {
      // Sets up the D-Bus connection.
      dbus::Bus::Options bus_options;
      bus_options.bus_type = dbus::Bus::SESSION;
      bus_options.connection_type = dbus::Bus::PRIVATE;
      bus_options.dbus_task_runner = dbus_thread_linux::GetTaskRunner();
      bus_ = base::MakeRefCounted<dbus::Bus>(bus_options);
    }

    if (!dbus_proxy_) {
      dbus_proxy_ = bus_->GetObjectProxy(DBUS_SERVICE_DBUS,
                                         dbus::ObjectPath(DBUS_PATH_DBUS));
    }

    if (prefer_filemanager_interface_.has_value()) {
      if (prefer_filemanager_interface_.value()) {
        ShowItemUsingFileManager(full_path);
      } else {
        ShowItemUsingFreedesktopPortal(full_path);
      }
    } else {
      CheckFileManagerRunning(full_path);
    }
  }

 private:
  void CheckFileManagerRunning(const base::FilePath& full_path) {
    dbus::MethodCall method_call(DBUS_INTERFACE_DBUS, kMethodNameHasOwner);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(kFreedesktopFileManagerName);

    dbus_proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::BindOnce(&ShowItemHelper::CheckFileManagerRunningResponse,
                       base::Unretained(this), full_path));
  }

  void CheckFileManagerRunningResponse(const base::FilePath& full_path,
                                       dbus::Response* response) {
    if (prefer_filemanager_interface_.has_value()) {
      ShowItemInFolder(full_path);
      return;
    }

    bool is_running = false;

    if (!response) {
      LOG(ERROR) << "Failed to call " << kMethodNameHasOwner;
    } else {
      dbus::MessageReader reader(response);
      bool owned = false;

      if (!reader.PopBool(&owned)) {
        LOG(ERROR) << "Failed to read " << kMethodNameHasOwner << " response";
      } else if (owned) {
        is_running = true;
      }
    }

    if (is_running) {
      prefer_filemanager_interface_ = true;
      ShowItemInFolder(full_path);
    } else {
      CheckFileManagerActivatable(full_path);
    }
  }

  void CheckFileManagerActivatable(const base::FilePath& full_path) {
    dbus::MethodCall method_call(DBUS_INTERFACE_DBUS,
                                 kMethodListActivatableNames);
    dbus_proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::BindOnce(&ShowItemHelper::CheckFileManagerActivatableResponse,
                       base::Unretained(this), full_path));
  }

  void CheckFileManagerActivatableResponse(const base::FilePath& full_path,
                                           dbus::Response* response) {
    if (prefer_filemanager_interface_.has_value()) {
      ShowItemInFolder(full_path);
      return;
    }

    bool is_activatable = false;

    if (!response) {
      LOG(ERROR) << "Failed to call " << kMethodListActivatableNames;
    } else {
      dbus::MessageReader reader(response);
      std::vector<std::string> names;
      if (!reader.PopArrayOfStrings(&names)) {
        LOG(ERROR) << "Failed to read " << kMethodListActivatableNames
                   << " response";
      } else if (base::Contains(names, kFreedesktopFileManagerName)) {
        is_activatable = true;
      }
    }

    prefer_filemanager_interface_ = is_activatable;
    ShowItemInFolder(full_path);
  }

  void ShowItemUsingFreedesktopPortal(const base::FilePath& full_path) {
    if (!object_proxy_) {
      object_proxy_ = bus_->GetObjectProxy(
          kFreedesktopPortalName, dbus::ObjectPath(kFreedesktopPortalPath));
    }

    base::ScopedFD fd(
        HANDLE_EINTR(open(full_path.value().c_str(), O_RDONLY | O_CLOEXEC)));
    if (!fd.is_valid()) {
      LOG(ERROR) << "Failed to open " << full_path << " for URI portal";

      // If the call fails, at least open the parent folder.
      platform_util::OpenFolder(full_path.DirName());

      return;
    }

    dbus::MethodCall open_directory_call(kFreedesktopPortalOpenURI,
                                         kMethodOpenDirectory);
    dbus::MessageWriter writer(&open_directory_call);

    writer.AppendString("");

    // Note that AppendFileDescriptor() duplicates the fd, so we shouldn't
    // release ownership of it here.
    writer.AppendFileDescriptor(fd.get());

    dbus::MessageWriter options_writer(nullptr);
    writer.OpenArray("{sv}", &options_writer);
    writer.CloseContainer(&options_writer);

    ShowItemUsingBusCall(&open_directory_call, full_path);
  }

  void ShowItemUsingFileManager(const base::FilePath& full_path) {
    if (!object_proxy_) {
      object_proxy_ =
          bus_->GetObjectProxy(kFreedesktopFileManagerName,
                               dbus::ObjectPath(kFreedesktopFileManagerPath));
    }

    dbus::MethodCall show_items_call(kFreedesktopFileManagerName,
                                     kMethodShowItems);
    dbus::MessageWriter writer(&show_items_call);

    writer.AppendArrayOfStrings(
        {"file://" + base::EscapePath(
                         full_path.value())});  // List of file(s) to highlight.
    writer.AppendString({});                    // startup-id

    ShowItemUsingBusCall(&show_items_call, full_path);
  }

  void ShowItemUsingBusCall(dbus::MethodCall* call,
                            const base::FilePath& full_path) {
    object_proxy_->CallMethod(
        call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::BindOnce(&ShowItemHelper::ShowItemInFolderResponse,
                       base::Unretained(this), full_path, call->GetMember()));
  }

  void ShowItemInFolderResponse(const base::FilePath& full_path,
                                const std::string& method,
                                dbus::Response* response) {
    if (response)
      return;

    LOG(ERROR) << "Error calling " << method;
    // If the bus call fails, at least open the parent folder.
    platform_util::OpenFolder(full_path.DirName());
  }

  scoped_refptr<dbus::Bus> bus_;
  raw_ptr<dbus::ObjectProxy> dbus_proxy_ = nullptr;
  raw_ptr<dbus::ObjectProxy> object_proxy_ = nullptr;

  std::optional<bool> prefer_filemanager_interface_;
};

// Descriptions pulled from https://linux.die.net/man/1/xdg-open
std::string GetErrorDescription(int error_code) {
  switch (error_code) {
    case 1:
      return "Error in command line syntax";
    case 2:
      return "The item does not exist";
    case 3:
      return "A required tool could not be found";
    case 4:
      return "The action failed";
    default:
      return "";
  }
}

bool XDGUtil(const std::vector<std::string>& argv,
             const base::FilePath& working_directory,
             const bool wait_for_exit,
             const bool focus_launched_process,
             platform_util::OpenCallback callback) {
  base::LaunchOptions options;
  if (focus_launched_process) {
    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    base::RepeatingClosure quit_loop = run_loop.QuitClosure();
    base::nix::CreateLaunchOptionsWithXdgActivation(base::BindOnce(
        [](base::RepeatingClosure quit_loop, base::LaunchOptions* options_out,
           base::LaunchOptions options) {
          // Correct the XDG_CURRENT_DESKTOP environment variable before calling
          // XDG, in case it was changed for compatibility.
          if (const auto* orig = base::FindOrNull(
                  options.environment, kOriginalXdgCurrentDesktopEnvVar))
            options.environment.emplace(kXdgCurrentDesktopEnvVar, *orig);
          *options_out = std::move(options);
          std::move(quit_loop).Run();
        },
        std::move(quit_loop), &options));
    run_loop.Run();
  }
  options.current_directory = working_directory;
  options.allow_new_privs = true;
  // xdg-open can fall back on mailcap which eventually might plumb through
  // to a command that needs a terminal.  Set the environment variable telling
  // it that we definitely don't have a terminal available and that it should
  // bring up a new terminal if necessary.  See "man mailcap".
  options.environment["MM_NOTTTY"] = "1";

  base::Process process = base::LaunchProcess(argv, options);
  if (!process.IsValid())
    return false;

  if (wait_for_exit) {
    base::ScopedAllowBaseSyncPrimitivesForTesting
        allow_sync;  // required by WaitForExit
    int exit_code = -1;
    bool success = process.WaitForExit(&exit_code);
    if (!callback.is_null())
      std::move(callback).Run(GetErrorDescription(exit_code));
    return success ? (exit_code == 0) : false;
  }

  base::EnsureProcessGetsReaped(std::move(process));
  return true;
}

bool XDGOpen(const base::FilePath& working_directory,
             const std::string& path,
             const bool wait_for_exit,
             platform_util::OpenCallback callback) {
  return XDGUtil({"xdg-open", path}, working_directory, wait_for_exit,
                 /*focus_launched_process=*/true, std::move(callback));
}

bool XDGEmail(const std::string& email, const bool wait_for_exit) {
  return XDGUtil({"xdg-email", email}, base::FilePath(), wait_for_exit,
                 /*focus_launched_process=*/true,
                 platform_util::OpenCallback());
}

}  // namespace

namespace platform_util {

void ShowItemInFolder(const base::FilePath& full_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ShowItemHelper::GetInstance().ShowItemInFolder(full_path);
}

void OpenPath(const base::FilePath& full_path, OpenCallback callback) {
  // This is async, so we don't care about the return value.
  XDGOpen(full_path.DirName(), full_path.value(), true, std::move(callback));
}

void OpenFolder(const base::FilePath& full_path) {
  if (!base::DirectoryExists(full_path))
    return;

  XDGOpen(full_path.DirName(), ".", false, platform_util::OpenCallback());
}

void OpenExternal(const GURL& url,
                  const OpenExternalOptions& options,
                  OpenCallback callback) {
  // Don't wait for exit, since we don't want to wait for the browser/email
  // client window to close before returning
  if (url.SchemeIs("mailto")) {
    bool success = XDGEmail(url.spec(), false);
    std::move(callback).Run(success ? "" : "Failed to open path");
  } else {
    bool success = XDGOpen(base::FilePath(), url.spec(), false,
                           platform_util::OpenCallback());
    std::move(callback).Run(success ? "" : "Failed to open path");
  }
}

bool MoveItemToTrash(const base::FilePath& full_path, bool delete_on_fail) {
  auto env = base::Environment::Create();

  // find the trash method
  std::string trash;
  if (!env->GetVar(ELECTRON_TRASH, &trash)) {
    // Determine desktop environment and set accordingly.
    const auto desktop_env(base::nix::GetDesktopEnvironment(env.get()));
    if (desktop_env == base::nix::DESKTOP_ENVIRONMENT_KDE4 ||
        desktop_env == base::nix::DESKTOP_ENVIRONMENT_KDE5) {
      trash = "kioclient5";
    } else if (desktop_env == base::nix::DESKTOP_ENVIRONMENT_KDE3) {
      trash = "kioclient";
    }
  }

  // build the invocation
  std::vector<std::string> argv;
  const auto& filename = full_path.value();
  if (trash == "kioclient5" || trash == "kioclient") {
    argv = {trash, "move", filename, "trash:/"};
  } else if (trash == "trash-cli") {
    argv = {"trash-put", filename};
  } else if (trash == "gvfs-trash") {
    argv = {"gvfs-trash", filename};  // deprecated, but still exists
  } else {
    argv = {"gio", "trash", filename};
  }

  return XDGUtil(argv, base::FilePath(), true, /*focus_launched_process=*/false,
                 platform_util::OpenCallback());
}

namespace internal {

bool PlatformTrashItem(const base::FilePath& full_path, std::string* error) {
  if (!MoveItemToTrash(full_path, false)) {
    // TODO(nornagon): at least include the exit code?
    *error = "Failed to move item to trash";
    return false;
  }
  return true;
}

}  // namespace internal

void Beep() {
  auto* display = gdk_display_get_default();
  gdk_display_beep(display);
}

bool GetDesktopName(std::string* setme) {
  return base::Environment::Create()->GetVar("CHROME_DESKTOP", setme);
}

std::string GetXdgAppId() {
  std::string desktop_file_name;
  if (GetDesktopName(&desktop_file_name)) {
    const std::string kDesktopExtension{".desktop"};
    if (base::EndsWith(desktop_file_name, kDesktopExtension,
                       base::CompareCase::INSENSITIVE_ASCII)) {
      desktop_file_name.resize(desktop_file_name.size() -
                               kDesktopExtension.size());
    }
  }

  return desktop_file_name;
}

}  // namespace platform_util
