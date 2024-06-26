// Copyright (c) 2022 Salesforce, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/inspectable_web_contents_view.h"

#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "ui/base/cocoa/base_view.h"

namespace electron {

bool InspectableWebContentsView::OnMousePressed(const ui::MouseEvent& event) {
  DCHECK(event.IsRightMouseButton() ||
         (event.IsLeftMouseButton() && event.IsControlDown()));
  content::WebContents* contents = inspectable_web_contents()->GetWebContents();
  electron::api::WebContents* api_contents =
      electron::api::WebContents::From(contents);
  if (api_contents) {
    // Temporarily pretend that the WebContents is fully non-draggable while we
    // re-send the mouse event. This allows the re-dispatched event to "land"
    // on the WebContents, instead of "falling through" back to the window.
    api_contents->SetForceNonDraggable(true);
    BaseView* contentsView = (BaseView*)contents->GetRenderWidgetHostView()
                                 ->GetNativeView()
                                 .GetNativeNSView();
    [contentsView mouseEvent:event.native_event().Get()];
    api_contents->SetForceNonDraggable(false);
  }
  return true;
}

}  // namespace electron
