// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_CONTENTS_VIEW_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_CONTENTS_VIEW_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "shell/browser/api/electron_api_view.h"
#include "shell/browser/draggable_region_provider.h"

namespace gin_helper {
class Dictionary;
}

namespace electron::api {

class WebContents;

class WebContentsView : public View,
                        private content::WebContentsObserver,
                        public DraggableRegionProvider {
 public:
  // Create a new instance of WebContentsView.
  static gin::Handle<WebContentsView> Create(
      v8::Isolate* isolate,
      const gin_helper::Dictionary& web_preferences);

  // Return the cached constructor function.
  static v8::Local<v8::Function> GetConstructor(v8::Isolate* isolate);

  // gin_helper::Wrappable
  static void BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype);

  // Public APIs.
  gin::Handle<WebContents> GetWebContents(v8::Isolate* isolate);
  void SetBackgroundColor(std::optional<WrappedSkColor> color);

  int NonClientHitTest(const gfx::Point& point) override;

 protected:
  // Takes an existing WebContents.
  WebContentsView(v8::Isolate* isolate, gin::Handle<WebContents> web_contents);
  ~WebContentsView() override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  // views::ViewObserver
  void OnViewAddedToWidget(views::View* view) override;
  void OnViewRemovedFromWidget(views::View* view) override;

 private:
  static gin_helper::WrappableBase* New(gin_helper::Arguments* args);

  // Keep a reference to v8 wrapper.
  v8::Global<v8::Value> web_contents_;
  raw_ptr<api::WebContents> api_web_contents_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_CONTENTS_VIEW_H_
