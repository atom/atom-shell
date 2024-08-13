// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_CONTENTS_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_CONTENTS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/devtools/devtools_file_system_indexer.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"  // nogncheck
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "content/common/frame.mojom.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "electron/buildflags/buildflags.h"
#include "electron/shell/common/api/api.mojom.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "printing/buildflags/buildflags.h"
#include "shell/browser/api/save_page_handler.h"
#include "shell/browser/background_throttling_source.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/browser/extended_web_contents_observer.h"
#include "shell/browser/ui/inspectable_web_contents_delegate.h"
#include "shell/browser/ui/inspectable_web_contents_view_delegate.h"
#include "shell/common/gin_helper/cleaned_up_at_exit.h"
#include "shell/common/gin_helper/constructible.h"
#include "shell/common/gin_helper/pinnable.h"
#include "ui/base/models/image_model.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "shell/browser/printing/print_view_manager_electron.h"
#endif

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "extensions/common/mojom/view_type.mojom.h"

namespace extensions {
class ScriptExecutor;
}
#endif

namespace blink {
struct DeviceEmulationParams;
// enum class PermissionType;
}  // namespace blink

namespace content {
enum class KeyboardEventProcessingResult;
class WebContents;
}  // namespace content

namespace gin {
class Arguments;
}

namespace gin_helper {
class Dictionary;
class ErrorThrower;
template <typename T>
class Promise;
}  // namespace gin_helper

namespace network {
class ResourceRequestBody;
}

namespace print_to_pdf {
enum class PdfPrintResult;
}  // namespace print_to_pdf

namespace ui {
class Cursor;
}

class DevToolsEyeDropper;
class SkRegion;

namespace electron {

class ElectronBrowserContext;
class InspectableWebContents;
class WebContentsZoomController;
class WebViewGuestDelegate;
class WebDialogHelper;
class NativeWindow;
class OffScreenRenderWidgetHostView;
class OffScreenWebContentsView;

namespace api {

class BaseWindow;
class FrameSubscriber;

// Wrapper around the content::WebContents.
class WebContents : public ExclusiveAccessContext,
                    public gin::Wrappable<WebContents>,
                    public gin_helper::EventEmitterMixin<WebContents>,
                    public gin_helper::Constructible<WebContents>,
                    public gin_helper::Pinnable<WebContents>,
                    public gin_helper::CleanedUpAtExit,
                    public content::WebContentsObserver,
                    public content::WebContentsDelegate,
                    private content::RenderWidgetHost::InputEventObserver,
                    public content::JavaScriptDialogManager,
                    public InspectableWebContentsDelegate,
                    public InspectableWebContentsViewDelegate,
                    public BackgroundThrottlingSource {
 public:
  enum class Type {
    kBackgroundPage,  // An extension background page.
    kBrowserWindow,   // Used by BrowserWindow.
    kBrowserView,     // Used by the JS implementation of BrowserView for
                      // backwards compatibility. Otherwise identical to
                      // kBrowserWindow.
    kRemote,          // Thin wrap around an existing WebContents.
    kWebView,         // Used by <webview>.
    kOffScreen,       // Used for offscreen rendering
  };

  // Create a new WebContents and return the V8 wrapper of it.
  static gin::Handle<WebContents> New(v8::Isolate* isolate,
                                      const gin_helper::Dictionary& options);

  // Create a new V8 wrapper for an existing |web_content|.
  //
  // The lifetime of |web_contents| will be managed by this class.
  static gin::Handle<WebContents> CreateAndTake(
      v8::Isolate* isolate,
      std::unique_ptr<content::WebContents> web_contents,
      Type type);

  // Get the api::WebContents associated with |web_contents|. Returns nullptr
  // if there is no associated wrapper.
  static WebContents* From(content::WebContents* web_contents);
  static WebContents* FromID(int32_t id);
  static std::list<WebContents*> GetWebContentsList();

  // Get the V8 wrapper of the |web_contents|, or create one if not existed.
  //
  // The lifetime of |web_contents| is NOT managed by this class, and the type
  // of this wrapper is always REMOTE.
  static gin::Handle<WebContents> FromOrCreate(
      v8::Isolate* isolate,
      content::WebContents* web_contents);

  static gin::Handle<WebContents> CreateFromWebPreferences(
      v8::Isolate* isolate,
      const gin_helper::Dictionary& web_preferences);

  // gin_helper::Constructible
  static void FillObjectTemplate(v8::Isolate*, v8::Local<v8::ObjectTemplate>);
  static const char* GetClassName() { return "WebContents"; }

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  const char* GetTypeName() override;

  void Destroy();
  void Close(std::optional<gin_helper::Dictionary> options);
  base::WeakPtr<WebContents> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // BackgroundThrottlingSource
  bool GetBackgroundThrottling() const override;

  void SetBackgroundThrottling(bool allowed);
  int GetProcessID() const;
  base::ProcessId GetOSProcessID() const;
  [[nodiscard]] Type type() const { return type_; }
  bool Equal(const WebContents* web_contents) const;
  void LoadURL(const GURL& url, const gin_helper::Dictionary& options);
  void Reload();
  void ReloadIgnoringCache();
  void DownloadURL(const GURL& url, gin::Arguments* args);
  GURL GetURL() const;
  std::u16string GetTitle() const;
  bool IsLoading() const;
  bool IsLoadingMainFrame() const;
  bool IsWaitingForResponse() const;
  void Stop();
  bool CanGoBack() const;
  void GoBack();
  bool CanGoForward() const;
  void GoForward();
  bool CanGoToOffset(int offset) const;
  void GoToOffset(int offset);
  bool CanGoToIndex(int index) const;
  void GoToIndex(int index);
  int GetActiveIndex() const;
  content::NavigationEntry* GetNavigationEntryAtIndex(int index) const;
  void ClearHistory();
  int GetHistoryLength() const;
  const std::string GetWebRTCIPHandlingPolicy() const;
  void SetWebRTCIPHandlingPolicy(const std::string& webrtc_ip_handling_policy);
  v8::Local<v8::Value> GetWebRTCUDPPortRange(v8::Isolate* isolate) const;
  void SetWebRTCUDPPortRange(gin::Arguments* args);
  std::string GetMediaSourceID(content::WebContents* request_web_contents);
  bool IsCrashed() const;
  void ForcefullyCrashRenderer();
  void SetUserAgent(const std::string& user_agent);
  std::string GetUserAgent();
  void InsertCSS(const std::string& css);
  v8::Local<v8::Promise> SavePage(const base::FilePath& full_file_path,
                                  const content::SavePageType& save_type);
  void OpenDevTools(gin::Arguments* args);
  void CloseDevTools();
  bool IsDevToolsOpened();
  bool IsDevToolsFocused();
  std::u16string GetDevToolsTitle();
  void SetDevToolsTitle(const std::u16string& title);
  void ToggleDevTools();
  void EnableDeviceEmulation(const blink::DeviceEmulationParams& params);
  void DisableDeviceEmulation();
  void InspectElement(int x, int y);
  void InspectSharedWorker();
  void InspectSharedWorkerById(const std::string& workerId);
  std::vector<scoped_refptr<content::DevToolsAgentHost>> GetAllSharedWorkers();
  void InspectServiceWorker();
  void SetIgnoreMenuShortcuts(bool ignore);
  void SetAudioMuted(bool muted);
  bool IsAudioMuted();
  bool IsCurrentlyAudible();
  void SetEmbedder(const WebContents* embedder);
  void SetDevToolsWebContents(const WebContents* devtools);
  v8::Local<v8::Value> GetNativeView(v8::Isolate* isolate) const;
  bool IsBeingCaptured();
  void HandleNewRenderFrame(content::RenderFrameHost* render_frame_host);

#if BUILDFLAG(ENABLE_PRINTING)
  void OnGetDeviceNameToUse(base::Value::Dict print_settings,
                            printing::CompletionCallback print_callback,
                            // <error, device_name>
                            std::pair<std::string, std::u16string> info);
  void Print(gin::Arguments* args);
  // Print current page as PDF.
  v8::Local<v8::Promise> PrintToPDF(const base::Value& settings);
  void OnPDFCreated(gin_helper::Promise<v8::Local<v8::Value>> promise,
                    print_to_pdf::PdfPrintResult print_result,
                    scoped_refptr<base::RefCountedMemory> data);
#endif

  void SetNextChildWebPreferences(const gin_helper::Dictionary);

  // DevTools workspace api.
  void AddWorkSpace(gin::Arguments* args, const base::FilePath& path);
  void RemoveWorkSpace(gin::Arguments* args, const base::FilePath& path);

  // Editing commands.
  void Undo();
  void Redo();
  void Cut();
  void Copy();
  void CenterSelection();
  void Paste();
  void PasteAndMatchStyle();
  void Delete();
  void SelectAll();
  void Unselect();
  void ScrollToTopOfDocument();
  void ScrollToBottomOfDocument();
  void AdjustSelectionByCharacterOffset(gin::Arguments* args);
  void Replace(const std::u16string& word);
  void ReplaceMisspelling(const std::u16string& word);
  uint32_t FindInPage(gin::Arguments* args);
  void StopFindInPage(content::StopFindAction action);
  void ShowDefinitionForSelection();
  void CopyImageAt(int x, int y);

  // Focus.
  void Focus();
  bool IsFocused() const;

  // Send WebInputEvent to the page.
  void SendInputEvent(v8::Isolate* isolate, v8::Local<v8::Value> input_event);

  // Subscribe to the frame updates.
  void BeginFrameSubscription(gin::Arguments* args);
  void EndFrameSubscription();

  // Dragging native items.
  void StartDrag(const gin_helper::Dictionary& item, gin::Arguments* args);

  // Captures the page with |rect|, |callback| would be called when capturing is
  // done.
  v8::Local<v8::Promise> CapturePage(gin::Arguments* args);

  // Methods for creating <webview>.
  [[nodiscard]] bool is_guest() const { return type_ == Type::kWebView; }
  void AttachToIframe(content::WebContents* embedder_web_contents,
                      int embedder_frame_id);
  void DetachFromOuterFrame();

  // Methods for offscreen rendering
  bool IsOffScreen() const;
  void OnPaint(const gfx::Rect& dirty_rect, const SkBitmap& bitmap);
  void StartPainting();
  void StopPainting();
  bool IsPainting() const;
  void SetFrameRate(int frame_rate);
  int GetFrameRate() const;
  void Invalidate();
  gfx::Size GetSizeForNewRenderView(content::WebContents*) override;

  // Methods for zoom handling.
  void SetZoomLevel(double level);
  double GetZoomLevel() const;
  void SetZoomFactor(gin_helper::ErrorThrower thrower, double factor);
  double GetZoomFactor() const;

  // Callback triggered on permission response.
  void OnEnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame,
      const blink::mojom::FullscreenOptions& options,
      bool allowed);

  // Create window with the given disposition.
  void OnCreateWindow(const GURL& target_url,
                      const content::Referrer& referrer,
                      const std::string& frame_name,
                      WindowOpenDisposition disposition,
                      const std::string& features,
                      const scoped_refptr<network::ResourceRequestBody>& body);

  // Returns the preload script path of current WebContents.
  std::vector<base::FilePath> GetPreloadPaths() const;

  // Returns the web preferences of current WebContents.
  v8::Local<v8::Value> GetLastWebPreferences(v8::Isolate* isolate) const;

  // Returns the owner window.
  v8::Local<v8::Value> GetOwnerBrowserWindow(v8::Isolate* isolate) const;

  // Notifies the web page that there is user interaction.
  void NotifyUserActivation();

  v8::Local<v8::Promise> TakeHeapSnapshot(v8::Isolate* isolate,
                                          const base::FilePath& file_path);
  v8::Local<v8::Promise> GetProcessMemoryInfo(v8::Isolate* isolate);

  // content::WebContentsDelegate:
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override;

  // Properties.
  int32_t ID() const { return id_; }
  v8::Local<v8::Value> Session(v8::Isolate* isolate);
  content::WebContents* HostWebContents() const;
  v8::Local<v8::Value> DevToolsWebContents(v8::Isolate* isolate);
  v8::Local<v8::Value> Debugger(v8::Isolate* isolate);
  content::RenderFrameHost* MainFrame();
  content::RenderFrameHost* Opener();

  WebContentsZoomController* GetZoomController() { return zoom_controller_; }

  void AddObserver(ExtendedWebContentsObserver* obs) {
    observers_.AddObserver(obs);
  }
  void RemoveObserver(ExtendedWebContentsObserver* obs) {
    // Trying to remove from an empty collection leads to an access violation
    if (!observers_.empty())
      observers_.RemoveObserver(obs);
  }

  bool EmitNavigationEvent(const std::string& event,
                           content::NavigationHandle* navigation_handle);

  // this.emit(name, new Event(sender, message), args...);
  template <typename... Args>
  bool EmitWithSender(const std::string_view name,
                      content::RenderFrameHost* frame,
                      electron::mojom::ElectronApiIPC::InvokeCallback callback,
                      Args&&... args) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope handle_scope(isolate);

    gin::Handle<gin_helper::internal::Event> event =
        MakeEventWithSender(isolate, frame, std::move(callback));
    if (event.IsEmpty())
      return false;
    EmitWithoutEvent(name, event, std::forward<Args>(args)...);
    return event->GetDefaultPrevented();
  }

  gin::Handle<gin_helper::internal::Event> MakeEventWithSender(
      v8::Isolate* isolate,
      content::RenderFrameHost* frame,
      electron::mojom::ElectronApiIPC::InvokeCallback callback);

  WebContents* embedder() { return embedder_; }

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions::ScriptExecutor* script_executor() {
    return script_executor_.get();
  }
#endif

  // Set the window as owner window.
  void SetOwnerWindow(NativeWindow* owner_window);
  void SetOwnerWindow(content::WebContents* web_contents,
                      NativeWindow* owner_window);
  void SetOwnerBaseWindow(std::optional<BaseWindow*> owner_window);

  // Returns the WebContents managed by this delegate.
  content::WebContents* GetWebContents() const;

  // Returns the WebContents of devtools.
  content::WebContents* GetDevToolsWebContents() const;

  InspectableWebContents* inspectable_web_contents() const {
    return inspectable_web_contents_.get();
  }

  NativeWindow* owner_window() const { return owner_window_.get(); }

  bool is_html_fullscreen() const { return html_fullscreen_; }

  void set_fullscreen_frame(content::RenderFrameHost* rfh) {
    fullscreen_frame_ = rfh;
  }

  // mojom::ElectronApiIPC
  void Message(bool internal,
               const std::string& channel,
               blink::CloneableMessage arguments,
               content::RenderFrameHost* render_frame_host);
  void Invoke(bool internal,
              const std::string& channel,
              blink::CloneableMessage arguments,
              electron::mojom::ElectronApiIPC::InvokeCallback callback,
              content::RenderFrameHost* render_frame_host);
  void ReceivePostMessage(const std::string& channel,
                          blink::TransferableMessage message,
                          content::RenderFrameHost* render_frame_host);
  void MessageSync(
      bool internal,
      const std::string& channel,
      blink::CloneableMessage arguments,
      electron::mojom::ElectronApiIPC::MessageSyncCallback callback,
      content::RenderFrameHost* render_frame_host);
  void MessageHost(const std::string& channel,
                   blink::CloneableMessage arguments,
                   content::RenderFrameHost* render_frame_host);

  // mojom::ElectronWebContentsUtility
  void OnFirstNonEmptyLayout(content::RenderFrameHost* render_frame_host);
  void SetTemporaryZoomLevel(double level);
  void DoGetZoomLevel(
      electron::mojom::ElectronWebContentsUtility::DoGetZoomLevelCallback
          callback);

  void SetImageAnimationPolicy(const std::string& new_policy);

  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEvent(const blink::WebInputEvent& event) override;

  // content::JavaScriptDialogManager:
  void RunJavaScriptDialog(content::WebContents* web_contents,
                           content::RenderFrameHost* rfh,
                           content::JavaScriptDialogType dialog_type,
                           const std::u16string& message_text,
                           const std::u16string& default_prompt_text,
                           DialogClosedCallback callback,
                           bool* did_suppress_message) override;
  void RunBeforeUnloadDialog(content::WebContents* web_contents,
                             content::RenderFrameHost* rfh,
                             bool is_reload,
                             DialogClosedCallback callback) override;
  void CancelDialogs(content::WebContents* web_contents,
                     bool reset_state) override;

  void SetBackgroundColor(std::optional<SkColor> color);

  SkRegion* draggable_region() {
    return force_non_draggable_ ? nullptr : draggable_region_.get();
  }

  void SetForceNonDraggable(bool force_non_draggable) {
    force_non_draggable_ = force_non_draggable;
  }

  // disable copy
  WebContents(const WebContents&) = delete;
  WebContents& operator=(const WebContents&) = delete;

 private:
  // Does not manage lifetime of |web_contents|.
  WebContents(v8::Isolate* isolate, content::WebContents* web_contents);
  // Takes over ownership of |web_contents|.
  WebContents(v8::Isolate* isolate,
              std::unique_ptr<content::WebContents> web_contents,
              Type type);
  // Creates a new content::WebContents.
  WebContents(v8::Isolate* isolate, const gin_helper::Dictionary& options);
  ~WebContents() override;

  // Delete this if garbage collection has not started.
  void DeleteThisIfAlive();

  // Creates a InspectableWebContents object and takes ownership of
  // |web_contents|.
  void InitWithWebContents(std::unique_ptr<content::WebContents> web_contents,
                           ElectronBrowserContext* browser_context,
                           bool is_guest);

  void InitWithSessionAndOptions(
      v8::Isolate* isolate,
      std::unique_ptr<content::WebContents> web_contents,
      gin::Handle<class Session> session,
      const gin_helper::Dictionary& options);

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  void InitWithExtensionView(v8::Isolate* isolate,
                             content::WebContents* web_contents,
                             extensions::mojom::ViewType view_type);
#endif

  // content::WebContentsDelegate:
  bool DidAddMessageToConsole(content::WebContents* source,
                              blink::mojom::ConsoleMessageLevel level,
                              const std::u16string& message,
                              int32_t line_no,
                              const std::u16string& source_id) override;
  bool IsWebContentsCreationOverridden(
      content::SiteInstance* source_site_instance,
      content::mojom::WindowContainerType window_container_type,
      const GURL& opener_url,
      const content::mojom::CreateNewWindowParams& params) override;
  content::WebContents* CreateCustomWebContents(
      content::RenderFrameHost* opener,
      content::SiteInstance* source_site_instance,
      bool is_new_browsing_instance,
      const GURL& opener_url,
      const std::string& frame_name,
      const GURL& target_url,
      const content::StoragePartitionConfig& partition_config,
      content::SessionStorageNamespace* session_storage_namespace) override;
  void WebContentsCreatedWithFullParams(
      content::WebContents* source_contents,
      int opener_render_process_id,
      int opener_render_frame_id,
      const content::mojom::CreateNewWindowParams& params,
      content::WebContents* new_contents) override;
  void AddNewContents(content::WebContents* source,
                      std::unique_ptr<content::WebContents> new_contents,
                      const GURL& target_url,
                      WindowOpenDisposition disposition,
                      const blink::mojom::WindowFeatures& window_features,
                      bool user_gesture,
                      bool* was_blocked) override;
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  void BeforeUnloadFired(content::WebContents* tab,
                         bool proceed,
                         bool* proceed_to_fire_unload) override;
  void SetContentsBounds(content::WebContents* source,
                         const gfx::Rect& pos) override;
  void CloseContents(content::WebContents* source) override;
  void ActivateContents(content::WebContents* contents) override;
  void UpdateTargetURL(content::WebContents* source, const GURL& url) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;
  bool PlatformHandleKeyboardEvent(content::WebContents* source,
                                   const input::NativeWebKeyboardEvent& event);
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  void ContentsZoomChange(bool zoom_in) override;
  void EnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame,
      const blink::mojom::FullscreenOptions& options) override;
  void ExitFullscreenModeForTab(content::WebContents* source) override;
  void RendererUnresponsive(
      content::WebContents* source,
      content::RenderWidgetHost* render_widget_host,
      base::RepeatingClosure hang_monitor_restarter) override;
  void RendererResponsive(
      content::WebContents* source,
      content::RenderWidgetHost* render_widget_host) override;
  void FindReply(content::WebContents* web_contents,
                 int request_id,
                 int number_of_matches,
                 const gfx::Rect& selection_rect,
                 int active_match_ordinal,
                 bool final_update) override;
  void OnRequestPointerLock(content::WebContents* web_contents,
                            bool user_gesture,
                            bool last_unlocked_by_target,
                            bool allowed);
  void RequestPointerLock(content::WebContents* web_contents,
                          bool user_gesture,
                          bool last_unlocked_by_target) override;
  void LostPointerLock() override;
  void OnRequestKeyboardLock(content::WebContents* web_contents,
                             bool esc_key_locked,
                             bool allowed);
  void RequestKeyboardLock(content::WebContents* web_contents,
                           bool esc_key_locked) override;
  void CancelKeyboardLockRequest(content::WebContents* web_contents) override;
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type) override;
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  void OnAudioStateChanged(bool audible) override;
  void UpdatePreferredSize(content::WebContents* web_contents,
                           const gfx::Size& pref_size) override;
  void DraggableRegionsChanged(
      const std::vector<blink::mojom::DraggableRegionPtr>& regions,
      content::WebContents* contents) override;

  // content::WebContentsObserver:
  void BeforeUnloadFired(bool proceed) override;
  void OnBackgroundColorChanged() override;
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void FrameDeleted(int frame_tree_node_id) override;
  void RenderViewDeleted(content::RenderViewHost*) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void DidFailLoad(content::RenderFrameHost* render_frame_host,
                   const GURL& validated_url,
                   int error_code) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidRedirectNavigation(
      content::NavigationHandle* navigation_handle) override;
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void DidUpdateFaviconURL(
      content::RenderFrameHost* render_frame_host,
      const std::vector<blink::mojom::FaviconURLPtr>& urls) override;
  void PluginCrashed(const base::FilePath& plugin_path,
                     base::ProcessId plugin_pid) override;
  void MediaStartedPlaying(const MediaPlayerInfo& video_type,
                           const content::MediaPlayerId& id) override;
  void MediaStoppedPlaying(
      const MediaPlayerInfo& video_type,
      const content::MediaPlayerId& id,
      content::WebContentsObserver::MediaStoppedReason reason) override;
  void DidChangeThemeColor() override;
  void OnCursorChanged(const ui::Cursor& cursor) override;
  void DidAcquireFullscreen(content::RenderFrameHost* rfh) override;
  void OnWebContentsFocused(
      content::RenderWidgetHost* render_widget_host) override;
  void OnWebContentsLostFocus(
      content::RenderWidgetHost* render_widget_host) override;

  // InspectableWebContentsDelegate:
  void DevToolsReloadPage() override;

  // InspectableWebContentsViewDelegate:
  void DevToolsFocused() override;
  void DevToolsOpened() override;
  void DevToolsClosed() override;
  void DevToolsResized() override;

  ElectronBrowserContext* GetBrowserContext() const;

  void OnElectronBrowserConnectionError();

  OffScreenWebContentsView* GetOffScreenWebContentsView() const;
  OffScreenRenderWidgetHostView* GetOffScreenRenderWidgetHostView() const;

  // Called when received a synchronous message from renderer to
  // get the zoom level.
  void OnGetZoomLevel(content::RenderFrameHost* frame_host,
                      IPC::Message* reply_msg);

  void InitZoomController(content::WebContents* web_contents,
                          const gin_helper::Dictionary& options);

  // content::WebContentsDelegate:
  bool CanOverscrollContent() override;
  std::unique_ptr<content::EyeDropper> OpenEyeDropper(
      content::RenderFrameHost* frame,
      content::EyeDropperListener* listener) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      const blink::mojom::FileChooserParams& params) override;
  void EnumerateDirectory(content::WebContents* web_contents,
                          scoped_refptr<content::FileSelectListener> listener,
                          const base::FilePath& path) override;

  // ExclusiveAccessContext:
  Profile* GetProfile() override;
  bool IsFullscreen() const override;
  void EnterFullscreen(const GURL& url,
                       ExclusiveAccessBubbleType bubble_type,
                       const int64_t display_id) override;
  void ExitFullscreen() override {}
  void UpdateExclusiveAccessBubble(
      const ExclusiveAccessBubbleParams& params,
      ExclusiveAccessBubbleHideCallback bubble_first_hide_callback) override {}
  void OnExclusiveAccessUserInput() override {}
  content::WebContents* GetWebContentsForExclusiveAccess() override;
  bool CanUserExitFullscreen() const override;
  bool IsExclusiveAccessBubbleDisplayed() const override;

  // content::WebContentsDelegate
  bool IsFullscreenForTabOrPending(const content::WebContents* source) override;
  content::FullscreenState GetFullscreenState(
      const content::WebContents* web_contents) const override;
  bool TakeFocus(content::WebContents* source, bool reverse) override;
  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* web_contents) override;
  void ExitPictureInPicture() override;

  // InspectableWebContentsDelegate:
  void DevToolsSaveToFile(const std::string& url,
                          const std::string& content,
                          bool save_as,
                          bool is_base64) override;
  void DevToolsAppendToFile(const std::string& url,
                            const std::string& content) override;
  void DevToolsRequestFileSystems() override;
  void DevToolsAddFileSystem(const std::string& type,
                             const base::FilePath& file_system_path) override;
  void DevToolsRemoveFileSystem(
      const base::FilePath& file_system_path) override;
  void DevToolsIndexPath(int request_id,
                         const std::string& file_system_path,
                         const std::string& excluded_folders_message) override;
  void DevToolsOpenInNewTab(const std::string& url) override;
  void DevToolsOpenSearchResultsInNewTab(const std::string& query) override;
  void DevToolsStopIndexing(int request_id) override;
  void DevToolsSearchInPath(int request_id,
                            const std::string& file_system_path,
                            const std::string& query) override;
  void DevToolsSetEyeDropperActive(bool active) override;

  // InspectableWebContentsViewDelegate:
#if defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_MAC)
  ui::ImageModel GetDevToolsWindowIcon() override;
#endif
#if BUILDFLAG(IS_LINUX)
  void GetDevToolsWindowWMClass(std::string* name,
                                std::string* class_name) override;
#endif

  void ColorPickedInEyeDropper(int r, int g, int b, int a);

  // DevTools index event callbacks.
  void OnDevToolsIndexingWorkCalculated(int request_id,
                                        const std::string& file_system_path,
                                        int total_work);
  void OnDevToolsIndexingWorked(int request_id,
                                const std::string& file_system_path,
                                int worked);
  void OnDevToolsIndexingDone(int request_id,
                              const std::string& file_system_path);
  void OnDevToolsSearchCompleted(int request_id,
                                 const std::string& file_system_path,
                                 const std::vector<std::string>& file_paths);

  // Set fullscreen mode triggered by html api.
  void SetHtmlApiFullscreen(bool enter_fullscreen);
  // Update the html fullscreen flag in both browser and renderer.
  void UpdateHtmlApiFullscreen(bool fullscreen);

  v8::Global<v8::Value> session_;
  v8::Global<v8::Value> devtools_web_contents_;
  v8::Global<v8::Value> debugger_;

  std::unique_ptr<WebViewGuestDelegate> guest_delegate_;
  std::unique_ptr<FrameSubscriber> frame_subscriber_;

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  std::unique_ptr<extensions::ScriptExecutor> script_executor_;
#endif

  // The host webcontents that may contain this webcontents.
  RAW_PTR_EXCLUSION WebContents* embedder_ = nullptr;

  // Whether the guest view has been attached.
  bool attached_ = false;

  // The type of current WebContents.
  Type type_ = Type::kBrowserWindow;

  // Weather the guest view should be transparent
  bool guest_transparent_ = true;

  int32_t id_;

  // Request id used for findInPage request.
  uint32_t find_in_page_request_id_ = 0;

  // Whether background throttling is disabled.
  bool background_throttling_ = true;

  // Whether to enable devtools.
  bool enable_devtools_ = true;

  // Observers of this WebContents.
  base::ObserverList<ExtendedWebContentsObserver> observers_;

  v8::Global<v8::Value> pending_child_web_preferences_;

  // The window that this WebContents belongs to.
  base::WeakPtr<NativeWindow> owner_window_;

  bool offscreen_ = false;

  // Whether window is fullscreened by HTML5 api.
  bool html_fullscreen_ = false;

  // Whether window is fullscreened by window api.
  bool native_fullscreen_ = false;

  const scoped_refptr<DevToolsFileSystemIndexer> devtools_file_system_indexer_ =
      base::MakeRefCounted<DevToolsFileSystemIndexer>();

  ExclusiveAccessManager exclusive_access_manager_{this};

  std::unique_ptr<DevToolsEyeDropper> eye_dropper_;

  raw_ptr<ElectronBrowserContext> browser_context_;

  // The stored InspectableWebContents object.
  // Notice that inspectable_web_contents_ must be placed after
  // dialog_manager_, so we can make sure inspectable_web_contents_ is
  // destroyed before dialog_manager_, otherwise a crash would happen.
  std::unique_ptr<InspectableWebContents> inspectable_web_contents_;

  // The zoom controller for this webContents.
  // Note: owned by inspectable_web_contents_, so declare this *after*
  // that field to ensure the dtor destroys them in the right order.
  raw_ptr<WebContentsZoomController> zoom_controller_ = nullptr;

  // Maps url to file path, used by the file requests sent from devtools.
  typedef std::map<std::string, base::FilePath> PathsMap;
  PathsMap saved_files_;

  // Map id to index job, used for file system indexing requests from devtools.
  typedef std::
      map<int, scoped_refptr<DevToolsFileSystemIndexer::FileSystemIndexingJob>>
          DevToolsIndexingJobsMap;
  DevToolsIndexingJobsMap devtools_indexing_jobs_;

  const scoped_refptr<base::SequencedTaskRunner> file_task_runner_ =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});

#if BUILDFLAG(ENABLE_PRINTING)
  const scoped_refptr<base::TaskRunner> print_task_runner_;
#endif

  // Stores the frame thats currently in fullscreen, nullptr if there is none.
  raw_ptr<content::RenderFrameHost> fullscreen_frame_ = nullptr;

  std::unique_ptr<SkRegion> draggable_region_;

  bool force_non_draggable_ = false;

  base::WeakPtrFactory<WebContents> weak_factory_{this};
};

}  // namespace api

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_CONTENTS_H_
