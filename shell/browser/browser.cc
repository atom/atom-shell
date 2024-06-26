// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/browser.h"

#include <memory>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/common/chrome_paths.h"
#include "shell/browser/browser_observer.h"
#include "shell/browser/electron_browser_main_parts.h"
#include "shell/browser/login_handler.h"
#include "shell/browser/native_window.h"
#include "shell/browser/window_list.h"
#include "shell/common/application_info.h"
#include "shell/common/electron_paths.h"
#include "shell/common/gin_helper/arguments.h"
#include "shell/common/thread_restrictions.h"

namespace electron {

LoginItemSettings::LoginItemSettings() = default;
LoginItemSettings::~LoginItemSettings() = default;
LoginItemSettings::LoginItemSettings(const LoginItemSettings& other) = default;

#if BUILDFLAG(IS_WIN)
LaunchItem::LaunchItem() = default;
LaunchItem::~LaunchItem() = default;
LaunchItem::LaunchItem(const LaunchItem& other) = default;
#endif

namespace {

// Call |quit| after Chromium is fully started.
//
// This is important for quitting immediately in the "ready" event, when
// certain initialization task may still be pending, and quitting at that time
// could end up with crash on exit.
void RunQuitClosure(base::OnceClosure quit) {
  // On Linux/Windows the "ready" event is emitted in "PreMainMessageLoopRun",
  // make sure we quit after message loop has run for once.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                              std::move(quit));
}

}  // namespace

Browser::Browser() {
  WindowList::AddObserver(this);
}

Browser::~Browser() {
  WindowList::RemoveObserver(this);
}

// static
Browser* Browser::Get() {
  return ElectronBrowserMainParts::Get()->browser();
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
void Browser::Focus(gin::Arguments* args) {
  // Focus on the first visible window.
  for (auto* const window : WindowList::GetWindows()) {
    if (window->IsVisible()) {
      window->Focus(true);
      break;
    }
  }
}
#endif

void Browser::Quit() {
  if (is_quitting_)
    return;

  is_quitting_ = HandleBeforeQuit();
  if (!is_quitting_)
    return;

  if (electron::WindowList::IsEmpty())
    NotifyAndShutdown();
  else
    electron::WindowList::CloseAllWindows();
}

void Browser::Exit(gin::Arguments* args) {
  int code = 0;
  args->GetNext(&code);

  if (!ElectronBrowserMainParts::Get()->SetExitCode(code)) {
    // Message loop is not ready, quit directly.
    exit(code);
  } else {
    // Prepare to quit when all windows have been closed.
    is_quitting_ = true;

    // Remember this caller so that we don't emit unrelated events.
    is_exiting_ = true;

    // Must destroy windows before quitting, otherwise bad things can happen.
    if (electron::WindowList::IsEmpty()) {
      Shutdown();
    } else {
      // Unlike Quit(), we do not ask to close window, but destroy the window
      // without asking.
      electron::WindowList::DestroyAllWindows();
    }
  }
}

void Browser::Shutdown() {
  if (is_shutdown_)
    return;

  is_shutdown_ = true;
  is_quitting_ = true;

  for (BrowserObserver& observer : observers_)
    observer.OnQuit();

  if (quit_main_message_loop_) {
    RunQuitClosure(std::move(quit_main_message_loop_));
  } else {
    // There is no message loop available so we are in early stage, wait until
    // the quit_main_message_loop_ is available.
    // Exiting now would leave defunct processes behind.
  }
}

std::string Browser::GetVersion() const {
  std::string ret = OverriddenApplicationVersion();
  if (ret.empty())
    ret = GetExecutableFileVersion();
  return ret;
}

void Browser::SetVersion(const std::string& version) {
  OverriddenApplicationVersion() = version;
}

std::string Browser::GetName() const {
  std::string ret = OverriddenApplicationName();
  if (ret.empty())
    ret = GetExecutableFileProductName();
  return ret;
}

void Browser::SetName(const std::string& name) {
  OverriddenApplicationName() = name;
}

bool Browser::OpenFile(const std::string& file_path) {
  bool prevent_default = false;
  for (BrowserObserver& observer : observers_)
    observer.OnOpenFile(&prevent_default, file_path);

  return prevent_default;
}

void Browser::OpenURL(const std::string& url) {
  for (BrowserObserver& observer : observers_)
    observer.OnOpenURL(url);
}

void Browser::Activate(bool has_visible_windows) {
  for (BrowserObserver& observer : observers_)
    observer.OnActivate(has_visible_windows);
}

void Browser::WillFinishLaunching() {
  for (BrowserObserver& observer : observers_)
    observer.OnWillFinishLaunching();
}

void Browser::DidFinishLaunching(base::Value::Dict launch_info) {
  // Make sure the userData directory is created.
  ScopedAllowBlockingForElectron allow_blocking;
  base::FilePath user_data;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    base::CreateDirectoryAndGetError(user_data, nullptr);
#if BUILDFLAG(IS_WIN)
    base::SetExtraNoExecuteAllowedPath(chrome::DIR_USER_DATA);
#endif
  }

  is_ready_ = true;
  if (ready_promise_) {
    ready_promise_->Resolve();
  }
  for (BrowserObserver& observer : observers_)
    observer.OnFinishLaunching(launch_info.Clone());
}

v8::Local<v8::Value> Browser::WhenReady(v8::Isolate* isolate) {
  if (!ready_promise_) {
    ready_promise_ = std::make_unique<gin_helper::Promise<void>>(isolate);
    if (is_ready()) {
      ready_promise_->Resolve();
    }
  }
  return ready_promise_->GetHandle();
}

void Browser::OnAccessibilitySupportChanged() {
  for (BrowserObserver& observer : observers_)
    observer.OnAccessibilitySupportChanged();
}

void Browser::PreMainMessageLoopRun() {
  for (BrowserObserver& observer : observers_) {
    observer.OnPreMainMessageLoopRun();
  }
}

void Browser::PreCreateThreads() {
  for (BrowserObserver& observer : observers_) {
    observer.OnPreCreateThreads();
  }
}

void Browser::SetMainMessageLoopQuitClosure(base::OnceClosure quit_closure) {
  if (is_shutdown_)
    RunQuitClosure(std::move(quit_closure));
  else
    quit_main_message_loop_ = std::move(quit_closure);
}

void Browser::NotifyAndShutdown() {
  if (is_shutdown_)
    return;

  bool prevent_default = false;
  for (BrowserObserver& observer : observers_)
    observer.OnWillQuit(&prevent_default);

  if (prevent_default) {
    is_quitting_ = false;
    return;
  }

  Shutdown();
}

bool Browser::HandleBeforeQuit() {
  bool prevent_default = false;
  for (BrowserObserver& observer : observers_)
    observer.OnBeforeQuit(&prevent_default);

  return !prevent_default;
}

void Browser::OnWindowCloseCancelled(NativeWindow* window) {
  if (is_quitting_)
    // Once a beforeunload handler has prevented the closing, we think the quit
    // is cancelled too.
    is_quitting_ = false;
}

void Browser::OnWindowAllClosed() {
  if (is_exiting_) {
    Shutdown();
  } else if (is_quitting_) {
    NotifyAndShutdown();
  } else {
    for (BrowserObserver& observer : observers_)
      observer.OnWindowAllClosed();
  }
}

#if BUILDFLAG(IS_MAC)
void Browser::NewWindowForTab() {
  for (BrowserObserver& observer : observers_)
    observer.OnNewWindowForTab();
}

void Browser::DidBecomeActive() {
  for (BrowserObserver& observer : observers_)
    observer.OnDidBecomeActive();
}

void Browser::DidResignActive() {
  for (BrowserObserver& observer : observers_)
    observer.OnDidResignActive();
}
#endif

}  // namespace electron
