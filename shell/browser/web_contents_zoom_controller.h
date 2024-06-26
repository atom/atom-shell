// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_WEB_CONTENTS_ZOOM_CONTROLLER_H_
#define ELECTRON_SHELL_BROWSER_WEB_CONTENTS_ZOOM_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace electron {

class WebContentsZoomObserver;

// Manages the zoom changes of WebContents.
class WebContentsZoomController
    : private content::WebContentsObserver,
      public content::WebContentsUserData<WebContentsZoomController> {
 public:
  // Defines how zoom changes are handled.
  enum ZoomMode {
    // Results in default zoom behavior, i.e. zoom changes are handled
    // automatically and on a per-origin basis, meaning that other tabs
    // navigated to the same origin will also zoom.
    ZOOM_MODE_DEFAULT,
    // Results in zoom changes being handled automatically, but on a per-tab
    // basis. Tabs in this zoom mode will not be affected by zoom changes in
    // other tabs, and vice versa.
    ZOOM_MODE_ISOLATED,
    // Overrides the automatic handling of zoom changes. The |onZoomChange|
    // event will still be dispatched, but the page will not actually be zoomed.
    // These zoom changes can be handled manually by listening for the
    // |onZoomChange| event. Zooming in this mode is also on a per-tab basis.
    ZOOM_MODE_MANUAL,
    // Disables all zooming in this tab. The tab will revert to the default
    // zoom level, and all attempted zoom changes will be ignored.
    ZOOM_MODE_DISABLED,
  };

  struct ZoomChangedEventData {
    ZoomChangedEventData(content::WebContents* web_contents,
                         double old_zoom_level,
                         double new_zoom_level,
                         bool temporary,
                         WebContentsZoomController::ZoomMode zoom_mode)
        : web_contents(web_contents),
          old_zoom_level(old_zoom_level),
          new_zoom_level(new_zoom_level),
          temporary(temporary),
          zoom_mode(zoom_mode) {}
    raw_ptr<content::WebContents> web_contents;
    double old_zoom_level;
    double new_zoom_level;
    bool temporary;
    WebContentsZoomController::ZoomMode zoom_mode;
  };

  explicit WebContentsZoomController(content::WebContents* web_contents);
  ~WebContentsZoomController() override;

  // disable copy
  WebContentsZoomController(const WebContentsZoomController&) = delete;
  WebContentsZoomController& operator=(const WebContentsZoomController&) =
      delete;

  void AddObserver(WebContentsZoomObserver* observer);
  void RemoveObserver(WebContentsZoomObserver* observer);

  void SetEmbedderZoomController(WebContentsZoomController* controller);

  // Gets the current zoom level by querying HostZoomMap (if not in manual zoom
  // mode) or from the ZoomController local value otherwise.
  double GetZoomLevel() const;

  // Sets the zoom level through HostZoomMap.
  // Returns true on success.
  bool SetZoomLevel(double zoom_level);

  void SetDefaultZoomFactor(double factor);
  double default_zoom_factor() { return default_zoom_factor_; }

  // Sets the temporary zoom level through HostZoomMap.
  void SetTemporaryZoomLevel(double level);
  bool UsesTemporaryZoomLevel();

  // Sets the zoom mode, which defines zoom behavior (see enum ZoomMode).
  void SetZoomMode(ZoomMode zoom_mode);

  ZoomMode zoom_mode() const { return zoom_mode_; }

  // Convenience method to get default zoom level. Implemented here for
  // inlining.
  double GetDefaultZoomLevel() const {
    return content::HostZoomMap::GetForWebContents(web_contents())
        ->GetDefaultZoomLevel();
  }

 protected:
  // content::WebContentsObserver overrides:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;

 private:
  friend class content::WebContentsUserData<WebContentsZoomController>;

  void ResetZoomModeOnNavigationIfNeeded(const GURL& url);
  void SetZoomFactorOnNavigationIfNeeded(const GURL& url);
  void OnZoomLevelChanged(const content::HostZoomMap::ZoomLevelChange& change);

  // Updates the zoom icon and zoom percentage based on current values and
  // notifies the observer if changes have occurred. |host| may be empty,
  // meaning the change should apply to ~all sites. If it is not empty, the
  // change only affects sites with the given host.
  void UpdateState(const std::string& host);

  // The current zoom mode.
  ZoomMode zoom_mode_ = ZOOM_MODE_DEFAULT;

  // The current zoom level.
  double zoom_level_;

  // The current default zoom factor.
  double default_zoom_factor_;

  int old_process_id_ = -1;
  int old_view_id_ = -1;

  std::unique_ptr<ZoomChangedEventData> event_data_;

  raw_ptr<WebContentsZoomController> embedder_zoom_controller_ = nullptr;

  // Observer receiving notifications on state changes.
  base::ObserverList<WebContentsZoomObserver> observers_;

  // Keep track of the HostZoomMap we're currently subscribed to.
  raw_ptr<content::HostZoomMap> host_zoom_map_;

  base::CallbackListSubscription zoom_subscription_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_WEB_CONTENTS_ZOOM_CONTROLLER_H_
