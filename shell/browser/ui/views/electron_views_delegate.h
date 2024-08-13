// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_UI_VIEWS_ELECTRON_VIEWS_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_UI_VIEWS_ELECTRON_VIEWS_DELEGATE_H_

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "ui/views/views_delegate.h"

namespace electron {

class ViewsDelegate : public views::ViewsDelegate {
 public:
  ViewsDelegate();
  ~ViewsDelegate() override;

  // disable copy
  ViewsDelegate(const ViewsDelegate&) = delete;
  ViewsDelegate& operator=(const ViewsDelegate&) = delete;

 protected:
  // views::ViewsDelegate:
  void SaveWindowPlacement(const views::Widget* window,
                           const std::string& window_name,
                           const gfx::Rect& bounds,
                           ui::WindowShowState show_state) override;
  bool GetSavedWindowPlacement(const views::Widget* widget,
                               const std::string& window_name,
                               gfx::Rect* bounds,
                               ui::WindowShowState* show_state) const override;
  void NotifyMenuItemFocused(const std::u16string& menu_name,
                             const std::u16string& menu_item_name,
                             int item_index,
                             int item_count,
                             bool has_submenu) override;

#if BUILDFLAG(IS_WIN)
  HICON GetDefaultWindowIcon() const override;
  HICON GetSmallWindowIcon() const override;
  int GetAppbarAutohideEdges(HMONITOR monitor,
                             base::OnceClosure callback) override;
#elif BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS)
  gfx::ImageSkia* GetDefaultWindowIcon() const override;
#endif
  std::unique_ptr<views::NonClientFrameView> CreateDefaultNonClientFrameView(
      views::Widget* widget) override;
  void AddRef() override {}
  void ReleaseRef() override {}
  void OnBeforeWidgetInit(
      views::Widget::InitParams* params,
      views::internal::NativeWidgetDelegate* delegate) override;
  bool WindowManagerProvidesTitleBar(bool maximized) override;

 private:
#if BUILDFLAG(IS_WIN)
  // Callback on main thread with the edges. |returned_edges| is the value that
  // was returned from the call to GetAutohideEdges() that initiated the lookup.
  void OnGotAppbarAutohideEdges(base::OnceClosure callback,
                                HMONITOR monitor,
                                int returned_edges,
                                int edges);

  base::flat_map<HMONITOR, int> appbar_autohide_edge_map_;
  // If true we're in the process of notifying a callback from
  // GetAutohideEdges().start a new query.
  bool in_autohide_edges_callback_ = false;

  base::WeakPtrFactory<ViewsDelegate> weak_factory_{this};
#endif
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_VIEWS_ELECTRON_VIEWS_DELEGATE_H_
