// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Adam Roben <adam@roben.org>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_UI_INSPECTABLE_WEB_CONTENTS_VIEW_H_
#define ELECTRON_SHELL_BROWSER_UI_INSPECTABLE_WEB_CONTENTS_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "electron/shell/common/api/api.mojom.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/views/view.h"

class DevToolsContentsResizingStrategy;
namespace views {
class WebView;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace electron {

class InspectableWebContents;
class InspectableWebContentsViewDelegate;

class InspectableWebContentsView : public views::View {
 public:
  explicit InspectableWebContentsView(
      InspectableWebContents* inspectable_web_contents);
  ~InspectableWebContentsView() override;

  InspectableWebContents* inspectable_web_contents() {
    return inspectable_web_contents_;
  }

  // The delegate manages its own life.
  void SetDelegate(InspectableWebContentsViewDelegate* delegate) {
    delegate_ = delegate;
  }
  InspectableWebContentsViewDelegate* GetDelegate() const { return delegate_; }

  void ShowDevTools(bool activate);
  void CloseDevTools();
  bool IsDevToolsViewShowing();
  bool IsDevToolsViewFocused();
  void SetIsDocked(bool docked, bool activate);
  void SetContentsResizingStrategy(
      const DevToolsContentsResizingStrategy& strategy);
  void SetTitle(const std::u16string& title);
  const std::u16string GetTitle();

  // views::View:
  void Layout(PassKey) override;
#if BUILDFLAG(IS_MAC)
  bool OnMousePressed(const ui::MouseEvent& event) override;
#endif

 private:
  // Owns us.
  raw_ptr<InspectableWebContents> inspectable_web_contents_;

  raw_ptr<InspectableWebContentsViewDelegate> delegate_ =
      nullptr;  // weak references.

  std::unique_ptr<views::Widget> devtools_window_;
  raw_ptr<views::WebView> devtools_window_web_view_ = nullptr;
  raw_ptr<views::View> contents_web_view_ = nullptr;
  raw_ptr<views::WebView> devtools_web_view_ = nullptr;

  DevToolsContentsResizingStrategy strategy_;
  bool devtools_visible_ = false;
  raw_ptr<views::WidgetDelegate> devtools_window_delegate_ = nullptr;
  std::u16string title_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_INSPECTABLE_WEB_CONTENTS_VIEW_H_
