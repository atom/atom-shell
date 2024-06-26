// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_navigation_throttle.h"

#include "content/browser/renderer_host/render_frame_host_impl.h"  // nogncheck
#include "content/public/browser/navigation_handle.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/javascript_environment.h"
#include "ui/base/page_transition_types.h"

namespace electron {

ElectronNavigationThrottle::ElectronNavigationThrottle(
    content::NavigationHandle* navigation_handle)
    : content::NavigationThrottle(navigation_handle) {}

ElectronNavigationThrottle::~ElectronNavigationThrottle() = default;

const char* ElectronNavigationThrottle::GetNameForLogging() {
  return "ElectronNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
ElectronNavigationThrottle::WillStartRequest() {
  auto* handle = navigation_handle();
  auto* contents = handle->GetWebContents();
  if (!contents) {
    NOTREACHED();
  }

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  api::WebContents* api_contents = api::WebContents::From(contents);
  if (!api_contents) {
    // No need to emit any event if the WebContents is not available in JS.
    return PROCEED;
  }

  bool is_renderer_initiated = handle->IsRendererInitiated();

  // Chrome's internal pages always mark navigation as browser-initiated. Let's
  // ignore that.
  // https://source.chromium.org/chromium/chromium/src/+/main:content/browser/renderer_host/navigator.cc;l=883-892;drc=0c8ffbe78dc0ef2047849e45cefb3f621043e956
  if (!is_renderer_initiated &&
      ui::PageTransitionIsWebTriggerable(handle->GetPageTransition())) {
    auto* render_frame_host = contents->GetPrimaryMainFrame();
    auto* rfh_impl =
        static_cast<content::RenderFrameHostImpl*>(render_frame_host);
    is_renderer_initiated = rfh_impl && rfh_impl->web_ui();
  }

  if (is_renderer_initiated &&
      api_contents->EmitNavigationEvent("will-frame-navigate", handle)) {
    return CANCEL;
  }
  if (is_renderer_initiated && handle->IsInMainFrame() &&
      api_contents->EmitNavigationEvent("will-navigate", handle)) {
    return CANCEL;
  }
  return PROCEED;
}

content::NavigationThrottle::ThrottleCheckResult
ElectronNavigationThrottle::WillRedirectRequest() {
  auto* handle = navigation_handle();
  auto* contents = handle->GetWebContents();
  if (!contents) {
    NOTREACHED();
  }

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  api::WebContents* api_contents = api::WebContents::From(contents);
  if (!api_contents) {
    // No need to emit any event if the WebContents is not available in JS.
    return PROCEED;
  }

  if (api_contents->EmitNavigationEvent("will-redirect", handle)) {
    return CANCEL;
  }
  return PROCEED;
}

}  // namespace electron
