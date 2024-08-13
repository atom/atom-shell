// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Adam Roben <adam@roben.org>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "shell/browser/ui/inspectable_web_contents.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include <string_view>
#include "base/base64.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram.h"
#include "base/strings/pattern.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/file_url_loader.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/shared_cors_origin_access_list.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/user_agent.h"
#include "ipc/ipc_channel.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/net/asar/asar_url_loader_factory.h"
#include "shell/browser/protocol_registry.h"
#include "shell/browser/ui/inspectable_web_contents_delegate.h"
#include "shell/browser/ui/inspectable_web_contents_view.h"
#include "shell/browser/ui/inspectable_web_contents_view_delegate.h"
#include "shell/common/application_info.h"
#include "shell/common/platform_util.h"
#include "third_party/blink/public/common/logging/logging_utils.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "v8/include/v8.h"

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "chrome/common/extensions/chrome_manifest_url_handlers.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_process_host.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/permissions/permissions_data.h"
#endif

namespace electron {

namespace {

const double kPresetZoomFactors[] = {0.25, 0.333, 0.5,  0.666, 0.75, 0.9,
                                     1.0,  1.1,   1.25, 1.5,   1.75, 2.0,
                                     2.5,  3.0,   4.0,  5.0};

const char kChromeUIDevToolsURL[] =
    "devtools://devtools/bundled/devtools_app.html?"
    "remoteBase=%s&"
    "can_dock=%s&"
    "toolbarColor=rgba(223,223,223,1)&"
    "textColor=rgba(0,0,0,1)&"
    "experiments=true";
const char kChromeUIDevToolsRemoteFrontendBase[] =
    "https://chrome-devtools-frontend.appspot.com/";
const char kChromeUIDevToolsRemoteFrontendPath[] = "serve_file";

const char kDevToolsBoundsPref[] = "electron.devtools.bounds";
const char kDevToolsZoomPref[] = "electron.devtools.zoom";
const char kDevToolsPreferences[] = "electron.devtools.preferences";

const char kFrontendHostId[] = "id";
const char kFrontendHostMethod[] = "method";
const char kFrontendHostParams[] = "params";
const char kTitleFormat[] = "Developer Tools - %s";

const size_t kMaxMessageChunkSize = IPC::Channel::kMaximumMessageSize / 4;

base::Value::Dict RectToDictionary(const gfx::Rect& bounds) {
  return base::Value::Dict{}
      .Set("x", bounds.x())
      .Set("y", bounds.y())
      .Set("width", bounds.width())
      .Set("height", bounds.height());
}

gfx::Rect DictionaryToRect(const base::Value::Dict& dict) {
  return gfx::Rect{dict.FindInt("x").value_or(0), dict.FindInt("y").value_or(0),
                   dict.FindInt("width").value_or(800),
                   dict.FindInt("height").value_or(600)};
}

bool IsPointInScreen(const gfx::Point& point) {
  return std::ranges::any_of(display::Screen::GetScreen()->GetAllDisplays(),
                             [&point](auto const& display) {
                               return display.bounds().Contains(point);
                             });
}

void SetZoomLevelForWebContents(content::WebContents* web_contents,
                                double level) {
  content::HostZoomMap::SetZoomLevel(web_contents, level);
}

double GetNextZoomLevel(double level, bool out) {
  double factor = blink::ZoomLevelToZoomFactor(level);
  size_t size = std::size(kPresetZoomFactors);
  for (size_t i = 0; i < size; ++i) {
    if (!blink::ZoomValuesEqual(kPresetZoomFactors[i], factor))
      continue;
    if (out && i > 0)
      return blink::ZoomFactorToZoomLevel(kPresetZoomFactors[i - 1]);
    if (!out && i != size - 1)
      return blink::ZoomFactorToZoomLevel(kPresetZoomFactors[i + 1]);
  }
  return level;
}

GURL GetRemoteBaseURL() {
  return GURL(base::StringPrintf("%s%s/%s/",
                                 kChromeUIDevToolsRemoteFrontendBase,
                                 kChromeUIDevToolsRemoteFrontendPath,
                                 content::GetChromiumGitRevision().c_str()));
}

GURL GetDevToolsURL(bool can_dock) {
  auto url_string = base::StringPrintf(kChromeUIDevToolsURL,
                                       GetRemoteBaseURL().spec().c_str(),
                                       can_dock ? "true" : "");
  return GURL(url_string);
}

void OnOpenItemComplete(const base::FilePath& path, const std::string& result) {
  platform_util::ShowItemInFolder(path);
}

constexpr base::TimeDelta kInitialBackoffDelay = base::Milliseconds(250);
constexpr base::TimeDelta kMaxBackoffDelay = base::Seconds(10);

}  // namespace

class InspectableWebContents::NetworkResourceLoader
    : public network::SimpleURLLoaderStreamConsumer {
 public:
  class URLLoaderFactoryHolder {
   public:
    network::mojom::URLLoaderFactory* get() {
      return ptr_.get() ? ptr_.get() : refptr_.get();
    }
    void operator=(std::unique_ptr<network::mojom::URLLoaderFactory>&& ptr) {
      ptr_ = std::move(ptr);
    }
    void operator=(scoped_refptr<network::SharedURLLoaderFactory>&& refptr) {
      refptr_ = std::move(refptr);
    }

   private:
    std::unique_ptr<network::mojom::URLLoaderFactory> ptr_;
    scoped_refptr<network::SharedURLLoaderFactory> refptr_;
  };

  static void Create(int stream_id,
                     InspectableWebContents* bindings,
                     const network::ResourceRequest& resource_request,
                     const net::NetworkTrafficAnnotationTag& traffic_annotation,
                     URLLoaderFactoryHolder url_loader_factory,
                     DispatchCallback callback,
                     base::TimeDelta retry_delay = base::TimeDelta()) {
    bindings->loaders_.insert(
        std::make_unique<InspectableWebContents::NetworkResourceLoader>(
            stream_id, bindings, resource_request, traffic_annotation,
            std::move(url_loader_factory), std::move(callback), retry_delay));
  }

  NetworkResourceLoader(
      int stream_id,
      InspectableWebContents* bindings,
      const network::ResourceRequest& resource_request,
      const net::NetworkTrafficAnnotationTag& traffic_annotation,
      URLLoaderFactoryHolder url_loader_factory,
      DispatchCallback callback,
      base::TimeDelta delay)
      : stream_id_(stream_id),
        bindings_(bindings),
        resource_request_(resource_request),
        traffic_annotation_(traffic_annotation),
        loader_(network::SimpleURLLoader::Create(
            std::make_unique<network::ResourceRequest>(resource_request),
            traffic_annotation)),
        url_loader_factory_(std::move(url_loader_factory)),
        callback_(std::move(callback)),
        retry_delay_(delay) {
    loader_->SetOnResponseStartedCallback(base::BindOnce(
        &NetworkResourceLoader::OnResponseStarted, base::Unretained(this)));
    timer_.Start(FROM_HERE, delay,
                 base::BindRepeating(&NetworkResourceLoader::DownloadAsStream,
                                     base::Unretained(this)));
  }

  NetworkResourceLoader(const NetworkResourceLoader&) = delete;
  NetworkResourceLoader& operator=(const NetworkResourceLoader&) = delete;

 private:
  void DownloadAsStream() {
    loader_->DownloadAsStream(url_loader_factory_.get(), this);
  }

  base::TimeDelta GetNextExponentialBackoffDelay(const base::TimeDelta& delta) {
    if (delta.is_zero()) {
      return kInitialBackoffDelay;
    } else {
      return delta * 1.3;
    }
  }

  void OnResponseStarted(const GURL& final_url,
                         const network::mojom::URLResponseHead& response_head) {
    response_headers_ = response_head.headers;
  }

  // network::SimpleURLLoaderStreamConsumer
  void OnDataReceived(std::string_view chunk,
                      base::OnceClosure resume) override {
    bool encoded = !base::IsStringUTF8(chunk);
    bindings_->CallClientFunction(
        "DevToolsAPI", "streamWrite", base::Value{stream_id_},
        base::Value{encoded ? base::Base64Encode(chunk) : chunk},
        base::Value{encoded});
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
    if (!success && loader_->NetError() == net::ERR_INSUFFICIENT_RESOURCES &&
        retry_delay_ < kMaxBackoffDelay) {
      const base::TimeDelta delay =
          GetNextExponentialBackoffDelay(retry_delay_);
      LOG(WARNING) << "InspectableWebContents::NetworkResourceLoader id = "
                   << stream_id_
                   << " failed with insufficient resources, retrying in "
                   << delay << "." << std::endl;
      NetworkResourceLoader::Create(
          stream_id_, bindings_, resource_request_, traffic_annotation_,
          std::move(url_loader_factory_), std::move(callback_), delay);
    } else {
      base::Value response(base::Value::Type::DICT);
      response.GetDict().Set(
          "statusCode", response_headers_ ? response_headers_->response_code()
                                          : net::HTTP_OK);

      base::Value::Dict headers;
      size_t iterator = 0;
      std::string name;
      std::string value;
      while (response_headers_ &&
             response_headers_->EnumerateHeaderLines(&iterator, &name, &value))
        headers.Set(name, value);

      response.GetDict().Set("headers", std::move(headers));

      std::move(callback_).Run(&response);
    }

    bindings_->loaders_.erase(this);
  }

  void OnRetry(base::OnceClosure start_retry) override {}

  const int stream_id_;
  raw_ptr<InspectableWebContents> const bindings_;
  const network::ResourceRequest resource_request_;
  const net::NetworkTrafficAnnotationTag traffic_annotation_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  URLLoaderFactoryHolder url_loader_factory_;
  DispatchCallback callback_;
  scoped_refptr<net::HttpResponseHeaders> response_headers_;
  base::OneShotTimer timer_;
  base::TimeDelta retry_delay_;
};

// Implemented separately on each platform.
InspectableWebContentsView* CreateInspectableContentsView(
    InspectableWebContents* inspectable_web_contents);

// static
// static
void InspectableWebContents::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kDevToolsBoundsPref,
                                   RectToDictionary(gfx::Rect{0, 0, 800, 600}));
  registry->RegisterDoublePref(kDevToolsZoomPref, 0.);
  registry->RegisterDictionaryPref(kDevToolsPreferences);
}

InspectableWebContents::InspectableWebContents(
    std::unique_ptr<content::WebContents> web_contents,
    PrefService* pref_service,
    bool is_guest)
    : pref_service_(pref_service),
      web_contents_(std::move(web_contents)),
      is_guest_(is_guest),
      view_(CreateInspectableContentsView(this)) {
  const base::Value* bounds_dict =
      &pref_service_->GetValue(kDevToolsBoundsPref);
  if (bounds_dict->is_dict()) {
    devtools_bounds_ = DictionaryToRect(bounds_dict->GetDict());
    // Sometimes the devtools window is out of screen or has too small size.
    if (devtools_bounds_.height() < 100 || devtools_bounds_.width() < 100) {
      devtools_bounds_.set_height(600);
      devtools_bounds_.set_width(800);
    }
    if (!IsPointInScreen(devtools_bounds_.origin())) {
      gfx::Rect display;
      if (!is_guest && web_contents_->GetNativeView()) {
        display = display::Screen::GetScreen()
                      ->GetDisplayNearestView(web_contents_->GetNativeView())
                      .bounds();
      } else {
        display = display::Screen::GetScreen()->GetPrimaryDisplay().bounds();
      }

      devtools_bounds_.set_x(display.x() +
                             (display.width() - devtools_bounds_.width()) / 2);
      devtools_bounds_.set_y(
          display.y() + (display.height() - devtools_bounds_.height()) / 2);
    }
  }
}

InspectableWebContents::~InspectableWebContents() {
  // Unsubscribe from devtools and Clean up resources.
  if (GetDevToolsWebContents())
    WebContentsDestroyed();
  // Let destructor destroy managed_devtools_web_contents_.
}

InspectableWebContentsView* InspectableWebContents::GetView() const {
  return view_.get();
}

content::WebContents* InspectableWebContents::GetWebContents() const {
  return web_contents_.get();
}

content::WebContents* InspectableWebContents::GetDevToolsWebContents() const {
  if (external_devtools_web_contents_)
    return external_devtools_web_contents_;
  else
    return managed_devtools_web_contents_.get();
}

void InspectableWebContents::InspectElement(int x, int y) {
  if (agent_host_)
    agent_host_->InspectElement(web_contents_->GetPrimaryMainFrame(), x, y);
}

void InspectableWebContents::SetDelegate(
    InspectableWebContentsDelegate* delegate) {
  delegate_ = delegate;
}

InspectableWebContentsDelegate* InspectableWebContents::GetDelegate() const {
  return delegate_;
}

void InspectableWebContents::ReleaseWebContents() {
  web_contents_.release();
  WebContentsDestroyed();
  view_.reset();
}

void InspectableWebContents::SetDockState(const std::string& state) {
  if (state == "detach") {
    can_dock_ = false;
  } else {
    can_dock_ = true;
    dock_state_ = state;
  }
}

void InspectableWebContents::SetDevToolsTitle(const std::u16string& title) {
  devtools_title_ = title;
  view_->SetTitle(devtools_title_);
}

void InspectableWebContents::SetDevToolsWebContents(
    content::WebContents* devtools) {
  if (!managed_devtools_web_contents_)
    external_devtools_web_contents_ = devtools;
}

void InspectableWebContents::ShowDevTools(bool activate) {
  if (embedder_message_dispatcher_) {
    if (managed_devtools_web_contents_)
      view_->ShowDevTools(activate);
    return;
  }

  activate_ = activate;

  // Show devtools only after it has done loading, this is to make sure the
  // SetIsDocked is called *BEFORE* ShowDevTools.
  embedder_message_dispatcher_ =
      DevToolsEmbedderMessageDispatcher::CreateForDevToolsFrontend(this);

  if (!external_devtools_web_contents_) {  // no external devtools
    managed_devtools_web_contents_ = content::WebContents::Create(
        content::WebContents::CreateParams(web_contents_->GetBrowserContext()));
    managed_devtools_web_contents_->SetDelegate(this);
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    api::WebContents::FromOrCreate(isolate,
                                   managed_devtools_web_contents_.get());
  }

  Observe(GetDevToolsWebContents());
  AttachTo(content::DevToolsAgentHost::GetOrCreateFor(web_contents_.get()));

  GetDevToolsWebContents()->GetController().LoadURL(
      GetDevToolsURL(can_dock_), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
}

void InspectableWebContents::CloseDevTools() {
  if (GetDevToolsWebContents()) {
    frontend_loaded_ = false;
    if (managed_devtools_web_contents_) {
      view_->CloseDevTools();
      managed_devtools_web_contents_.reset();
    }
    embedder_message_dispatcher_.reset();
    if (!is_guest())
      web_contents_->Focus();
  }
}

bool InspectableWebContents::IsDevToolsViewShowing() {
  return managed_devtools_web_contents_ && view_->IsDevToolsViewShowing();
}

std::u16string InspectableWebContents::GetDevToolsTitle() {
  return view_->GetTitle();
}

void InspectableWebContents::AttachTo(
    scoped_refptr<content::DevToolsAgentHost> host) {
  Detach();
  agent_host_ = std::move(host);
  // We could use ForceAttachClient here if problem arises with
  // devtools multiple session support.
  agent_host_->AttachClient(this);
}

void InspectableWebContents::Detach() {
  if (agent_host_)
    agent_host_->DetachClient(this);
  agent_host_ = nullptr;
}

void InspectableWebContents::Reattach(DispatchCallback callback) {
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_->AttachClient(this);
  }
  std::move(callback).Run(nullptr);
}

void InspectableWebContents::CallClientFunction(
    const std::string& object_name,
    const std::string& method_name,
    base::Value arg1,
    base::Value arg2,
    base::Value arg3,
    base::OnceCallback<void(base::Value)> cb) {
  if (!GetDevToolsWebContents())
    return;

  base::Value::List arguments;
  if (!arg1.is_none()) {
    arguments.Append(std::move(arg1));
    if (!arg2.is_none()) {
      arguments.Append(std::move(arg2));
      if (!arg3.is_none()) {
        arguments.Append(std::move(arg3));
      }
    }
  }

  GetDevToolsWebContents()->GetPrimaryMainFrame()->ExecuteJavaScriptMethod(
      base::ASCIIToUTF16(object_name), base::ASCIIToUTF16(method_name),
      std::move(arguments), std::move(cb));
}

void InspectableWebContents::SaveDevToolsBounds(const gfx::Rect& bounds) {
  pref_service_->Set(kDevToolsBoundsPref,
                     base::Value{RectToDictionary(bounds)});
  devtools_bounds_ = bounds;
}

double InspectableWebContents::GetDevToolsZoomLevel() const {
  return pref_service_->GetDouble(kDevToolsZoomPref);
}

void InspectableWebContents::UpdateDevToolsZoomLevel(double level) {
  pref_service_->SetDouble(kDevToolsZoomPref, level);
}

void InspectableWebContents::ActivateWindow() {
  // Set the zoom level.
  SetZoomLevelForWebContents(GetDevToolsWebContents(), GetDevToolsZoomLevel());
}

void InspectableWebContents::CloseWindow() {
  GetDevToolsWebContents()->DispatchBeforeUnload(false /* auto_cancel */);
}

void InspectableWebContents::LoadCompleted() {
  frontend_loaded_ = true;
  if (managed_devtools_web_contents_)
    view_->ShowDevTools(activate_);

  // If the devtools can dock, "SetIsDocked" will be called by devtools itself.
  if (!can_dock_) {
    SetIsDocked(DispatchCallback(), false);
    if (!devtools_title_.empty()) {
      view_->SetTitle(devtools_title_);
    }
  } else {
    if (dock_state_.empty()) {
      const base::Value::Dict& prefs =
          pref_service_->GetDict(kDevToolsPreferences);
      const std::string* current_dock_state =
          prefs.FindString("currentDockState");
      base::RemoveChars(*current_dock_state, "\"", &dock_state_);
    }
#if BUILDFLAG(IS_WIN)
    auto* api_web_contents = api::WebContents::From(GetWebContents());
    if (api_web_contents) {
      auto* win =
          static_cast<NativeWindowViews*>(api_web_contents->owner_window());
      // When WCO is enabled, undock the devtools if the current dock
      // position overlaps with the position of window controls to avoid
      // broken layout.
      if (win && win->IsWindowControlsOverlayEnabled()) {
        if (IsAppRTL() && dock_state_ == "left") {
          dock_state_ = "undocked";
        } else if (dock_state_ == "right") {
          dock_state_ = "undocked";
        }
      }
    }
#endif
    std::u16string javascript = base::UTF8ToUTF16(
        "EUI.DockController.DockController.instance().setDockSide(\"" +
        dock_state_ + "\");");
    GetDevToolsWebContents()->GetPrimaryMainFrame()->ExecuteJavaScript(
        javascript, base::NullCallback());
  }

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  AddDevToolsExtensionsToClient();
#endif

  if (view_->GetDelegate())
    view_->GetDelegate()->DevToolsOpened();
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
void InspectableWebContents::AddDevToolsExtensionsToClient() {
  // get main browser context
  auto* browser_context = web_contents_->GetBrowserContext();
  const extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(browser_context);
  if (!registry)
    return;

  base::Value::List results;
  for (auto& extension : registry->enabled_extensions()) {
    auto devtools_page_url =
        extensions::chrome_manifest_urls::GetDevToolsPage(extension.get());
    if (devtools_page_url.is_empty())
      continue;

    // Each devtools extension will need to be able to run in the devtools
    // process. Grant the devtools process the ability to request URLs from the
    // extension.
    content::ChildProcessSecurityPolicy::GetInstance()->GrantRequestOrigin(
        web_contents_->GetPrimaryMainFrame()->GetProcess()->GetID(),
        url::Origin::Create(extension->url()));

    base::Value::Dict extension_info;
    extension_info.Set("startPage", devtools_page_url.spec());
    extension_info.Set("name", extension->name());
    extension_info.Set("exposeExperimentalAPIs",
                       extension->permissions_data()->HasAPIPermission(
                           extensions::mojom::APIPermissionID::kExperimental));
    extension_info.Set("allowFileAccess",
                       (extension->creation_flags() &
                        extensions::Extension::ALLOW_FILE_ACCESS) != 0);
    results.Append(std::move(extension_info));
  }

  CallClientFunction("DevToolsAPI", "addExtensions",
                     base::Value(std::move(results)));
}
#endif

void InspectableWebContents::SetInspectedPageBounds(const gfx::Rect& rect) {
  if (managed_devtools_web_contents_)
    view_->SetContentsResizingStrategy(DevToolsContentsResizingStrategy{rect});
}

void InspectableWebContents::InspectedURLChanged(const std::string& url) {
  if (managed_devtools_web_contents_) {
    if (devtools_title_.empty()) {
      view_->SetTitle(
          base::UTF8ToUTF16(base::StringPrintf(kTitleFormat, url.c_str())));
    }
  }
}

void InspectableWebContents::LoadNetworkResource(DispatchCallback callback,
                                                 const std::string& url,
                                                 const std::string& headers,
                                                 int stream_id) {
  GURL gurl(url);
  if (!gurl.is_valid()) {
    base::Value response(base::Value::Type::DICT);
    response.GetDict().Set("statusCode", net::HTTP_NOT_FOUND);
    std::move(callback).Run(&response);
    return;
  }

  // Create traffic annotation tag.
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("devtools_network_resource", R"(
        semantics {
          sender: "Developer Tools"
          description:
            "When user opens Developer Tools, the browser may fetch additional "
            "resources from the network to enrich the debugging experience "
            "(e.g. source map resources)."
          trigger: "User opens Developer Tools to debug a web page."
          data: "Any resources requested by Developer Tools."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "It's not possible to disable this feature from settings."
        })");

  network::ResourceRequest resource_request;
  resource_request.url = gurl;
  resource_request.site_for_cookies = net::SiteForCookies::FromUrl(gurl);
  resource_request.headers.AddHeadersFromString(headers);

  const auto* const protocol_registry = ProtocolRegistry::FromBrowserContext(
      GetDevToolsWebContents()->GetBrowserContext());
  NetworkResourceLoader::URLLoaderFactoryHolder url_loader_factory;
  if (gurl.SchemeIsFile()) {
    mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_remote =
        AsarURLLoaderFactory::Create();
    url_loader_factory = network::SharedURLLoaderFactory::Create(
        std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
            std::move(pending_remote)));
  } else if (const auto* const protocol_handler =
                 protocol_registry->FindRegistered(gurl.scheme())) {
    url_loader_factory = network::SharedURLLoaderFactory::Create(
        std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
            ElectronURLLoaderFactory::Create(protocol_handler->first,
                                             protocol_handler->second)));
  } else {
    auto* partition = GetDevToolsWebContents()
                          ->GetBrowserContext()
                          ->GetDefaultStoragePartition();
    url_loader_factory = partition->GetURLLoaderFactoryForBrowserProcess();
  }

  NetworkResourceLoader::Create(
      stream_id, this, resource_request, traffic_annotation,
      std::move(url_loader_factory), std::move(callback));
}

void InspectableWebContents::SetIsDocked(DispatchCallback callback,
                                         bool docked) {
  if (managed_devtools_web_contents_)
    view_->SetIsDocked(docked, activate_);
  if (!callback.is_null())
    std::move(callback).Run(nullptr);
}

void InspectableWebContents::OpenInNewTab(const std::string& url) {
  if (delegate_)
    delegate_->DevToolsOpenInNewTab(url);
}

void InspectableWebContents::OpenSearchResultsInNewTab(
    const std::string& query) {
  if (delegate_)
    delegate_->DevToolsOpenSearchResultsInNewTab(query);
}

void InspectableWebContents::ShowItemInFolder(
    const std::string& file_system_path) {
  if (file_system_path.empty())
    return;

  base::FilePath path = base::FilePath::FromUTF8Unsafe(file_system_path);
  platform_util::OpenPath(path.DirName(),
                          base::BindOnce(&OnOpenItemComplete, path));
}

void InspectableWebContents::SaveToFile(const std::string& url,
                                        const std::string& content,
                                        bool save_as,
                                        bool is_base64) {
  if (delegate_)
    delegate_->DevToolsSaveToFile(url, content, save_as, is_base64);
}

void InspectableWebContents::AppendToFile(const std::string& url,
                                          const std::string& content) {
  if (delegate_)
    delegate_->DevToolsAppendToFile(url, content);
}

void InspectableWebContents::RequestFileSystems() {
  if (delegate_)
    delegate_->DevToolsRequestFileSystems();
}

void InspectableWebContents::AddFileSystem(const std::string& type) {
  if (delegate_)
    delegate_->DevToolsAddFileSystem(type, base::FilePath());
}

void InspectableWebContents::RemoveFileSystem(
    const std::string& file_system_path) {
  if (delegate_)
    delegate_->DevToolsRemoveFileSystem(
        base::FilePath::FromUTF8Unsafe(file_system_path));
}

void InspectableWebContents::IndexPath(int request_id,
                                       const std::string& file_system_path,
                                       const std::string& excluded_folders) {
  if (delegate_)
    delegate_->DevToolsIndexPath(request_id, file_system_path,
                                 excluded_folders);
}

void InspectableWebContents::StopIndexing(int request_id) {
  if (delegate_)
    delegate_->DevToolsStopIndexing(request_id);
}

void InspectableWebContents::SearchInPath(int request_id,
                                          const std::string& file_system_path,
                                          const std::string& query) {
  if (delegate_)
    delegate_->DevToolsSearchInPath(request_id, file_system_path, query);
}

void InspectableWebContents::SetEyeDropperActive(bool active) {
  if (delegate_)
    delegate_->DevToolsSetEyeDropperActive(active);
}
void InspectableWebContents::ZoomIn() {
  double new_level = GetNextZoomLevel(GetDevToolsZoomLevel(), false);
  SetZoomLevelForWebContents(GetDevToolsWebContents(), new_level);
  UpdateDevToolsZoomLevel(new_level);
}

void InspectableWebContents::ZoomOut() {
  double new_level = GetNextZoomLevel(GetDevToolsZoomLevel(), true);
  SetZoomLevelForWebContents(GetDevToolsWebContents(), new_level);
  UpdateDevToolsZoomLevel(new_level);
}

void InspectableWebContents::ResetZoom() {
  SetZoomLevelForWebContents(GetDevToolsWebContents(), 0.);
  UpdateDevToolsZoomLevel(0.);
}

void InspectableWebContents::DispatchProtocolMessageFromDevToolsFrontend(
    const std::string& message) {
  // If the devtools wants to reload the page, hijack the message and handle it
  // to the delegate.
  if (base::MatchPattern(message,
                         "{\"id\":*,"
                         "\"method\":\"Page.reload\","
                         "\"params\":*}")) {
    if (delegate_)
      delegate_->DevToolsReloadPage();
    return;
  }

  if (agent_host_)
    agent_host_->DispatchProtocolMessage(
        this, base::as_bytes(base::make_span(message)));
}

void InspectableWebContents::SendJsonRequest(DispatchCallback callback,
                                             const std::string& browser_id,
                                             const std::string& url) {
  std::move(callback).Run(nullptr);
}

void InspectableWebContents::GetPreferences(DispatchCallback callback) {
  const base::Value& prefs = pref_service_->GetValue(kDevToolsPreferences);
  std::move(callback).Run(&prefs);
}

void InspectableWebContents::GetPreference(DispatchCallback callback,
                                           const std::string& name) {
  if (auto* pref = pref_service_->GetDict(kDevToolsPreferences).Find(name)) {
    std::move(callback).Run(pref);
    return;
  }

  // Pref wasn't found, return an empty value
  base::Value no_pref;
  std::move(callback).Run(&no_pref);
}

void InspectableWebContents::SetPreference(const std::string& name,
                                           const std::string& value) {
  ScopedDictPrefUpdate update(pref_service_, kDevToolsPreferences);
  update->Set(name, base::Value(value));
}

void InspectableWebContents::RemovePreference(const std::string& name) {
  ScopedDictPrefUpdate update(pref_service_, kDevToolsPreferences);
  update->Remove(name);
}

void InspectableWebContents::ClearPreferences() {
  ScopedDictPrefUpdate unsynced_update(pref_service_, kDevToolsPreferences);
  unsynced_update->clear();
}

void InspectableWebContents::GetSyncInformation(DispatchCallback callback) {
  base::Value result(base::Value::Type::DICT);
  result.GetDict().Set("isSyncActive", false);
  std::move(callback).Run(&result);
}

void InspectableWebContents::GetHostConfig(DispatchCallback callback) {
  base::Value::Dict response_dict;
  base::Value response = base::Value(std::move(response_dict));
  std::move(callback).Run(&response);
}

void InspectableWebContents::RegisterExtensionsAPI(const std::string& origin,
                                                   const std::string& script) {
  extensions_api_[origin + "/"] = script;
}

void InspectableWebContents::HandleMessageFromDevToolsFrontend(
    base::Value::Dict message) {
  // TODO(alexeykuzmin): Should we expect it to exist?
  if (!embedder_message_dispatcher_) {
    return;
  }

  const std::string* method = message.FindString(kFrontendHostMethod);
  base::Value* params = message.Find(kFrontendHostParams);

  if (!method || (params && !params->is_list())) {
    LOG(ERROR) << "Invalid message was sent to embedder: " << message;
    return;
  }

  const base::Value::List no_params;
  const base::Value::List& params_list =
      params != nullptr && params->is_list() ? params->GetList() : no_params;

  const int id = message.FindInt(kFrontendHostId).value_or(0);
  embedder_message_dispatcher_->Dispatch(
      base::BindRepeating(&InspectableWebContents::SendMessageAck,
                          weak_factory_.GetWeakPtr(), id),
      *method, params_list);
}

void InspectableWebContents::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  if (!frontend_loaded_)
    return;

  const std::string_view str_message{
      reinterpret_cast<const char*>(message.data()), message.size()};
  if (str_message.length() < kMaxMessageChunkSize) {
    CallClientFunction("DevToolsAPI", "dispatchMessage",
                       base::Value(std::string(str_message)));
  } else {
    size_t total_size = str_message.length();
    for (size_t pos = 0; pos < str_message.length();
         pos += kMaxMessageChunkSize) {
      std::string_view str_message_chunk =
          str_message.substr(pos, kMaxMessageChunkSize);

      CallClientFunction(
          "DevToolsAPI", "dispatchMessageChunk",
          base::Value(std::string(str_message_chunk)),
          base::Value(base::NumberToString(pos ? 0 : total_size)));
    }
  }
}

void InspectableWebContents::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  if (new_host->GetParent())
    return;
  frontend_host_ = content::DevToolsFrontendHost::Create(
      new_host, base::BindRepeating(
                    &InspectableWebContents::HandleMessageFromDevToolsFrontend,
                    weak_factory_.GetWeakPtr()));
}

void InspectableWebContents::WebContentsDestroyed() {
  if (managed_devtools_web_contents_)
    managed_devtools_web_contents_->SetDelegate(nullptr);

  frontend_loaded_ = false;
  external_devtools_web_contents_ = nullptr;
  Observe(nullptr);
  Detach();
  embedder_message_dispatcher_.reset();
  frontend_host_.reset();

  if (view_ && view_->GetDelegate())
    view_->GetDelegate()->DevToolsClosed();
}

bool InspectableWebContents::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  auto* delegate = web_contents_->GetDelegate();
  return !delegate || delegate->HandleKeyboardEvent(source, event);
}

void InspectableWebContents::CloseContents(content::WebContents* source) {
  // This is where the devtools closes itself (by clicking the x button).
  CloseDevTools();
}

void InspectableWebContents::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    scoped_refptr<content::FileSelectListener> listener,
    const blink::mojom::FileChooserParams& params) {
  auto* delegate = web_contents_->GetDelegate();
  if (delegate)
    delegate->RunFileChooser(render_frame_host, std::move(listener), params);
}

void InspectableWebContents::EnumerateDirectory(
    content::WebContents* source,
    scoped_refptr<content::FileSelectListener> listener,
    const base::FilePath& path) {
  auto* delegate = web_contents_->GetDelegate();
  if (delegate)
    delegate->EnumerateDirectory(source, std::move(listener), path);
}

void InspectableWebContents::OnWebContentsFocused(
    content::RenderWidgetHost* render_widget_host) {
#if defined(TOOLKIT_VIEWS)
  if (view_->GetDelegate())
    view_->GetDelegate()->DevToolsFocused();
#endif
}

void InspectableWebContents::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame()) {
    if (navigation_handle->GetRenderFrameHost() ==
            GetDevToolsWebContents()->GetPrimaryMainFrame() &&
        frontend_host_) {
      return;
    }
    frontend_host_ = content::DevToolsFrontendHost::Create(
        web_contents()->GetPrimaryMainFrame(),
        base::BindRepeating(
            &InspectableWebContents::HandleMessageFromDevToolsFrontend,
            base::Unretained(this)));
    return;
  }
}

void InspectableWebContents::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->GetURL().SchemeIs("chrome-extension") ||
      !navigation_handle->HasCommitted())
    return;
  content::RenderFrameHost* frame = navigation_handle->GetRenderFrameHost();
  auto origin = navigation_handle->GetURL().DeprecatedGetOriginAsURL().spec();
  auto it = extensions_api_.find(origin);
  if (it == extensions_api_.end())
    return;
  // Injected Script from devtools frontend doesn't expose chrome,
  // most likely bug in chromium.
  base::ReplaceFirstSubstringAfterOffset(&it->second, 0, "var chrome",
                                         "var chrome = window.chrome ");
  auto script = base::StringPrintf(
      "%s(\"%s\")", it->second.c_str(),
      base::Uuid::GenerateRandomV4().AsLowercaseString().c_str());
  // Invoking content::DevToolsFrontendHost::SetupExtensionsAPI(frame, script);
  // should be enough, but it seems to be a noop currently.
  frame->ExecuteJavaScriptForTests(base::UTF8ToUTF16(script),
                                   base::NullCallback());
}

void InspectableWebContents::SendMessageAck(int request_id,
                                            const base::Value* arg) {
  if (arg) {
    CallClientFunction("DevToolsAPI", "embedderMessageAck",
                       base::Value(request_id), arg->Clone());
  } else {
    CallClientFunction("DevToolsAPI", "embedderMessageAck",
                       base::Value(request_id));
  }
}

}  // namespace electron
