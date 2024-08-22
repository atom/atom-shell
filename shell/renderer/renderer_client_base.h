// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_RENDERER_CLIENT_BASE_H_
#define ELECTRON_SHELL_RENDERER_RENDERER_CLIENT_BASE_H_

#include <memory>
#include <string>

#include "content/public/renderer/content_renderer_client.h"
#include "electron/buildflags/buildflags.h"
// In SHARED_INTERMEDIATE_DIR.
#include "widevine_cdm_version.h"  // NOLINT(build/include_directory)

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
#include "services/service_manager/public/cpp/binder_registry.h"
#include "services/service_manager/public/cpp/local_interface_provider.h"

class SpellCheck;
#endif

namespace blink {
class WebLocalFrame;
}

namespace gin_helper {
class Dictionary;
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
namespace extensions {
class ExtensionsClient;
}
#endif

namespace electron {

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
class ElectronExtensionsRendererClient;
#endif

class RendererClientBase : public content::ContentRendererClient
#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
    ,
                           public service_manager::LocalInterfaceProvider
#endif
{
 public:
  RendererClientBase();
  ~RendererClientBase() override;

  static RendererClientBase* Get();

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  // service_manager::LocalInterfaceProvider implementation.
  void GetInterface(const std::string& name,
                    mojo::ScopedMessagePipeHandle interface_pipe) override;
#endif

  virtual void DidCreateScriptContext(v8::Local<v8::Context> context,
                                      content::RenderFrame* render_frame) = 0;
  virtual void WillReleaseScriptContext(v8::Local<v8::Context> context,
                                        content::RenderFrame* render_frame) = 0;
  virtual void DidClearWindowObject(content::RenderFrame* render_frame);
  virtual void SetupMainWorldOverrides(v8::Local<v8::Context> context,
                                       content::RenderFrame* render_frame);

  std::unique_ptr<blink::WebPrescientNetworking> CreatePrescientNetworking(
      content::RenderFrame* render_frame) override;

  // Get the context that the Electron API is running in.
  v8::Local<v8::Context> GetContext(blink::WebLocalFrame* frame,
                                    v8::Isolate* isolate) const;

  static void AllowGuestViewElementDefinition(
      v8::Isolate* isolate,
      v8::Local<v8::Object> context,
      v8::Local<v8::Function> register_cb);

  bool IsWebViewFrame(v8::Local<v8::Context> context,
                      content::RenderFrame* render_frame) const;

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  SpellCheck* GetSpellCheck() { return spellcheck_.get(); }
#endif

 protected:
  void BindProcess(v8::Isolate* isolate,
                   gin_helper::Dictionary* process,
                   content::RenderFrame* render_frame);

  bool ShouldLoadPreload(v8::Local<v8::Context> context,
                         content::RenderFrame* render_frame) const;

  // content::ContentRendererClient:
  void RenderThreadStarted() override;
  void ExposeInterfacesToBrowser(mojo::BinderMap* binders) override;
  void RenderFrameCreated(content::RenderFrame*) override;
  bool OverrideCreatePlugin(content::RenderFrame* render_frame,
                            const blink::WebPluginParams& params,
                            blink::WebPlugin** plugin) override;
  void DidSetUserAgent(const std::string& user_agent) override;
  bool IsPluginHandledExternally(content::RenderFrame* render_frame,
                                 const blink::WebElement& plugin_element,
                                 const GURL& original_url,
                                 const std::string& mime_type) override;
  v8::Local<v8::Object> GetScriptableObject(
      const blink::WebElement& plugin_element,
      v8::Isolate* isolate) override;

  void RunScriptsAtDocumentStart(content::RenderFrame* render_frame) override;
  void RunScriptsAtDocumentEnd(content::RenderFrame* render_frame) override;
  void RunScriptsAtDocumentIdle(content::RenderFrame* render_frame) override;

  bool AllowScriptExtensionForServiceWorker(
      const url::Origin& script_origin) override;
  void DidInitializeServiceWorkerContextOnWorkerThread(
      blink::WebServiceWorkerContextProxy* context_proxy,
      const GURL& service_worker_scope,
      const GURL& script_url) override;
  void WillEvaluateServiceWorkerOnWorkerThread(
      blink::WebServiceWorkerContextProxy* context_proxy,
      v8::Local<v8::Context> v8_context,
      int64_t service_worker_version_id,
      const GURL& service_worker_scope,
      const GURL& script_url,
      const blink::ServiceWorkerToken& service_worker_token) override;
  void DidStartServiceWorkerContextOnWorkerThread(
      int64_t service_worker_version_id,
      const GURL& service_worker_scope,
      const GURL& script_url) override;
  void WillDestroyServiceWorkerContextOnWorkerThread(
      v8::Local<v8::Context> context,
      int64_t service_worker_version_id,
      const GURL& service_worker_scope,
      const GURL& script_url) override;
  void WebViewCreated(blink::WebView* web_view,
                      bool was_created_by_renderer,
                      const url::Origin* outermost_origin) override;

 protected:
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // app_shell embedders may need custom extensions client interfaces.
  // This class takes ownership of the returned object.
  virtual extensions::ExtensionsClient* CreateExtensionsClient();
#endif

 private:
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  std::unique_ptr<extensions::ExtensionsClient> extensions_client_;
  std::unique_ptr<ElectronExtensionsRendererClient> extensions_renderer_client_;
#endif

  std::string renderer_client_id_;
  // An increasing ID used for identifying an V8 context in this process.
  int64_t next_context_id_ = 0;

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  std::unique_ptr<SpellCheck> spellcheck_;
#endif
};

}  // namespace electron

#endif  // ELECTRON_SHELL_RENDERER_RENDERER_CLIENT_BASE_H_
