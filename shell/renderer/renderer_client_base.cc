// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/renderer_client_base.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "components/network_hints/renderer/web_prescient_networking_impl.h"
#include "content/common/buildflags.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_thread.h"
#include "electron/buildflags/buildflags.h"
#include "electron/fuses.h"
#include "printing/buildflags/buildflags.h"
#include "shell/browser/api/electron_api_protocol.h"
#include "shell/common/api/electron_api_native_image.h"
#include "shell/common/color_util.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"
#include "shell/common/node_util.h"
#include "shell/common/options_switches.h"
#include "shell/common/plugin.mojom.h"
#include "shell/common/world_ids.h"
#include "shell/renderer/api/context_bridge/object_cache.h"
#include "shell/renderer/api/electron_api_context_bridge.h"
#include "shell/renderer/browser_exposed_renderer_interfaces.h"
#include "shell/renderer/content_settings_observer.h"
#include "shell/renderer/electron_api_service_impl.h"
#include "shell/renderer/electron_autofill_agent.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/platform/web_runtime_features.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_custom_element.h"  // NOLINT(build/include_alpha)
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_plugin_params.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "third_party/blink/public/web/web_security_policy.h"
#include "third_party/blink/public/web/web_view.h"
#include "third_party/blink/renderer/platform/media/multi_buffer_data_source.h"  // nogncheck
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"  // nogncheck
#include "third_party/widevine/cdm/buildflags.h"

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "components/pdf/renderer/internal_plugin_renderer_helpers.h"
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

#if BUILDFLAG(IS_MAC)
#include "base/strings/sys_string_conversions.h"
#endif

#if BUILDFLAG(IS_WIN)
#include <shlobj.h>
#endif

#if BUILDFLAG(ENABLE_WIDEVINE)
#include "chrome/renderer/media/chrome_key_systems.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
#include "components/spellcheck/renderer/spellcheck.h"
#include "components/spellcheck/renderer/spellcheck_provider.h"
#endif

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "components/pdf/common/constants.h"  // nogncheck
#include "components/pdf/common/pdf_util.h"   // nogncheck
#include "components/pdf/renderer/pdf_internal_plugin_delegate.h"
#include "shell/common/electron_constants.h"
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

#if BUILDFLAG(ENABLE_PLUGINS)
#include "shell/common/plugin_info.h"
#endif  // BUILDFLAG(ENABLE_PLUGINS)

#if BUILDFLAG(ENABLE_PRINTING)
#include "components/printing/renderer/print_render_frame_helper.h"
#include "printing/metafile_agent.h"  // nogncheck
#include "shell/renderer/printing/print_render_frame_helper_delegate.h"
#endif  // BUILDFLAG(ENABLE_PRINTING)

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "base/strings/utf_string_conversions.h"
#include "content/public/common/webplugininfo.h"
#include "extensions/common/constants.h"
#include "extensions/common/extensions_client.h"
#include "extensions/renderer/api/core_extensions_renderer_api_provider.h"
#include "extensions/renderer/dispatcher.h"
#include "extensions/renderer/extension_frame_helper.h"
#include "extensions/renderer/extension_web_view_helper.h"
#include "extensions/renderer/guest_view/mime_handler_view/mime_handler_view_container_manager.h"
#include "shell/common/extensions/electron_extensions_client.h"
#include "shell/renderer/extensions/electron_extensions_renderer_api_provider.h"
#include "shell/renderer/extensions/electron_extensions_renderer_client.h"
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

namespace electron {

content::RenderFrame* GetRenderFrame(v8::Local<v8::Object> value);

namespace {

void SetIsWebView(v8::Isolate* isolate, v8::Local<v8::Object> object) {
  gin_helper::Dictionary dict(isolate, object);
  dict.SetHidden("isWebView", true);
}

std::vector<std::string> ParseSchemesCLISwitch(base::CommandLine* command_line,
                                               const char* switch_name) {
  std::string custom_schemes = command_line->GetSwitchValueASCII(switch_name);
  return base::SplitString(custom_schemes, ",", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

#if BUILDFLAG(ENABLE_PDF_VIEWER)
class ChromePdfInternalPluginDelegate final
    : public pdf::PdfInternalPluginDelegate {
 public:
  ChromePdfInternalPluginDelegate() = default;
  ChromePdfInternalPluginDelegate(const ChromePdfInternalPluginDelegate&) =
      delete;
  ChromePdfInternalPluginDelegate& operator=(
      const ChromePdfInternalPluginDelegate&) = delete;
  ~ChromePdfInternalPluginDelegate() override = default;

  // `pdf::PdfInternalPluginDelegate`:
  bool IsAllowedOrigin(const url::Origin& origin) const override {
    return origin.scheme() == extensions::kExtensionScheme &&
           origin.host() == extension_misc::kPdfExtensionId;
  }
};
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

// static
RendererClientBase* g_renderer_client_base = nullptr;

bool IsDevTools(content::RenderFrame* render_frame) {
  return render_frame->GetWebFrame()->GetDocument().Url().ProtocolIs(
      "devtools");
}

bool IsDevToolsExtension(content::RenderFrame* render_frame) {
  return render_frame->GetWebFrame()->GetDocument().Url().ProtocolIs(
      "chrome-extension");
}

}  // namespace

RendererClientBase::RendererClientBase() {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  // Parse --service-worker-schemes=scheme1,scheme2
  std::vector<std::string> service_worker_schemes_list =
      ParseSchemesCLISwitch(command_line, switches::kServiceWorkerSchemes);
  for (const std::string& scheme : service_worker_schemes_list)
    electron::api::AddServiceWorkerScheme(scheme);
  // Parse --standard-schemes=scheme1,scheme2
  std::vector<std::string> standard_schemes_list =
      ParseSchemesCLISwitch(command_line, switches::kStandardSchemes);
  for (const std::string& scheme : standard_schemes_list)
    url::AddStandardScheme(scheme.c_str(), url::SCHEME_WITH_HOST);
  // Parse --cors-schemes=scheme1,scheme2
  std::vector<std::string> cors_schemes_list =
      ParseSchemesCLISwitch(command_line, switches::kCORSSchemes);
  for (const std::string& scheme : cors_schemes_list)
    url::AddCorsEnabledScheme(scheme.c_str());
  // Parse --streaming-schemes=scheme1,scheme2
  std::vector<std::string> streaming_schemes_list =
      ParseSchemesCLISwitch(command_line, switches::kStreamingSchemes);
  for (const std::string& scheme : streaming_schemes_list)
    blink::AddStreamingScheme(scheme.c_str());
  // Parse --secure-schemes=scheme1,scheme2
  std::vector<std::string> secure_schemes_list =
      ParseSchemesCLISwitch(command_line, switches::kSecureSchemes);
  for (const std::string& scheme : secure_schemes_list)
    url::AddSecureScheme(scheme.data());
  // We rely on the unique process host id which is notified to the
  // renderer process via command line switch from the content layer,
  // if this switch is removed from the content layer for some reason,
  // we should define our own.
  DCHECK(command_line->HasSwitch(::switches::kRendererClientId));
  renderer_client_id_ =
      command_line->GetSwitchValueASCII(::switches::kRendererClientId);

  g_renderer_client_base = this;
}

RendererClientBase::~RendererClientBase() {
  g_renderer_client_base = nullptr;
}

// static
RendererClientBase* RendererClientBase::Get() {
  DCHECK(g_renderer_client_base);
  return g_renderer_client_base;
}

void RendererClientBase::BindProcess(v8::Isolate* isolate,
                                     gin_helper::Dictionary* process,
                                     content::RenderFrame* render_frame) {
  auto context_id = base::StringPrintf(
      "%s-%" PRId64, renderer_client_id_.c_str(), ++next_context_id_);

  process->SetReadOnly("isMainFrame", render_frame->IsMainFrame());
  process->SetReadOnly("contextIsolated",
                       render_frame->GetBlinkPreferences().context_isolation);
  process->SetReadOnly("contextId", context_id);
}

bool RendererClientBase::ShouldLoadPreload(
    v8::Local<v8::Context> context,
    content::RenderFrame* render_frame) const {
  auto prefs = render_frame->GetBlinkPreferences();
  bool is_main_frame = render_frame->IsMainFrame();
  bool is_devtools =
      IsDevTools(render_frame) || IsDevToolsExtension(render_frame);
  bool allow_node_in_sub_frames = prefs.node_integration_in_sub_frames;

  return (is_main_frame || is_devtools || allow_node_in_sub_frames) &&
         !IsWebViewFrame(context, render_frame);
}

void RendererClientBase::RenderThreadStarted() {
  auto* command_line = base::CommandLine::ForCurrentProcess();

  // Enable MessagePort close event by default.
  // The feature got reverted from stable to test in
  // https://chromium-review.googlesource.com/c/chromium/src/+/5276821
  // We had the event supported through patch before upstream support,
  // this is an alternative option than restoring our patch.
  blink::WebRuntimeFeatures::EnableFeatureFromString("MessagePortCloseEvent",
                                                     true);

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  auto* thread = content::RenderThread::Get();

  extensions_client_.reset(CreateExtensionsClient());
  extensions::ExtensionsClient::Set(extensions_client_.get());

  extensions_renderer_client_ =
      std::make_unique<ElectronExtensionsRendererClient>();
  extensions_renderer_client_->AddAPIProvider(
      std::make_unique<extensions::CoreExtensionsRendererAPIProvider>());
  extensions_renderer_client_->AddAPIProvider(
      std::make_unique<ElectronExtensionsRendererAPIProvider>());
  extensions_renderer_client_->RenderThreadStarted();
  extensions::ExtensionsRendererClient::Set(extensions_renderer_client_.get());

  thread->AddObserver(extensions_renderer_client_->GetDispatcher());

  WTF::String extension_scheme(extensions::kExtensionScheme);
  // Extension resources are HTTP-like and safe to expose to the fetch API. The
  // rules for the fetch API are consistent with XHR.
  blink::SchemeRegistry::RegisterURLSchemeAsSupportingFetchAPI(
      extension_scheme);
  // Extension resources, when loaded as the top-level document, should bypass
  // Blink's strict first-party origin checks.
  blink::SchemeRegistry::RegisterURLSchemeAsFirstPartyWhenTopLevel(
      extension_scheme);
  blink::SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(
      extension_scheme);
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  spellcheck_ = std::make_unique<SpellCheck>(this);
#endif

  blink::WebCustomElement::AddEmbedderCustomElementName("webview");
  blink::WebCustomElement::AddEmbedderCustomElementName("browserplugin");

  std::vector<std::string> fetch_enabled_schemes =
      ParseSchemesCLISwitch(command_line, switches::kFetchSchemes);
  for (const std::string& scheme : fetch_enabled_schemes) {
    blink::WebSecurityPolicy::RegisterURLSchemeAsSupportingFetchAPI(
        blink::WebString::FromASCII(scheme));
  }

  std::vector<std::string> service_worker_schemes =
      ParseSchemesCLISwitch(command_line, switches::kServiceWorkerSchemes);
  for (const std::string& scheme : service_worker_schemes)
    blink::WebSecurityPolicy::RegisterURLSchemeAsAllowingServiceWorkers(
        blink::WebString::FromASCII(scheme));

  std::vector<std::string> csp_bypassing_schemes =
      ParseSchemesCLISwitch(command_line, switches::kBypassCSPSchemes);
  for (const std::string& scheme : csp_bypassing_schemes)
    blink::SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(
        WTF::String::FromUTF8(scheme.data(), scheme.length()));

  std::vector<std::string> code_cache_schemes_list =
      ParseSchemesCLISwitch(command_line, switches::kCodeCacheSchemes);
  for (const auto& scheme : code_cache_schemes_list) {
    blink::WebSecurityPolicy::RegisterURLSchemeAsCodeCacheWithHashing(
        blink::WebString::FromASCII(scheme));
  }

  // Allow file scheme to handle service worker by default.
  // FIXME(zcbenz): Can this be moved elsewhere?
  if (electron::fuses::IsGrantFileProtocolExtraPrivilegesEnabled()) {
    blink::WebSecurityPolicy::RegisterURLSchemeAsAllowingServiceWorkers("file");
    blink::SchemeRegistry::RegisterURLSchemeAsSupportingFetchAPI("file");
  }

#if BUILDFLAG(IS_WIN)
  // Set ApplicationUserModelID in renderer process.
  std::wstring app_id =
      command_line->GetSwitchValueNative(switches::kAppUserModelId);
  if (!app_id.empty()) {
    SetCurrentProcessExplicitAppUserModelID(app_id.c_str());
  }
#endif
}

void RendererClientBase::ExposeInterfacesToBrowser(mojo::BinderMap* binders) {
  // NOTE: Do not add binders directly within this method. Instead, modify the
  // definition of |ExposeElectronRendererInterfacesToBrowser()| to ensure
  // security review coverage.
  ExposeElectronRendererInterfacesToBrowser(this, binders);
}

void RendererClientBase::RenderFrameCreated(
    content::RenderFrame* render_frame) {
#if defined(TOOLKIT_VIEWS)
  new AutofillAgent(render_frame,
                    render_frame->GetAssociatedInterfaceRegistry());
#endif
  new ContentSettingsObserver(render_frame);
#if BUILDFLAG(ENABLE_PRINTING)
  new printing::PrintRenderFrameHelper(
      render_frame,
      std::make_unique<electron::PrintRenderFrameHelperDelegate>());
#endif

  // Note: ElectronApiServiceImpl has to be created now to capture the
  // DidCreateDocumentElement event.
  new ElectronApiServiceImpl(render_frame, this);

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  auto* dispatcher = extensions_renderer_client_->GetDispatcher();
  // ExtensionFrameHelper destroys itself when the RenderFrame is destroyed.
  new extensions::ExtensionFrameHelper(render_frame, dispatcher);

  dispatcher->OnRenderFrameCreated(render_frame);

  render_frame->GetAssociatedInterfaceRegistry()
      ->AddInterface<extensions::mojom::MimeHandlerViewContainerManager>(
          base::BindRepeating(
              &extensions::MimeHandlerViewContainerManager::BindReceiver,
              base::Unretained(render_frame)));
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  if (render_frame->GetBlinkPreferences().enable_spellcheck) {
    new SpellCheckProvider(render_frame, spellcheck_.get());
  }
#endif
}

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
void RendererClientBase::GetInterface(
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle interface_pipe) {
  // TODO(crbug.com/977637): Get rid of the use of this implementation of
  // |service_manager::LocalInterfaceProvider|. This was done only to avoid
  // churning spellcheck code while eliminating the "chrome" and
  // "chrome_renderer" services. Spellcheck is (and should remain) the only
  // consumer of this implementation.
  content::RenderThread::Get()->BindHostReceiver(
      mojo::GenericPendingReceiver(interface_name, std::move(interface_pipe)));
}
#endif

void RendererClientBase::DidClearWindowObject(
    content::RenderFrame* render_frame) {
  // Make sure every page will get a script context created.
  render_frame->GetWebFrame()->ExecuteScript(blink::WebScriptSource("void 0"));
}

bool RendererClientBase::OverrideCreatePlugin(
    content::RenderFrame* render_frame,
    const blink::WebPluginParams& params,
    blink::WebPlugin** plugin) {
#if BUILDFLAG(ENABLE_PDF_VIEWER)
  if (params.mime_type.Utf8() == pdf::kInternalPluginMimeType) {
    *plugin = pdf::CreateInternalPlugin(
        std::move(params), render_frame,
        std::make_unique<ChromePdfInternalPluginDelegate>());
    return true;
  }
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

  if (params.mime_type.Utf8() == content::kBrowserPluginMimeType ||
      render_frame->GetBlinkPreferences().enable_plugins)
    return false;

  *plugin = nullptr;
  return true;
}

void RendererClientBase::DidSetUserAgent(const std::string& user_agent) {
#if BUILDFLAG(ENABLE_PRINTING)
  printing::SetAgent(user_agent);
#endif
}

bool RendererClientBase::IsPluginHandledExternally(
    content::RenderFrame* render_frame,
    const blink::WebElement& plugin_element,
    const GURL& original_url,
    const std::string& mime_type) {
#if BUILDFLAG(ENABLE_PDF_VIEWER)
  DCHECK(plugin_element.HasHTMLTagName("object") ||
         plugin_element.HasHTMLTagName("embed"));

  mojo::AssociatedRemote<mojom::ElectronPluginInfoHost> plugin_info_host;
  render_frame->GetRemoteAssociatedInterfaces()->GetInterface(
      &plugin_info_host);
  mojom::PluginInfoPtr plugin_info = mojom::PluginInfo::New();
  plugin_info_host->GetPluginInfo(
      original_url, render_frame->GetWebFrame()->Top()->GetSecurityOrigin(),
      mime_type, &plugin_info);

  if (plugin_info->actual_mime_type == pdf::kInternalPluginMimeType) {
    if (IsPdfInternalPluginAllowedOrigin(
            render_frame->GetWebFrame()->GetSecurityOrigin())) {
      return true;
    }
  }

  return extensions::MimeHandlerViewContainerManager::Get(
             content::RenderFrame::FromWebFrame(
                 plugin_element.GetDocument().GetFrame()),
             true /* create_if_does_not_exist */)
      ->CreateFrameContainer(plugin_element, original_url,
                             plugin_info->actual_mime_type,
                             plugin_info->plugin);
#else
  return false;
#endif
}

v8::Local<v8::Object> RendererClientBase::GetScriptableObject(
    const blink::WebElement& plugin_element,
    v8::Isolate* isolate) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // If there is a MimeHandlerView that can provide the scriptable object then
  // MaybeCreateMimeHandlerView must have been called before and a container
  // manager should exist.
  auto* container_manager = extensions::MimeHandlerViewContainerManager::Get(
      content::RenderFrame::FromWebFrame(
          plugin_element.GetDocument().GetFrame()),
      false /* create_if_does_not_exist */);
  if (container_manager)
    return container_manager->GetScriptableObject(plugin_element, isolate);
#endif
  return v8::Local<v8::Object>();
}

std::unique_ptr<blink::WebPrescientNetworking>
RendererClientBase::CreatePrescientNetworking(
    content::RenderFrame* render_frame) {
  return std::make_unique<network_hints::WebPrescientNetworkingImpl>(
      render_frame);
}

void RendererClientBase::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_.get()->RunScriptsAtDocumentStart(render_frame);
#endif
}

void RendererClientBase::RunScriptsAtDocumentIdle(
    content::RenderFrame* render_frame) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_.get()->RunScriptsAtDocumentIdle(render_frame);
#endif
}

void RendererClientBase::RunScriptsAtDocumentEnd(
    content::RenderFrame* render_frame) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_.get()->RunScriptsAtDocumentEnd(render_frame);
#endif
}

bool RendererClientBase::AllowScriptExtensionForServiceWorker(
    const url::Origin& script_origin) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  return script_origin.scheme() == extensions::kExtensionScheme;
#else
  return false;
#endif
}

void RendererClientBase::DidInitializeServiceWorkerContextOnWorkerThread(
    blink::WebServiceWorkerContextProxy* context_proxy,
    const GURL& service_worker_scope,
    const GURL& script_url) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_->GetDispatcher()
      ->DidInitializeServiceWorkerContextOnWorkerThread(
          context_proxy, service_worker_scope, script_url);
#endif
}

void RendererClientBase::WillEvaluateServiceWorkerOnWorkerThread(
    blink::WebServiceWorkerContextProxy* context_proxy,
    v8::Local<v8::Context> v8_context,
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_->GetDispatcher()
      ->WillEvaluateServiceWorkerOnWorkerThread(
          context_proxy, v8_context, service_worker_version_id,
          service_worker_scope, script_url);
#endif
}

void RendererClientBase::DidStartServiceWorkerContextOnWorkerThread(
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_->GetDispatcher()
      ->DidStartServiceWorkerContextOnWorkerThread(
          service_worker_version_id, service_worker_scope, script_url);
#endif
}

void RendererClientBase::WillDestroyServiceWorkerContextOnWorkerThread(
    v8::Local<v8::Context> context,
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_renderer_client_->GetDispatcher()
      ->WillDestroyServiceWorkerContextOnWorkerThread(
          context, service_worker_version_id, service_worker_scope, script_url);
#endif
}

void RendererClientBase::WebViewCreated(blink::WebView* web_view,
                                        bool was_created_by_renderer,
                                        const url::Origin* outermost_origin) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  new extensions::ExtensionWebViewHelper(web_view, outermost_origin);
#endif
}

v8::Local<v8::Context> RendererClientBase::GetContext(
    blink::WebLocalFrame* frame,
    v8::Isolate* isolate) const {
  auto* render_frame = content::RenderFrame::FromWebFrame(frame);
  DCHECK(render_frame);
  if (render_frame && render_frame->GetBlinkPreferences().context_isolation)
    return frame->GetScriptContextFromWorldId(isolate,
                                              WorldIDs::ISOLATED_WORLD_ID);
  else
    return frame->MainWorldScriptContext();
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
extensions::ExtensionsClient* RendererClientBase::CreateExtensionsClient() {
  return new ElectronExtensionsClient;
}
#endif

bool RendererClientBase::IsWebViewFrame(
    v8::Local<v8::Context> context,
    content::RenderFrame* render_frame) const {
  auto* isolate = context->GetIsolate();

  if (render_frame->IsMainFrame())
    return false;

  gin::Dictionary window_dict(
      isolate, GetContext(render_frame->GetWebFrame(), isolate)->Global());

  v8::Local<v8::Object> frame_element;
  if (!window_dict.Get("frameElement", &frame_element))
    return false;

  gin_helper::Dictionary frame_element_dict(isolate, frame_element);

  bool is_webview = false;
  return frame_element_dict.GetHidden("isWebView", &is_webview) && is_webview;
}

void RendererClientBase::SetupMainWorldOverrides(
    v8::Local<v8::Context> context,
    content::RenderFrame* render_frame) {
  auto prefs = render_frame->GetBlinkPreferences();
  // We only need to run the isolated bundle if webview is enabled
  if (!prefs.webview_tag)
    return;

  // Setup window overrides in the main world context
  // Wrap the bundle into a function that receives the isolatedApi as
  // an argument.
  auto* isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);

  auto isolated_api = gin_helper::Dictionary::CreateEmpty(isolate);
  isolated_api.SetMethod("allowGuestViewElementDefinition",
                         &AllowGuestViewElementDefinition);
  isolated_api.SetMethod("setIsWebView", &SetIsWebView);

  auto source_context = GetContext(render_frame->GetWebFrame(), isolate);
  gin_helper::Dictionary global(isolate, source_context->Global());

  v8::Local<v8::Value> guest_view_internal;
  if (global.GetHidden("guestViewInternal", &guest_view_internal)) {
    api::context_bridge::ObjectCache object_cache;
    auto result = api::PassValueToOtherContext(
        source_context, context, guest_view_internal, source_context->Global(),
        &object_cache, false, 0, api::BridgeErrorTarget::kSource);
    if (!result.IsEmpty()) {
      isolated_api.Set("guestViewInternal", result.ToLocalChecked());
    }
  }

  std::vector<v8::Local<v8::String>> isolated_bundle_params = {
      node::FIXED_ONE_BYTE_STRING(isolate, "isolatedApi")};

  std::vector<v8::Local<v8::Value>> isolated_bundle_args = {
      isolated_api.GetHandle()};

  util::CompileAndCall(context, "electron/js2c/isolated_bundle",
                       &isolated_bundle_params, &isolated_bundle_args);
}

// static
void RendererClientBase::AllowGuestViewElementDefinition(
    v8::Isolate* isolate,
    v8::Local<v8::Object> context,
    v8::Local<v8::Function> register_cb) {
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context->GetCreationContextChecked());
  blink::WebCustomElement::EmbedderNamesAllowedScope embedder_names_scope;

  content::RenderFrame* render_frame = GetRenderFrame(context);
  if (!render_frame)
    return;

  render_frame->GetWebFrame()->RequestExecuteV8Function(
      context->GetCreationContextChecked(), register_cb, v8::Null(isolate), 0,
      nullptr, base::NullCallback());
}

}  // namespace electron
