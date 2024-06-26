// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_BROWSER_H_
#define ELECTRON_SHELL_BROWSER_BROWSER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "gin/dictionary.h"
#include "shell/browser/browser_observer.h"
#include "shell/browser/window_list_observer.h"
#include "shell/common/gin_converters/login_item_settings_converter.h"
#include "shell/common/gin_helper/promise.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#include "base/files/file_path.h"
#include "shell/browser/ui/win/taskbar_host.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "ui/base/cocoa/secure_password_input.h"
#endif

namespace base {
class FilePath;
}

namespace gin_helper {
class Arguments;
}

namespace electron {

class ElectronMenuModel;

#if BUILDFLAG(IS_WIN)
struct LaunchItem {
  std::wstring name;
  std::wstring path;
  std::wstring scope;
  std::vector<std::wstring> args;
  bool enabled = true;

  LaunchItem();
  ~LaunchItem();
  LaunchItem(const LaunchItem&);
};
#endif

struct LoginItemSettings {
  bool open_at_login = false;
  bool open_as_hidden = false;
  bool restore_state = false;
  bool opened_at_login = false;
  bool opened_as_hidden = false;
  std::u16string path;
  std::vector<std::u16string> args;

#if BUILDFLAG(IS_MAC)
  std::string type = "mainAppService";
  std::string service_name;
  std::string status;
#elif BUILDFLAG(IS_WIN)
  // used in browser::setLoginItemSettings
  bool enabled = true;
  std::wstring name;

  // used in browser::getLoginItemSettings
  bool executable_will_launch_at_login = false;
  std::vector<LaunchItem> launch_items;
#endif

  LoginItemSettings();
  ~LoginItemSettings();
  LoginItemSettings(const LoginItemSettings&);
};

// This class is used for control application-wide operations.
class Browser : private WindowListObserver {
 public:
  Browser();
  ~Browser() override;

  // disable copy
  Browser(const Browser&) = delete;
  Browser& operator=(const Browser&) = delete;

  static Browser* Get();

  // Try to close all windows and quit the application.
  void Quit();

  // Exit the application immediately and set exit code.
  void Exit(gin::Arguments* args);

  // Cleanup everything and shutdown the application gracefully.
  void Shutdown();

  // Focus the application.
  void Focus(gin::Arguments* args);

  // Returns the version of the executable (or bundle).
  std::string GetVersion() const;

  // Overrides the application version.
  void SetVersion(const std::string& version);

  // Returns the application's name, default is just Electron.
  std::string GetName() const;

  // Overrides the application name.
  void SetName(const std::string& name);

  // Add the |path| to recent documents list.
  void AddRecentDocument(const base::FilePath& path);

  // Clear the recent documents list.
  void ClearRecentDocuments();

#if BUILDFLAG(IS_WIN)
  // Set the application user model ID.
  void SetAppUserModelID(const std::wstring& name);
#endif

  // Remove the default protocol handler registry key
  bool RemoveAsDefaultProtocolClient(const std::string& protocol,
                                     gin::Arguments* args);

  // Set as default handler for a protocol.
  bool SetAsDefaultProtocolClient(const std::string& protocol,
                                  gin::Arguments* args);

  // Query the current state of default handler for a protocol.
  bool IsDefaultProtocolClient(const std::string& protocol,
                               gin::Arguments* args);

  std::u16string GetApplicationNameForProtocol(const GURL& url);

#if !BUILDFLAG(IS_LINUX)
  // get the name, icon and path for an application
  v8::Local<v8::Promise> GetApplicationInfoForProtocol(v8::Isolate* isolate,
                                                       const GURL& url);
#endif

  // Set/Get the badge count.
  bool SetBadgeCount(std::optional<int> count);
  [[nodiscard]] int badge_count() const { return badge_count_; }

  void SetLoginItemSettings(LoginItemSettings settings);
  v8::Local<v8::Value> GetLoginItemSettings(const LoginItemSettings& options);

#if BUILDFLAG(IS_MAC)
  // Set the handler which decides whether to shutdown.
  void SetShutdownHandler(base::RepeatingCallback<bool()> handler);

  // Hide the application.
  void Hide();
  bool IsHidden();

  // Show the application.
  void Show();

  // Creates an activity and sets it as the one currently in use.
  void SetUserActivity(const std::string& type,
                       base::Value::Dict user_info,
                       gin::Arguments* args);

  // Returns the type name of the current user activity.
  std::string GetCurrentActivityType();

  // Invalidates an activity and marks it as no longer eligible for
  // continuation
  void InvalidateCurrentActivity();

  // Marks this activity object as inactive without invalidating it.
  void ResignCurrentActivity();

  // Updates the current user activity
  void UpdateCurrentActivity(const std::string& type,
                             base::Value::Dict user_info);

  // Indicates that an user activity is about to be resumed.
  bool WillContinueUserActivity(const std::string& type);

  // Indicates a failure to resume a Handoff activity.
  void DidFailToContinueUserActivity(const std::string& type,
                                     const std::string& error);

  // Resumes an activity via hand-off.
  bool ContinueUserActivity(const std::string& type,
                            base::Value::Dict user_info,
                            base::Value::Dict details);

  // Indicates that an activity was continued on another device.
  void UserActivityWasContinued(const std::string& type,
                                base::Value::Dict user_info);

  // Gives an opportunity to update the Handoff payload.
  bool UpdateUserActivityState(const std::string& type,
                               base::Value::Dict user_info);

  void ApplyForcedRTL();

  // Bounce the dock icon.
  enum class BounceType{
      kCritical = 0,        // NSCriticalRequest
      kInformational = 10,  // NSInformationalRequest
  };
  int DockBounce(BounceType type);
  void DockCancelBounce(int request_id);

  // Bounce the Downloads stack.
  void DockDownloadFinished(const std::string& filePath);

  // Set/Get dock's badge text.
  void DockSetBadgeText(const std::string& label);
  std::string DockGetBadgeText();

  // Hide/Show dock.
  void DockHide();
  v8::Local<v8::Promise> DockShow(v8::Isolate* isolate);
  bool DockIsVisible();

  // Set docks' menu.
  void DockSetMenu(ElectronMenuModel* model);

  // Set docks' icon.
  void DockSetIcon(v8::Isolate* isolate, v8::Local<v8::Value> icon);

  void SetLaunchedAtLogin(bool launched_at_login) {
    was_launched_at_login_ = launched_at_login;
  }

#endif  // BUILDFLAG(IS_MAC)

  void ShowAboutPanel();
  void SetAboutPanelOptions(base::Value::Dict options);

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  void ShowEmojiPanel();
#endif

#if BUILDFLAG(IS_WIN)
  struct UserTask {
    base::FilePath program;
    std::wstring arguments;
    std::wstring title;
    std::wstring description;
    base::FilePath working_dir;
    base::FilePath icon_path;
    int icon_index;

    UserTask();
    UserTask(const UserTask&);
    ~UserTask();
  };

  // Add a custom task to jump list.
  bool SetUserTasks(const std::vector<UserTask>& tasks);

  // Returns the application user model ID, if there isn't one, then create
  // one from app's name.
  // The returned string managed by Browser, and should not be modified.
  PCWSTR GetAppUserModelID();
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_LINUX)
  // Whether Unity launcher is running.
  bool IsUnityRunning();
#endif  // BUILDFLAG(IS_LINUX)

  // Tell the application to open a file.
  bool OpenFile(const std::string& file_path);

  // Tell the application to open a url.
  void OpenURL(const std::string& url);

#if BUILDFLAG(IS_MAC)
  // Tell the application to create a new window for a tab.
  void NewWindowForTab();

  // Indicate that the app is now active.
  void DidBecomeActive();
  // Indicate that the app is no longer active and doesn’t have focus.
  void DidResignActive();

#endif  // BUILDFLAG(IS_MAC)

  // Tell the application that application is activated with visible/invisible
  // windows.
  void Activate(bool has_visible_windows);

  bool IsEmojiPanelSupported();

  // Tell the application the loading has been done.
  void WillFinishLaunching();
  void DidFinishLaunching(base::Value::Dict launch_info);

  void OnAccessibilitySupportChanged();

  void PreMainMessageLoopRun();
  void PreCreateThreads();

  // Stores the supplied |quit_closure|, to be run when the last Browser
  // instance is destroyed.
  void SetMainMessageLoopQuitClosure(base::OnceClosure quit_closure);

  void AddObserver(BrowserObserver* obs) { observers_.AddObserver(obs); }

  void RemoveObserver(BrowserObserver* obs) { observers_.RemoveObserver(obs); }

#if BUILDFLAG(IS_MAC)
  // Returns whether secure input is enabled
  bool IsSecureKeyboardEntryEnabled();
  void SetSecureKeyboardEntryEnabled(bool enabled);
#endif

  bool is_shutting_down() const { return is_shutdown_; }
  bool is_quitting() const { return is_quitting_; }
  bool is_ready() const { return is_ready_; }
  v8::Local<v8::Value> WhenReady(v8::Isolate* isolate);

 protected:
  // Returns the version of application bundle or executable file.
  std::string GetExecutableFileVersion() const;

  // Returns the name of application bundle or executable file.
  std::string GetExecutableFileProductName() const;

  // Send the will-quit message and then shutdown the application.
  void NotifyAndShutdown();

  // Send the before-quit message and start closing windows.
  bool HandleBeforeQuit();

  bool is_quitting_ = false;

 private:
  // WindowListObserver implementations:
  void OnWindowCloseCancelled(NativeWindow* window) override;
  void OnWindowAllClosed() override;

  // Observers of the browser.
  base::ObserverList<BrowserObserver> observers_;

  // Tracks tasks requesting file icons.
  base::CancelableTaskTracker cancelable_task_tracker_;

  // Whether `app.exit()` has been called
  bool is_exiting_ = false;

  // Whether "ready" event has been emitted.
  bool is_ready_ = false;

  // The browser is being shutdown.
  bool is_shutdown_ = false;

  // Null until/unless the default main message loop is running.
  base::OnceClosure quit_main_message_loop_;

  int badge_count_ = 0;

  std::unique_ptr<gin_helper::Promise<void>> ready_promise_;

#if BUILDFLAG(IS_MAC)
  std::unique_ptr<ui::ScopedPasswordInputEnabler> password_input_enabler_;
  base::Time last_dock_show_;
  bool was_launched_at_login_;
#endif

  base::Value::Dict about_panel_options_;

#if BUILDFLAG(IS_WIN)
  void UpdateBadgeContents(HWND hwnd,
                           const std::optional<std::string>& badge_content,
                           const std::string& badge_alt_string);

  // In charge of running taskbar related APIs.
  TaskbarHost taskbar_host_;
#endif
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_BROWSER_H_
