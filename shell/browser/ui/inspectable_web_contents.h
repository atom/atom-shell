// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Adam Roben <adam@roben.org>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef ELECTRON_SHELL_BROWSER_UI_INSPECTABLE_WEB_CONTENTS_H_
#define ELECTRON_SHELL_BROWSER_UI_INSPECTABLE_WEB_CONTENTS_H_

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/containers/span.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/devtools/devtools_embedder_message_dispatcher.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "electron/buildflags/buildflags.h"
#include "ui/gfx/geometry/rect.h"

class PrefService;
class PrefRegistrySimple;
struct RegisterOptions;

namespace electron {

class InspectableWebContentsDelegate;
class InspectableWebContentsView;

class InspectableWebContents
    : public content::DevToolsAgentHostClient,
      private content::WebContentsObserver,
      public content::WebContentsDelegate,
      public DevToolsEmbedderMessageDispatcher::Delegate {
 public:
  static void RegisterPrefs(PrefRegistrySimple* pref_registry);

  InspectableWebContents(std::unique_ptr<content::WebContents> web_contents,
                         PrefService* pref_service,
                         bool is_guest);
  ~InspectableWebContents() override;

  // disable copy
  InspectableWebContents(const InspectableWebContents&) = delete;
  InspectableWebContents& operator=(const InspectableWebContents&) = delete;

  InspectableWebContentsView* GetView() const;
  content::WebContents* GetWebContents() const;
  content::WebContents* GetDevToolsWebContents() const;

  void SetDelegate(InspectableWebContentsDelegate* delegate);
  InspectableWebContentsDelegate* GetDelegate() const;
  [[nodiscard]] bool is_guest() const { return is_guest_; }
  void ReleaseWebContents();
  void SetDevToolsWebContents(content::WebContents* devtools);
  void SetDockState(const std::string& state);
  void SetDevToolsTitle(const std::u16string& title);
  void ShowDevTools(bool activate);
  void CloseDevTools();
  bool IsDevToolsViewShowing();
  std::u16string GetDevToolsTitle();
  void AttachTo(scoped_refptr<content::DevToolsAgentHost>);
  void Detach();
  void CallClientFunction(
      const std::string& object_name,
      const std::string& method_name,
      const base::Value arg1 = {},
      const base::Value arg2 = {},
      const base::Value arg3 = {},
      base::OnceCallback<void(base::Value)> cb = base::NullCallback());
  void InspectElement(int x, int y);

  // Return the last position and size of devtools window.
  [[nodiscard]] const gfx::Rect& dev_tools_bounds() const {
    return devtools_bounds_;
  }
  void SaveDevToolsBounds(const gfx::Rect& bounds);

  // Return the last set zoom level of devtools window.
  double GetDevToolsZoomLevel() const;
  void UpdateDevToolsZoomLevel(double level);

 private:
  // DevToolsEmbedderMessageDispatcher::Delegate
  void ActivateWindow() override;
  void CloseWindow() override;
  void LoadCompleted() override;
  void SetInspectedPageBounds(const gfx::Rect& rect) override;
  void InspectElementCompleted() override {}
  void InspectedURLChanged(const std::string& url) override;
  void LoadNetworkResource(DispatchCallback callback,
                           const std::string& url,
                           const std::string& headers,
                           int stream_id) override;
  void SetIsDocked(DispatchCallback callback, bool is_docked) override;
  void OpenInNewTab(const std::string& url) override;
  void OpenSearchResultsInNewTab(const std::string& query) override;
  void ShowItemInFolder(const std::string& file_system_path) override;
  void SaveToFile(const std::string& url,
                  const std::string& content,
                  bool save_as,
                  bool is_base64) override;
  void AppendToFile(const std::string& url,
                    const std::string& content) override;
  void RequestFileSystems() override;
  void AddFileSystem(const std::string& type) override;
  void RemoveFileSystem(const std::string& file_system_path) override;
  void UpgradeDraggedFileSystemPermissions(
      const std::string& file_system_url) override {}
  void IndexPath(int index_request_id,
                 const std::string& file_system_path,
                 const std::string& excluded_folders) override;
  void StopIndexing(int index_request_id) override;
  void SearchInPath(int search_request_id,
                    const std::string& file_system_path,
                    const std::string& query) override;
  void SetWhitelistedShortcuts(const std::string& message) override {}
  void SetEyeDropperActive(bool active) override;
  void ShowCertificateViewer(const std::string& cert_chain) override {}
  void ZoomIn() override;
  void ZoomOut() override;
  void ResetZoom() override;
  void SetDevicesDiscoveryConfig(
      bool discover_usb_devices,
      bool port_forwarding_enabled,
      const std::string& port_forwarding_config,
      bool network_discovery_enabled,
      const std::string& network_discovery_config) override {}
  void SetDevicesUpdatesEnabled(bool enabled) override {}
  void OpenRemotePage(const std::string& browser_id,
                      const std::string& url) override {}
  void OpenNodeFrontend() override {}
  void DispatchProtocolMessageFromDevToolsFrontend(
      const std::string& message) override;
  void RecordCountHistogram(const std::string& name,
                            int sample,
                            int min,
                            int exclusive_max,
                            int buckets) override {}
  void SendJsonRequest(DispatchCallback callback,
                       const std::string& browser_id,
                       const std::string& url) override;
  void RegisterPreference(const std::string& name,
                          const RegisterOptions& options) override {}
  void GetPreferences(DispatchCallback callback) override;
  void GetPreference(DispatchCallback callback,
                     const std::string& name) override;
  void SetPreference(const std::string& name,
                     const std::string& value) override;
  void RemovePreference(const std::string& name) override;
  void ClearPreferences() override;
  void GetSyncInformation(DispatchCallback callback) override;
  void GetHostConfig(DispatchCallback callback) override;
  void ConnectionReady() override {}
  void RegisterExtensionsAPI(const std::string& origin,
                             const std::string& script) override;
  void Reattach(DispatchCallback callback) override;
  void RecordEnumeratedHistogram(const std::string& name,
                                 int sample,
                                 int boundary_value) override {}
  void ReadyForTest() override {}
  void SetOpenNewWindowForPopups(bool value) override {}
  void RecordPerformanceHistogram(const std::string& name,
                                  double duration) override {}
  void RecordUserMetricsAction(const std::string& name) override {}
  void RecordImpression(const ImpressionEvent& event) override {}
  void RecordResize(const ResizeEvent& event) override {}
  void RecordClick(const ClickEvent& event) override {}
  void RecordHover(const HoverEvent& event) override {}
  void RecordDrag(const DragEvent& event) override {}
  void RecordChange(const ChangeEvent& event) override {}
  void RecordKeyDown(const KeyDownEvent& event) override {}
  void ShowSurvey(DispatchCallback callback,
                  const std::string& trigger) override {}
  void CanShowSurvey(DispatchCallback callback,
                     const std::string& trigger) override {}
  void DoAidaConversation(DispatchCallback callback,
                          const std::string& request,
                          int stream_id) override {}
  void RegisterAidaClientEvent(DispatchCallback callback,
                               const std::string& request) override {}

  // content::DevToolsFrontendHostDelegate:
  void HandleMessageFromDevToolsFrontend(base::Value::Dict message);

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override {}

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void WebContentsDestroyed() override;
  void OnWebContentsFocused(
      content::RenderWidgetHost* render_widget_host) override;
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(content::WebContents*,
                           const input::NativeWebKeyboardEvent&) override;
  void CloseContents(content::WebContents* source) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      const blink::mojom::FileChooserParams& params) override;
  void EnumerateDirectory(content::WebContents* source,
                          scoped_refptr<content::FileSelectListener> listener,
                          const base::FilePath& path) override;

  void SendMessageAck(int request_id, const base::Value* arg1);

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  void AddDevToolsExtensionsToClient();
#endif

  gfx::Rect devtools_bounds_;
  bool can_dock_ = true;
  std::string dock_state_;
  std::u16string devtools_title_;
  bool activate_ = true;

  raw_ptr<InspectableWebContentsDelegate> delegate_ =
      nullptr;  // weak references.

  raw_ptr<PrefService> pref_service_;  // weak reference.

  std::unique_ptr<content::WebContents> web_contents_;

  // The default devtools created by this class when we don't have an external
  // one assigned by SetDevToolsWebContents.
  std::unique_ptr<content::WebContents> managed_devtools_web_contents_;
  // The external devtools assigned by SetDevToolsWebContents.
  raw_ptr<content::WebContents> external_devtools_web_contents_ = nullptr;

  bool is_guest_;
  std::unique_ptr<InspectableWebContentsView> view_;

  bool frontend_loaded_ = false;
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  std::unique_ptr<content::DevToolsFrontendHost> frontend_host_;
  std::unique_ptr<DevToolsEmbedderMessageDispatcher>
      embedder_message_dispatcher_;

  class NetworkResourceLoader;
  base::flat_set<std::unique_ptr<NetworkResourceLoader>,
                 base::UniquePtrComparator>
      loaders_;

  // origin -> script
  base::flat_map<std::string, std::string> extensions_api_;

  // Contains the set of synced settings.
  // The DevTools frontend *must* call `Register` for each setting prior to
  // use, which guarantees that this set must not be persisted.
  base::flat_set<std::string> synced_setting_names_;

  base::WeakPtrFactory<InspectableWebContents> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_UI_INSPECTABLE_WEB_CONTENTS_H_
