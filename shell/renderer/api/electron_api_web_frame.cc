// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/strings/utf_string_conversions.h"
#include "components/spellcheck/renderer/spellcheck.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_visitor.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "gin/wrappable.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "shell/common/api/api.mojom.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/function_template_extensions.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "shell/renderer/api/context_bridge/object_cache.h"
#include "shell/renderer/api/electron_api_context_bridge.h"
#include "shell/renderer/api/electron_api_spell_check_client.h"
#include "shell/renderer/renderer_client_base.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/common/web_cache/web_cache_resource_type_stats.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/platform/web_cache.h"
#include "third_party/blink/public/platform/web_isolated_world_info.h"
#include "third_party/blink/public/web/web_custom_element.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_input_method_controller.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_execution_callback.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "third_party/blink/public/web/web_view.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"  // nogncheck
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"  // nogncheck
#include "third_party/blink/renderer/platform/bindings/dom_wrapper_world.h"  // nogncheck
#include "ui/base/ime/ime_text_span.h"
#include "url/url_util.h"

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
#include "components/spellcheck/renderer/spellcheck.h"
#include "components/spellcheck/renderer/spellcheck_provider.h"
#endif

namespace gin {

template <>
struct Converter<blink::WebCssOrigin> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     blink::WebCssOrigin* out) {
    std::string css_origin;
    if (!ConvertFromV8(isolate, val, &css_origin))
      return false;
    if (css_origin == "user") {
      *out = blink::WebCssOrigin::kUser;
    } else if (css_origin == "author") {
      *out = blink::WebCssOrigin::kAuthor;
    } else {
      return false;
    }
    return true;
  }
};

}  // namespace gin

namespace electron {

content::RenderFrame* GetRenderFrame(v8::Local<v8::Object> value) {
  v8::Local<v8::Context> context = value->GetCreationContextChecked();
  if (context.IsEmpty())
    return nullptr;
  blink::WebLocalFrame* frame = blink::WebLocalFrame::FrameForContext(context);
  if (!frame)
    return nullptr;
  return content::RenderFrame::FromWebFrame(frame);
}

namespace api {

namespace {

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)

bool SpellCheckWord(content::RenderFrame* render_frame,
                    const std::string& word,
                    std::vector<std::u16string>* optional_suggestions) {
  size_t start;
  size_t length;

  RendererClientBase* client = RendererClientBase::Get();

  mojo::Remote<spellcheck::mojom::SpellCheckHost> spellcheck_host;
  render_frame->GetBrowserInterfaceBroker()->GetInterface(
      spellcheck_host.BindNewPipeAndPassReceiver());
  if (!spellcheck_host.is_bound())
    return false;

  std::u16string w = base::UTF8ToUTF16(word);
  return client->GetSpellCheck()->SpellCheckWord(
      w, *spellcheck_host.get(), &start, &length, optional_suggestions);
}

#endif

class ScriptExecutionCallback {
 public:
  // for compatibility with the older version of this, error is after result
  using CompletionCallback =
      base::OnceCallback<void(const v8::Local<v8::Value>& result,
                              const v8::Local<v8::Value>& error)>;

  explicit ScriptExecutionCallback(
      gin_helper::Promise<v8::Local<v8::Value>> promise,
      CompletionCallback callback)
      : promise_(std::move(promise)), callback_(std::move(callback)) {}

  ~ScriptExecutionCallback() = default;

  // disable copy
  ScriptExecutionCallback(const ScriptExecutionCallback&) = delete;
  ScriptExecutionCallback& operator=(const ScriptExecutionCallback&) = delete;

  void CopyResultToCallingContextAndFinalize(
      v8::Isolate* isolate,
      const v8::Local<v8::Object>& result) {
    v8::MaybeLocal<v8::Value> maybe_result;
    bool success = true;
    std::string error_message =
        "An unknown exception occurred while getting the result of the script";
    {
      v8::TryCatch try_catch(isolate);
      context_bridge::ObjectCache object_cache;
      v8::Local<v8::Context> source_context =
          result->GetCreationContextChecked();
      maybe_result =
          PassValueToOtherContext(source_context, promise_.GetContext(), result,
                                  source_context->Global(), &object_cache,
                                  false, 0, BridgeErrorTarget::kSource);
      if (maybe_result.IsEmpty() || try_catch.HasCaught()) {
        success = false;
      }
      if (try_catch.HasCaught()) {
        auto message = try_catch.Message();

        if (!message.IsEmpty()) {
          gin::ConvertFromV8(isolate, message->Get(), &error_message);
        }
      }
    }
    if (!success) {
      // Failed convert so we send undefined everywhere
      if (callback_)
        std::move(callback_).Run(
            v8::Undefined(isolate),
            v8::Exception::Error(
                v8::String::NewFromUtf8(isolate, error_message.c_str())
                    .ToLocalChecked()));
      promise_.RejectWithErrorMessage(error_message);
    } else {
      v8::Local<v8::Context> context = promise_.GetContext();
      v8::Context::Scope context_scope(context);
      v8::Local<v8::Value> cloned_value = maybe_result.ToLocalChecked();
      if (callback_)
        std::move(callback_).Run(cloned_value, v8::Undefined(isolate));
      promise_.Resolve(cloned_value);
    }
  }

  void Completed(const blink::WebVector<v8::Local<v8::Value>>& result) {
    v8::Isolate* isolate = promise_.isolate();
    if (!result.empty()) {
      if (!result[0].IsEmpty()) {
        v8::Local<v8::Value> value = result[0];
        // Either the result was created in the same world as the caller
        // or the result is not an object and therefore does not have a
        // prototype chain to protect
        bool should_clone_value =
            !(value->IsObject() &&
              promise_.GetContext() ==
                  value.As<v8::Object>()->GetCreationContextChecked()) &&
            value->IsObject();
        if (should_clone_value) {
          CopyResultToCallingContextAndFinalize(isolate,
                                                value.As<v8::Object>());
        } else {
          // Right now only single results per frame is supported.
          if (callback_)
            std::move(callback_).Run(value, v8::Undefined(isolate));
          promise_.Resolve(value);
        }
      } else {
        const char error_message[] =
            "Script failed to execute, this normally means an error "
            "was thrown. Check the renderer console for the error.";
        if (!callback_.is_null()) {
          v8::Local<v8::Context> context = promise_.GetContext();
          v8::Context::Scope context_scope(context);
          std::move(callback_).Run(
              v8::Undefined(isolate),
              v8::Exception::Error(
                  v8::String::NewFromUtf8(isolate, error_message)
                      .ToLocalChecked()));
        }
        promise_.RejectWithErrorMessage(error_message);
      }
    } else {
      const char error_message[] =
          "WebFrame was removed before script could run. This normally means "
          "the underlying frame was destroyed";
      if (!callback_.is_null()) {
        v8::Local<v8::Context> context = promise_.GetContext();
        v8::Context::Scope context_scope(context);
        std::move(callback_).Run(
            v8::Undefined(isolate),
            v8::Exception::Error(v8::String::NewFromUtf8(isolate, error_message)
                                     .ToLocalChecked()));
      }
      promise_.RejectWithErrorMessage(error_message);
    }
    delete this;
  }

 private:
  gin_helper::Promise<v8::Local<v8::Value>> promise_;
  CompletionCallback callback_;
};

class FrameSetSpellChecker : public content::RenderFrameVisitor {
 public:
  FrameSetSpellChecker(SpellCheckClient* spell_check_client,
                       content::RenderFrame* main_frame)
      : spell_check_client_(spell_check_client), main_frame_(main_frame) {
    content::RenderFrame::ForEach(this);
    main_frame->GetWebFrame()->SetSpellCheckPanelHostClient(spell_check_client);
  }

  // disable copy
  FrameSetSpellChecker(const FrameSetSpellChecker&) = delete;
  FrameSetSpellChecker& operator=(const FrameSetSpellChecker&) = delete;

  bool Visit(content::RenderFrame* render_frame) override {
    if (render_frame->GetMainRenderFrame() == main_frame_ ||
        (render_frame->IsMainFrame() && render_frame == main_frame_)) {
      render_frame->GetWebFrame()->SetTextCheckClient(spell_check_client_);
    }
    return true;
  }

 private:
  SpellCheckClient* spell_check_client_;
  content::RenderFrame* main_frame_;
};

class SpellCheckerHolder final : private content::RenderFrameObserver {
 public:
  // Find existing holder for the |render_frame|.
  static SpellCheckerHolder* FromRenderFrame(
      content::RenderFrame* render_frame) {
    for (auto* holder : instances_) {
      if (holder->render_frame() == render_frame)
        return holder;
    }
    return nullptr;
  }

  SpellCheckerHolder(content::RenderFrame* render_frame,
                     std::unique_ptr<SpellCheckClient> spell_check_client)
      : content::RenderFrameObserver(render_frame),
        spell_check_client_(std::move(spell_check_client)) {
    DCHECK(!FromRenderFrame(render_frame));
    instances_.insert(this);
  }

  ~SpellCheckerHolder() final { instances_.erase(this); }

  void UnsetAndDestroy() {
    FrameSetSpellChecker set_spell_checker(nullptr, render_frame());
    delete this;
  }

  // RenderFrameObserver implementation.
  void OnDestruct() final {
    // Since we delete this in WillReleaseScriptContext, this method is unlikely
    // to be called, but override anyway since I'm not sure if there are some
    // corner cases.
    //
    // Note that while there are two "delete this", it is totally fine as the
    // observer unsubscribes automatically in destructor and the other one won't
    // be called.
    //
    // Also note that we should not call UnsetAndDestroy here, as the render
    // frame is going to be destroyed.
    delete this;
  }

  void WillReleaseScriptContext(v8::Local<v8::Context> context,
                                int world_id) final {
    // Unset spell checker when the script context is going to be released, as
    // the spell check implementation lives there.
    UnsetAndDestroy();
  }

 private:
  static std::set<SpellCheckerHolder*> instances_;

  std::unique_ptr<SpellCheckClient> spell_check_client_;
};

class WebFrameRenderer : public gin::Wrappable<WebFrameRenderer>,
                         private content::RenderFrameObserver {
 public:
  static gin::WrapperInfo kWrapperInfo;

  static gin::Handle<WebFrameRenderer> Create(
      v8::Isolate* isolate,
      content::RenderFrame* render_frame) {
    return gin::CreateHandle(isolate, new WebFrameRenderer(render_frame));
  }

  explicit WebFrameRenderer(content::RenderFrame* render_frame)
      : content::RenderFrameObserver(render_frame) {
    DCHECK(render_frame);
  }

  // gin::Wrappable:
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override {
    return gin::Wrappable<WebFrameRenderer>::GetObjectTemplateBuilder(isolate)
        .SetMethod("getWebFrameId", &WebFrameRenderer::GetWebFrameId)
        .SetMethod("setName", &WebFrameRenderer::SetName)
        .SetMethod("setZoomLevel", &WebFrameRenderer::SetZoomLevel)
        .SetMethod("getZoomLevel", &WebFrameRenderer::GetZoomLevel)
        .SetMethod("setZoomFactor", &WebFrameRenderer::SetZoomFactor)
        .SetMethod("getZoomFactor", &WebFrameRenderer::GetZoomFactor)
        .SetMethod("getWebPreference", &WebFrameRenderer::GetWebPreference)
#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
        .SetMethod("isWordMisspelled", &WebFrameRenderer::IsWordMisspelled)
        .SetMethod("getWordSuggestions", &WebFrameRenderer::GetWordSuggestions)
#endif
        .SetMethod("setVisualZoomLevelLimits",
                   &WebFrameRenderer::SetVisualZoomLevelLimits)
        .SetMethod("allowGuestViewElementDefinition",
                   &RendererClientBase::AllowGuestViewElementDefinition)
        .SetMethod("insertText", &WebFrameRenderer::InsertText)
        .SetMethod("insertCSS", &WebFrameRenderer::InsertCSS)
        .SetMethod("removeInsertedCSS", &WebFrameRenderer::RemoveInsertedCSS)
        .SetMethod("_isEvalAllowed", &WebFrameRenderer::IsEvalAllowed)
        .SetMethod("executeJavaScript", &WebFrameRenderer::ExecuteJavaScript)
        .SetMethod("executeJavaScriptInIsolatedWorld",
                   &WebFrameRenderer::ExecuteJavaScriptInIsolatedWorld)
        .SetMethod("setIsolatedWorldInfo",
                   &WebFrameRenderer::SetIsolatedWorldInfo)
        .SetMethod("getResourceUsage", &WebFrameRenderer::GetResourceUsage)
        .SetMethod("clearCache", &WebFrameRenderer::ClearCache)
        .SetMethod("setSpellCheckProvider",
                   &WebFrameRenderer::SetSpellCheckProvider)
        // Frame navigators
        .SetMethod("findFrameByRoutingId",
                   &WebFrameRenderer::FindFrameByRoutingId)
        .SetMethod("getFrameForSelector",
                   &WebFrameRenderer::GetFrameForSelector)
        .SetMethod("findFrameByName", &WebFrameRenderer::FindFrameByName)
        .SetProperty("opener", &WebFrameRenderer::GetOpener)
        .SetProperty("parent", &WebFrameRenderer::GetFrameParent)
        .SetProperty("top", &WebFrameRenderer::GetTop)
        .SetProperty("firstChild", &WebFrameRenderer::GetFirstChild)
        .SetProperty("nextSibling", &WebFrameRenderer::GetNextSibling)
        .SetProperty("routingId", &WebFrameRenderer::GetRoutingId);
  }

  const char* GetTypeName() override { return "WebFrameRenderer"; }

  void OnDestruct() override {}

 private:
  bool MaybeGetRenderFrame(v8::Isolate* isolate,
                           const std::string_view method_name,
                           content::RenderFrame** render_frame_ptr) {
    std::string error_msg;
    if (!MaybeGetRenderFrame(&error_msg, method_name, render_frame_ptr)) {
      gin_helper::ErrorThrower(isolate).ThrowError(error_msg);
      return false;
    }
    return true;
  }

  bool MaybeGetRenderFrame(std::string* error_msg,
                           const std::string_view method_name,
                           content::RenderFrame** render_frame_ptr) {
    auto* frame = render_frame();
    if (!frame) {
      *error_msg = base::ToString("Render frame was torn down before webFrame.",
                                  method_name, " could be executed");
      return false;
    }
    *render_frame_ptr = frame;
    return true;
  }

  static v8::Local<v8::Value> CreateWebFrameRenderer(v8::Isolate* isolate,
                                                     blink::WebFrame* frame) {
    if (frame && frame->IsWebLocalFrame()) {
      auto* render_frame =
          content::RenderFrame::FromWebFrame(frame->ToWebLocalFrame());
      return WebFrameRenderer::Create(isolate, render_frame).ToV8();
    } else {
      return v8::Null(isolate);
    }
  }

  void SetName(v8::Isolate* isolate, const std::string& name) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "setName", &render_frame))
      return;

    render_frame->GetWebFrame()->SetName(blink::WebString::FromUTF8(name));
  }

  void SetZoomLevel(v8::Isolate* isolate, double level) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "setZoomLevel", &render_frame))
      return;

    mojo::AssociatedRemote<mojom::ElectronWebContentsUtility>
        web_contents_utility_remote;
    render_frame->GetRemoteAssociatedInterfaces()->GetInterface(
        &web_contents_utility_remote);
    web_contents_utility_remote->SetTemporaryZoomLevel(level);
  }

  double GetZoomLevel(v8::Isolate* isolate) {
    double result = 0.0;
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "getZoomLevel", &render_frame))
      return result;

    mojo::AssociatedRemote<mojom::ElectronWebContentsUtility>
        web_contents_utility_remote;
    render_frame->GetRemoteAssociatedInterfaces()->GetInterface(
        &web_contents_utility_remote);
    web_contents_utility_remote->DoGetZoomLevel(&result);
    return result;
  }

  void SetZoomFactor(gin_helper::ErrorThrower thrower, double factor) {
    if (factor < std::numeric_limits<double>::epsilon()) {
      thrower.ThrowError("'zoomFactor' must be a double greater than 0.0");
      return;
    }

    SetZoomLevel(thrower.isolate(), blink::PageZoomFactorToZoomLevel(factor));
  }

  double GetZoomFactor(v8::Isolate* isolate) {
    double zoom_level = GetZoomLevel(isolate);
    return blink::PageZoomLevelToZoomFactor(zoom_level);
  }

  v8::Local<v8::Value> GetWebPreference(v8::Isolate* isolate,
                                        std::string pref_name) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "getWebPreference", &render_frame))
      return v8::Undefined(isolate);

    const auto& prefs = render_frame->GetBlinkPreferences();

    if (pref_name == "isWebView") {
      // FIXME(zcbenz): For child windows opened with window.open('') from
      // webview, the WebPreferences is inherited from webview and the value
      // of |is_webview| is wrong.
      // Please check ElectronRenderFrameObserver::DidInstallConditionalFeatures
      // for the background.
      auto* web_frame = render_frame->GetWebFrame();
      if (web_frame->Opener())
        return gin::ConvertToV8(isolate, false);
      return gin::ConvertToV8(isolate, prefs.is_webview);
    } else if (pref_name == options::kHiddenPage) {
      // NOTE: hiddenPage is internal-only.
      return gin::ConvertToV8(isolate, prefs.hidden_page);
    } else if (pref_name == options::kNodeIntegration) {
      return gin::ConvertToV8(isolate, prefs.node_integration);
    } else if (pref_name == options::kWebviewTag) {
      return gin::ConvertToV8(isolate, prefs.webview_tag);
    }
    return v8::Null(isolate);
  }

  void SetVisualZoomLevelLimits(v8::Isolate* isolate,
                                double min_level,
                                double max_level) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "setVisualZoomLevelLimits",
                             &render_frame))
      return;

    blink::WebFrame* web_frame = render_frame->GetWebFrame();
    web_frame->View()->SetDefaultPageScaleLimits(min_level, max_level);
  }

  static int GetWebFrameId(v8::Local<v8::Object> content_window) {
    // Get the WebLocalFrame before (possibly) executing any user-space JS while
    // getting the |params|. We track the status of the RenderFrame via an
    // observer in case it is deleted during user code execution.
    content::RenderFrame* render_frame = GetRenderFrame(content_window);
    if (!render_frame)
      return -1;

    blink::WebLocalFrame* frame = render_frame->GetWebFrame();
    // Parent must exist.
    blink::WebFrame* parent_frame = frame->Parent();
    DCHECK(parent_frame);
    DCHECK(parent_frame->IsWebLocalFrame());

    return render_frame->GetRoutingID();
  }

  void SetSpellCheckProvider(gin_helper::ErrorThrower thrower,
                             v8::Isolate* isolate,
                             const std::string& language,
                             v8::Local<v8::Object> provider) {
    auto context = isolate->GetCurrentContext();
    if (!provider->Has(context, gin::StringToV8(isolate, "spellCheck"))
             .ToChecked()) {
      thrower.ThrowError("\"spellCheck\" has to be defined");
      return;
    }

    // Remove the old client.
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "setSpellCheckProvider", &render_frame))
      return;

    auto* existing = SpellCheckerHolder::FromRenderFrame(render_frame);
    if (existing)
      existing->UnsetAndDestroy();

    // Set spellchecker for all live frames in the same process or
    // in the sandbox mode for all live sub frames to this WebFrame.
    auto spell_check_client =
        std::make_unique<SpellCheckClient>(language, isolate, provider);
    FrameSetSpellChecker spell_checker(spell_check_client.get(), render_frame);

    // Attach the spell checker to RenderFrame.
    new SpellCheckerHolder(render_frame, std::move(spell_check_client));
  }

  void InsertText(v8::Isolate* isolate, const std::string& text) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "insertText", &render_frame))
      return;

    blink::WebFrame* web_frame = render_frame->GetWebFrame();
    if (web_frame->IsWebLocalFrame()) {
      web_frame->ToWebLocalFrame()
          ->FrameWidget()
          ->GetActiveWebInputMethodController()
          ->CommitText(blink::WebString::FromUTF8(text),
                       blink::WebVector<ui::ImeTextSpan>(), blink::WebRange(),
                       0);
    }
  }

  std::u16string InsertCSS(v8::Isolate* isolate,
                           const std::string& css,
                           gin::Arguments* args) {
    blink::WebCssOrigin css_origin = blink::WebCssOrigin::kAuthor;

    gin_helper::Dictionary options;
    if (args->GetNext(&options))
      options.Get("cssOrigin", &css_origin);

    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "insertCSS", &render_frame))
      return std::u16string();

    blink::WebFrame* web_frame = render_frame->GetWebFrame();
    if (web_frame->IsWebLocalFrame()) {
      return web_frame->ToWebLocalFrame()
          ->GetDocument()
          .InsertStyleSheet(blink::WebString::FromUTF8(css), nullptr,
                            css_origin)
          .Utf16();
    }
    return std::u16string();
  }

  void RemoveInsertedCSS(v8::Isolate* isolate, const std::u16string& key) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "removeInsertedCSS", &render_frame))
      return;

    blink::WebFrame* web_frame = render_frame->GetWebFrame();
    if (web_frame->IsWebLocalFrame()) {
      web_frame->ToWebLocalFrame()->GetDocument().RemoveInsertedStyleSheet(
          blink::WebString::FromUTF16(key));
    }
  }

  bool IsEvalAllowed(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "isEvalAllowed", &render_frame))
      return true;

    auto* context = blink::ExecutionContext::From(
        render_frame->GetWebFrame()->MainWorldScriptContext());
    return !context->GetContentSecurityPolicy()->ShouldCheckEval();
  }

  v8::Local<v8::Promise> ExecuteJavaScript(gin::Arguments* gin_args,
                                           const std::u16string& code) {
    gin_helper::Arguments* args = static_cast<gin_helper::Arguments*>(gin_args);

    v8::Isolate* isolate = args->isolate();
    gin_helper::Promise<v8::Local<v8::Value>> promise(isolate);
    v8::Local<v8::Promise> handle = promise.GetHandle();

    content::RenderFrame* render_frame;
    std::string error_msg;
    if (!MaybeGetRenderFrame(&error_msg, "executeJavaScript", &render_frame)) {
      promise.RejectWithErrorMessage(error_msg);
      return handle;
    }

    const blink::WebScriptSource source{blink::WebString::FromUTF16(code)};

    bool has_user_gesture = false;
    args->GetNext(&has_user_gesture);

    ScriptExecutionCallback::CompletionCallback completion_callback;
    args->GetNext(&completion_callback);

    auto* self = new ScriptExecutionCallback(std::move(promise),
                                             std::move(completion_callback));

    render_frame->GetWebFrame()->RequestExecuteScript(
        blink::DOMWrapperWorld::kMainWorldId, base::make_span(&source, 1u),
        has_user_gesture ? blink::mojom::UserActivationOption::kActivate
                         : blink::mojom::UserActivationOption::kDoNotActivate,
        blink::mojom::EvaluationTiming::kSynchronous,
        blink::mojom::LoadEventBlockingOption::kDoNotBlock,
        base::NullCallback(),
        base::BindOnce(&ScriptExecutionCallback::Completed,
                       base::Unretained(self)),
        blink::BackForwardCacheAware::kAllow,
        blink::mojom::WantResultOption::kWantResult,
        blink::mojom::PromiseResultOption::kDoNotWait);

    return handle;
  }

  v8::Local<v8::Promise> ExecuteJavaScriptInIsolatedWorld(
      gin::Arguments* gin_args,
      int world_id,
      const std::vector<gin_helper::Dictionary>& scripts) {
    gin_helper::Arguments* args = static_cast<gin_helper::Arguments*>(gin_args);

    v8::Isolate* isolate = args->isolate();
    gin_helper::Promise<v8::Local<v8::Value>> promise(isolate);
    v8::Local<v8::Promise> handle = promise.GetHandle();

    content::RenderFrame* render_frame;
    std::string error_msg;
    if (!MaybeGetRenderFrame(&error_msg, "executeJavaScriptInIsolatedWorld",
                             &render_frame)) {
      promise.RejectWithErrorMessage(error_msg);
      return handle;
    }

    bool has_user_gesture = false;
    args->GetNext(&has_user_gesture);

    blink::mojom::EvaluationTiming script_execution_type =
        blink::mojom::EvaluationTiming::kSynchronous;
    blink::mojom::LoadEventBlockingOption load_blocking_option =
        blink::mojom::LoadEventBlockingOption::kDoNotBlock;
    std::string execution_type;
    args->GetNext(&execution_type);

    if (execution_type == "asynchronous") {
      script_execution_type = blink::mojom::EvaluationTiming::kAsynchronous;
    } else if (execution_type == "asynchronousBlockingOnload") {
      script_execution_type = blink::mojom::EvaluationTiming::kAsynchronous;
      load_blocking_option = blink::mojom::LoadEventBlockingOption::kBlock;
    }

    ScriptExecutionCallback::CompletionCallback completion_callback;
    args->GetNext(&completion_callback);

    std::vector<blink::WebScriptSource> sources;
    sources.reserve(scripts.size());

    for (const auto& script : scripts) {
      std::u16string code;
      std::u16string url;
      script.Get("url", &url);

      if (!script.Get("code", &code)) {
        const char error_message[] = "Invalid 'code'";
        if (!completion_callback.is_null()) {
          std::move(completion_callback)
              .Run(v8::Undefined(isolate),
                   v8::Exception::Error(
                       v8::String::NewFromUtf8(isolate, error_message)
                           .ToLocalChecked()));
        }
        promise.RejectWithErrorMessage(error_message);
        return handle;
      }

      sources.emplace_back(blink::WebString::FromUTF16(code),
                           blink::WebURL(GURL(url)));
    }

    // Deletes itself.
    auto* self = new ScriptExecutionCallback(std::move(promise),
                                             std::move(completion_callback));

    render_frame->GetWebFrame()->RequestExecuteScript(
        world_id, base::make_span(sources),
        has_user_gesture ? blink::mojom::UserActivationOption::kActivate
                         : blink::mojom::UserActivationOption::kDoNotActivate,
        script_execution_type, load_blocking_option, base::NullCallback(),
        base::BindOnce(&ScriptExecutionCallback::Completed,
                       base::Unretained(self)),
        blink::BackForwardCacheAware::kPossiblyDisallow,
        blink::mojom::WantResultOption::kWantResult,
        blink::mojom::PromiseResultOption::kDoNotWait);

    return handle;
  }

  void SetIsolatedWorldInfo(v8::Isolate* isolate,
                            int world_id,
                            const gin_helper::Dictionary& options) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "setIsolatedWorldInfo", &render_frame))
      return;

    std::string origin_url, security_policy, name;
    options.Get("securityOrigin", &origin_url);
    options.Get("csp", &security_policy);
    options.Get("name", &name);

    if (!security_policy.empty() && origin_url.empty()) {
      gin_helper::ErrorThrower(isolate).ThrowError(
          "If csp is specified, securityOrigin should also be specified");
      return;
    }

    blink::WebIsolatedWorldInfo info;
    info.security_origin = blink::WebSecurityOrigin::CreateFromString(
        blink::WebString::FromUTF8(origin_url));
    info.content_security_policy = blink::WebString::FromUTF8(security_policy);
    info.human_readable_name = blink::WebString::FromUTF8(name);
    blink::SetIsolatedWorldInfo(world_id, info);
  }

  blink::WebCacheResourceTypeStats GetResourceUsage(v8::Isolate* isolate) {
    blink::WebCacheResourceTypeStats stats;
    blink::WebCache::GetResourceTypeStats(&stats);
    return stats;
  }

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  bool IsWordMisspelled(v8::Isolate* isolate, const std::string& word) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "isWordMisspelled", &render_frame))
      return false;

    return !SpellCheckWord(render_frame, word, nullptr);
  }

  std::vector<std::u16string> GetWordSuggestions(v8::Isolate* isolate,
                                                 const std::string& word) {
    content::RenderFrame* render_frame;
    std::vector<std::u16string> suggestions;
    if (!MaybeGetRenderFrame(isolate, "getWordSuggestions", &render_frame))
      return suggestions;

    SpellCheckWord(render_frame, word, &suggestions);
    return suggestions;
  }
#endif

  void ClearCache(v8::Isolate* isolate) {
    blink::WebCache::Clear();
    base::MemoryPressureListener::NotifyMemoryPressure(
        base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  }

  v8::Local<v8::Value> FindFrameByRoutingId(v8::Isolate* isolate,
                                            int routing_id) {
    content::RenderFrame* render_frame =
        content::RenderFrame::FromRoutingID(routing_id);
    if (render_frame)
      return WebFrameRenderer::Create(isolate, render_frame).ToV8();
    else
      return v8::Null(isolate);
  }

  v8::Local<v8::Value> GetOpener(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "opener", &render_frame))
      return v8::Null(isolate);

    blink::WebFrame* frame = render_frame->GetWebFrame()->Opener();
    return CreateWebFrameRenderer(isolate, frame);
  }

  // Don't name it as GetParent, Windows has API with same name.
  v8::Local<v8::Value> GetFrameParent(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "parent", &render_frame))
      return v8::Null(isolate);

    blink::WebFrame* frame = render_frame->GetWebFrame()->Parent();
    return CreateWebFrameRenderer(isolate, frame);
  }

  v8::Local<v8::Value> GetTop(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "top", &render_frame))
      return v8::Null(isolate);

    blink::WebFrame* frame = render_frame->GetWebFrame()->Top();
    return CreateWebFrameRenderer(isolate, frame);
  }

  v8::Local<v8::Value> GetFirstChild(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "firstChild", &render_frame))
      return v8::Null(isolate);

    blink::WebFrame* frame = render_frame->GetWebFrame()->FirstChild();
    return CreateWebFrameRenderer(isolate, frame);
  }

  v8::Local<v8::Value> GetNextSibling(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "nextSibling", &render_frame))
      return v8::Null(isolate);

    blink::WebFrame* frame = render_frame->GetWebFrame()->NextSibling();
    return CreateWebFrameRenderer(isolate, frame);
  }

  v8::Local<v8::Value> GetFrameForSelector(v8::Isolate* isolate,
                                           const std::string& selector) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "getFrameForSelector", &render_frame))
      return v8::Null(isolate);

    blink::WebElement element =
        render_frame->GetWebFrame()->GetDocument().QuerySelector(
            blink::WebString::FromUTF8(selector));
    if (element.IsNull())  // not found
      return v8::Null(isolate);

    blink::WebFrame* frame = blink::WebFrame::FromFrameOwnerElement(element);
    return CreateWebFrameRenderer(isolate, frame);
  }

  v8::Local<v8::Value> FindFrameByName(v8::Isolate* isolate,
                                       const std::string& name) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "findFrameByName", &render_frame))
      return v8::Null(isolate);

    blink::WebFrame* frame = render_frame->GetWebFrame()->FindFrameByName(
        blink::WebString::FromUTF8(name));
    return CreateWebFrameRenderer(isolate, frame);
  }

  int GetRoutingId(v8::Isolate* isolate) {
    content::RenderFrame* render_frame;
    if (!MaybeGetRenderFrame(isolate, "routingId", &render_frame))
      return 0;

    return render_frame->GetRoutingID();
  }
};
}  // namespace

gin::WrapperInfo WebFrameRenderer::kWrapperInfo = {gin::kEmbedderNativeGin};

// static
std::set<SpellCheckerHolder*> SpellCheckerHolder::instances_;

}  // namespace api

}  // namespace electron

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  using namespace electron::api;  // NOLINT(build/namespaces)

  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("mainFrame", WebFrameRenderer::Create(
                            isolate, electron::GetRenderFrame(exports)));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_renderer_web_frame, Initialize)
