// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_BROWSER_WINDOW_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_BROWSER_WINDOW_H_

#include <string>

#include "base/cancelable_callback.h"
#include "shell/browser/api/electron_api_base_window.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/ui/drag_util.h"

namespace gin_helper {
class ErrorThrower;
}  // namespace gin_helper

namespace electron::api {

class BrowserWindow : public BaseWindow,
                      private content::WebContentsObserver,
                      private ExtendedWebContentsObserver {
 public:
  static gin_helper::WrappableBase* New(gin_helper::ErrorThrower thrower,
                                        gin::Arguments* args);

  static void BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype);

  // Returns the BrowserWindow object from |native_window|.
  static v8::Local<v8::Value> From(v8::Isolate* isolate,
                                   NativeWindow* native_window);

  base::WeakPtr<BrowserWindow> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // disable copy
  BrowserWindow(const BrowserWindow&) = delete;
  BrowserWindow& operator=(const BrowserWindow&) = delete;

 protected:
  BrowserWindow(gin::Arguments* args, const gin_helper::Dictionary& options);
  ~BrowserWindow() override;

  // content::WebContentsObserver:
  void BeforeUnloadDialogCancelled() override;
  void OnRendererUnresponsive(content::RenderProcessHost*) override;
  void OnRendererResponsive(
      content::RenderProcessHost* render_process_host) override;
  void WebContentsDestroyed() override;

  // ExtendedWebContentsObserver:
  void OnSetContentBounds(const gfx::Rect& rect) override;
  void OnActivateContents() override;
  void OnPageTitleUpdated(const std::u16string& title,
                          bool explicit_set) override;

  // NativeWindowObserver:
  void RequestPreferredWidth(int* width) override;
  void OnCloseButtonClicked(bool* prevent_default) override;
  void OnWindowIsKeyChanged(bool is_key) override;
  void UpdateWindowControlsOverlay(const gfx::Rect& bounding_rect) override;

  // BaseWindow:
  void OnWindowBlur() override;
  void OnWindowFocus() override;
  void OnWindowLeaveFullScreen() override;
  void CloseImmediately() override;
  void Focus() override;
  void Blur() override;
  void SetBackgroundColor(const std::string& color_name) override;

  // BrowserWindow APIs.
  void FocusOnWebView();
  void BlurWebView();
  bool IsWebViewFocused();
  v8::Local<v8::Value> GetWebContents(v8::Isolate* isolate);

 private:
  // Helpers.

  // Schedule a notification unresponsive event.
  void ScheduleUnresponsiveEvent(int ms);

  // Dispatch unresponsive event to observers.
  void NotifyWindowUnresponsive();

  // Closure that would be called when window is unresponsive when closing,
  // it should be cancelled when we can prove that the window is responsive.
  base::CancelableRepeatingClosure window_unresponsive_closure_;

  v8::Global<v8::Value> web_contents_;
  v8::Global<v8::Value> web_contents_view_;
  base::WeakPtr<api::WebContents> api_web_contents_;

  base::WeakPtrFactory<BrowserWindow> weak_factory_{this};
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_BROWSER_WINDOW_H_
