// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/api/electron_api_url_loader.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/fixed_flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/sequence_checker.h"
#include "base/strings/string_number_conversions.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "net/base/load_flags.h"
#include "net/http/http_util.h"
#include "net/url_request/redirect_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/url_util.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/chunked_data_pipe_getter.mojom.h"
#include "services/network/public/mojom/http_raw_headers.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/net/asar/asar_url_loader_factory.h"
#include "shell/browser/net/proxying_url_loader_factory.h"
#include "shell/browser/protocol_registry.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/net_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_includes.h"
#include "shell/common/process_util.h"
#include "shell/services/node/node_service.h"
#include "third_party/blink/public/common/loader/referrer_utils.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom.h"

namespace gin {

template <>
struct Converter<network::mojom::HttpRawHeaderPairPtr> {
  static v8::Local<v8::Value> ToV8(
      v8::Isolate* isolate,
      const network::mojom::HttpRawHeaderPairPtr& pair) {
    auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
    dict.Set("key", pair->key);
    dict.Set("value", pair->value);
    return dict.GetHandle();
  }
};

template <>
struct Converter<network::mojom::CredentialsMode> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     network::mojom::CredentialsMode* out) {
    using Val = network::mojom::CredentialsMode;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"include", Val::kInclude},
            {"omit", Val::kOmit},
            // Note: This only makes sense if the request
            // specifies the "origin" option.
            {"same-origin", Val::kSameOrigin},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

template <>
struct Converter<blink::mojom::FetchCacheMode> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     blink::mojom::FetchCacheMode* out) {
    using Val = blink::mojom::FetchCacheMode;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"default", Val::kDefault},
            {"force-cache", Val::kForceCache},
            {"no-cache", Val::kValidateCache},
            {"no-store", Val::kNoStore},
            {"only-if-cached", Val::kOnlyIfCached},
            {"reload", Val::kBypassCache},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

template <>
struct Converter<net::ReferrerPolicy> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     net::ReferrerPolicy* out) {
    using Val = net::ReferrerPolicy;
    // clang-format off
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"", Val::REDUCE_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN},
            {"no-referrer", Val::NO_REFERRER},
            {"no-referrer-when-downgrade", Val::CLEAR_ON_TRANSITION_FROM_SECURE_TO_INSECURE},
            {"origin", Val::ORIGIN},
            {"origin-when-cross-origin", Val::ORIGIN_ONLY_ON_TRANSITION_CROSS_ORIGIN},
            {"same-origin", Val::CLEAR_ON_TRANSITION_CROSS_ORIGIN},
            {"strict-origin", Val:: ORIGIN_CLEAR_ON_TRANSITION_FROM_SECURE_TO_INSECURE},
            {"strict-origin-when-cross-origin", Val::REDUCE_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN},
            {"unsafe-url", Val::NEVER_CLEAR},
        });
    // clang-format on
    return FromV8WithLowerLookup(isolate, val, Lookup, out);
  }
};
}  // namespace gin

namespace electron::api {

namespace {

class BufferDataSource : public mojo::DataPipeProducer::DataSource {
 public:
  explicit BufferDataSource(base::span<char> buffer) {
    buffer_.resize(buffer.size());
    memcpy(buffer_.data(), buffer.data(), buffer_.size());
  }
  ~BufferDataSource() override = default;

 private:
  // mojo::DataPipeProducer::DataSource:
  uint64_t GetLength() const override { return buffer_.size(); }
  ReadResult Read(uint64_t offset, base::span<char> buffer) override {
    ReadResult result;
    if (offset <= buffer_.size()) {
      size_t readable_size = buffer_.size() - offset;
      size_t writable_size = buffer.size();
      size_t copyable_size = std::min(readable_size, writable_size);
      if (copyable_size > 0) {
        memcpy(buffer.data(), &buffer_[offset], copyable_size);
      }
      result.bytes_read = copyable_size;
    } else {
      NOTREACHED_IN_MIGRATION();
      result.result = MOJO_RESULT_OUT_OF_RANGE;
    }
    return result;
  }

  std::vector<char> buffer_;
};

class JSChunkedDataPipeGetter : public gin::Wrappable<JSChunkedDataPipeGetter>,
                                public network::mojom::ChunkedDataPipeGetter {
 public:
  static gin::Handle<JSChunkedDataPipeGetter> Create(
      v8::Isolate* isolate,
      v8::Local<v8::Function> body_func,
      mojo::PendingReceiver<network::mojom::ChunkedDataPipeGetter>
          chunked_data_pipe_getter) {
    return gin::CreateHandle(
        isolate, new JSChunkedDataPipeGetter(
                     isolate, body_func, std::move(chunked_data_pipe_getter)));
  }

  // gin::Wrappable
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override {
    return gin::Wrappable<JSChunkedDataPipeGetter>::GetObjectTemplateBuilder(
               isolate)
        .SetMethod("write", &JSChunkedDataPipeGetter::WriteChunk)
        .SetMethod("done", &JSChunkedDataPipeGetter::Done);
  }

  const char* GetTypeName() override { return "JSChunkedDataPipeGetter"; }

  static gin::WrapperInfo kWrapperInfo;
  ~JSChunkedDataPipeGetter() override = default;

 private:
  JSChunkedDataPipeGetter(
      v8::Isolate* isolate,
      v8::Local<v8::Function> body_func,
      mojo::PendingReceiver<network::mojom::ChunkedDataPipeGetter>
          chunked_data_pipe_getter)
      : isolate_(isolate), body_func_(isolate, body_func) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
    receiver_.Bind(std::move(chunked_data_pipe_getter));
  }

  // network::mojom::ChunkedDataPipeGetter:
  void GetSize(GetSizeCallback callback) override {
    size_callback_ = std::move(callback);
  }

  void StartReading(mojo::ScopedDataPipeProducerHandle pipe) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (body_func_.IsEmpty()) {
      LOG(ERROR) << "Tried to read twice from a JSChunkedDataPipeGetter";
      // Drop the handle on the floor.
      return;
    }
    data_producer_ = std::make_unique<mojo::DataPipeProducer>(std::move(pipe));

    v8::HandleScope handle_scope(isolate_);
    auto maybe_wrapper = GetWrapper(isolate_);
    v8::Local<v8::Value> wrapper;
    if (!maybe_wrapper.ToLocal(&wrapper)) {
      return;
    }
    v8::Local<v8::Value> argv[] = {wrapper};
    node::Environment* env = node::Environment::GetCurrent(isolate_);
    auto global = env->context()->Global();
    node::MakeCallback(isolate_, global, body_func_.Get(isolate_),
                       node::arraysize(argv), argv, {0, 0});
  }

  v8::Local<v8::Promise> WriteChunk(v8::Local<v8::Value> buffer_val) {
    gin_helper::Promise<void> promise(isolate_);
    v8::Local<v8::Promise> handle = promise.GetHandle();
    if (!buffer_val->IsArrayBufferView()) {
      promise.RejectWithErrorMessage("Expected an ArrayBufferView");
      return handle;
    }
    if (is_writing_) {
      promise.RejectWithErrorMessage("Only one write can be pending at a time");
      return handle;
    }
    if (!size_callback_) {
      promise.RejectWithErrorMessage("Can't write after calling done()");
      return handle;
    }
    auto buffer = buffer_val.As<v8::ArrayBufferView>();
    is_writing_ = true;
    bytes_written_ += buffer->ByteLength();
    auto backing_store = buffer->Buffer()->GetBackingStore();
    auto buffer_span = base::make_span(
        static_cast<char*>(backing_store->Data()) + buffer->ByteOffset(),
        buffer->ByteLength());
    auto buffer_source = std::make_unique<BufferDataSource>(buffer_span);
    data_producer_->Write(
        std::move(buffer_source),
        base::BindOnce(&JSChunkedDataPipeGetter::OnWriteChunkComplete,
                       // We're OK to use Unretained here because we own
                       // |data_producer_|.
                       base::Unretained(this), std::move(promise)));
    return handle;
  }

  void OnWriteChunkComplete(gin_helper::Promise<void> promise,
                            MojoResult result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    is_writing_ = false;
    if (result == MOJO_RESULT_OK) {
      promise.Resolve();
    } else {
      promise.RejectWithErrorMessage("mojo result not ok: " +
                                     base::NumberToString(result));
      Finished();
    }
  }

  // TODO(nornagon): accept a net error here to allow the data provider to
  // cancel the request with an error.
  void Done() {
    if (size_callback_) {
      std::move(size_callback_).Run(net::OK, bytes_written_);
      Finished();
    }
  }

  void Finished() {
    body_func_.Reset();
    data_producer_.reset();
    receiver_.reset();
    size_callback_.Reset();
  }

  SEQUENCE_CHECKER(sequence_checker_);
  GetSizeCallback size_callback_;
  mojo::Receiver<network::mojom::ChunkedDataPipeGetter> receiver_{this};
  std::unique_ptr<mojo::DataPipeProducer> data_producer_;
  bool is_writing_ = false;
  uint64_t bytes_written_ = 0;

  raw_ptr<v8::Isolate> isolate_;
  v8::Global<v8::Function> body_func_;
};

gin::WrapperInfo JSChunkedDataPipeGetter::kWrapperInfo = {
    gin::kEmbedderNativeGin};

const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("electron_net_module", R"(
        semantics {
          sender: "Electron Net module"
          description:
            "Issue HTTP/HTTPS requests using Chromium's native networking "
            "library."
          trigger: "Using the Net module"
          data: "Anything the user wants to send."
          destination: OTHER
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "This feature cannot be disabled."
        })");

}  // namespace

gin::WrapperInfo SimpleURLLoaderWrapper::kWrapperInfo = {
    gin::kEmbedderNativeGin};

SimpleURLLoaderWrapper::SimpleURLLoaderWrapper(
    ElectronBrowserContext* browser_context,
    std::unique_ptr<network::ResourceRequest> request,
    int options)
    : browser_context_(browser_context),
      request_options_(options),
      request_(std::move(request)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  if (!request_->trusted_params)
    request_->trusted_params = network::ResourceRequest::TrustedParams();
  mojo::PendingRemote<network::mojom::URLLoaderNetworkServiceObserver>
      url_loader_network_observer_remote;
  url_loader_network_observer_receivers_.Add(
      this,
      url_loader_network_observer_remote.InitWithNewPipeAndPassReceiver());
  request_->trusted_params->url_loader_network_observer =
      std::move(url_loader_network_observer_remote);
  // Chromium filters headers using browser rules, while for net module we have
  // every header passed. The following setting will allow us to capture the
  // raw headers in the URLLoader.
  request_->trusted_params->report_raw_headers = true;
  Start();
}

void SimpleURLLoaderWrapper::Start() {
  // Make a copy of the request; we'll need to re-send it if we get redirected.
  auto request = std::make_unique<network::ResourceRequest>();
  *request = *request_;

  // SimpleURLLoader has no way to set a data pipe as the request body, which
  // we need to do for streaming upload, so instead we "cheat" and pretend to
  // SimpleURLLoader like there is no request_body when we construct it. Later,
  // we will sneakily put the request_body back while it isn't looking.
  scoped_refptr<network::ResourceRequestBody> request_body =
      std::move(request->request_body);

  network::ResourceRequest* request_ref = request.get();
  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);

  if (request_body)
    request_ref->request_body = std::move(request_body);

  loader_->SetAllowHttpErrorResults(true);
  loader_->SetURLLoaderFactoryOptions(request_options_);
  loader_->SetOnResponseStartedCallback(base::BindOnce(
      &SimpleURLLoaderWrapper::OnResponseStarted, base::Unretained(this)));
  loader_->SetOnRedirectCallback(base::BindRepeating(
      &SimpleURLLoaderWrapper::OnRedirect, base::Unretained(this)));
  loader_->SetOnUploadProgressCallback(base::BindRepeating(
      &SimpleURLLoaderWrapper::OnUploadProgress, base::Unretained(this)));
  loader_->SetOnDownloadProgressCallback(base::BindRepeating(
      &SimpleURLLoaderWrapper::OnDownloadProgress, base::Unretained(this)));

  url_loader_factory_ = GetURLLoaderFactoryForURL(request_ref->url);
  loader_->DownloadAsStream(url_loader_factory_.get(), this);
}

void SimpleURLLoaderWrapper::Pin() {
  // Prevent ourselves from being GC'd until the request is complete.  Must be
  // called after gin::CreateHandle, otherwise the wrapper isn't initialized.
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  pinned_wrapper_.Reset(isolate, GetWrapper(isolate).ToLocalChecked());
}

void SimpleURLLoaderWrapper::PinBodyGetter(v8::Local<v8::Value> body_getter) {
  pinned_chunk_pipe_getter_.Reset(JavascriptEnvironment::GetIsolate(),
                                  body_getter);
}

SimpleURLLoaderWrapper::~SimpleURLLoaderWrapper() = default;

void SimpleURLLoaderWrapper::OnAuthRequired(
    const std::optional<base::UnguessableToken>& window_id,
    uint32_t request_id,
    const GURL& url,
    bool first_auth_attempt,
    const net::AuthChallengeInfo& auth_info,
    const scoped_refptr<net::HttpResponseHeaders>& head_headers,
    mojo::PendingRemote<network::mojom::AuthChallengeResponder>
        auth_challenge_responder) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  mojo::Remote<network::mojom::AuthChallengeResponder> auth_responder(
      std::move(auth_challenge_responder));
  // WeakPtr because if we're Cancel()ed while waiting for auth, and the
  // network service also decides to cancel at the same time and kill this
  // pipe, we might end up trying to call Cancel again on dead memory.
  auth_responder.set_disconnect_handler(base::BindOnce(
      &SimpleURLLoaderWrapper::Cancel, weak_factory_.GetWeakPtr()));
  auto cb = base::BindOnce(
      [](mojo::Remote<network::mojom::AuthChallengeResponder> auth_responder,
         gin::Arguments* args) {
        std::u16string username_str, password_str;
        if (!args->GetNext(&username_str) || !args->GetNext(&password_str)) {
          auth_responder->OnAuthCredentials(std::nullopt);
          return;
        }
        auth_responder->OnAuthCredentials(
            net::AuthCredentials(username_str, password_str));
      },
      std::move(auth_responder));
  Emit("login", auth_info, base::AdaptCallbackForRepeating(std::move(cb)));
}

void SimpleURLLoaderWrapper::OnSSLCertificateError(
    const GURL& url,
    int net_error,
    const net::SSLInfo& ssl_info,
    bool fatal,
    OnSSLCertificateErrorCallback response) {
  std::move(response).Run(net_error);
}

void SimpleURLLoaderWrapper::OnClearSiteData(
    const GURL& url,
    const std::string& header_value,
    int32_t load_flags,
    const std::optional<net::CookiePartitionKey>& cookie_partition_key,
    bool partitioned_state_allowed_only,
    OnClearSiteDataCallback callback) {
  std::move(callback).Run();
}
void SimpleURLLoaderWrapper::OnLoadingStateUpdate(
    network::mojom::LoadInfoPtr info,
    OnLoadingStateUpdateCallback callback) {
  std::move(callback).Run();
}

void SimpleURLLoaderWrapper::OnSharedStorageHeaderReceived(
    const url::Origin& request_origin,
    std::vector<network::mojom::SharedStorageOperationPtr> operations,
    OnSharedStorageHeaderReceivedCallback callback) {
  std::move(callback).Run();
}

void SimpleURLLoaderWrapper::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderNetworkServiceObserver>
        observer) {
  url_loader_network_observer_receivers_.Add(this, std::move(observer));
}

void SimpleURLLoaderWrapper::Cancel() {
  loader_.reset();
  pinned_wrapper_.Reset();
  pinned_chunk_pipe_getter_.Reset();
  // This ensures that no further callbacks will be called, so there's no need
  // for additional guards.
}
scoped_refptr<network::SharedURLLoaderFactory>
SimpleURLLoaderWrapper::GetURLLoaderFactoryForURL(const GURL& url) {
  if (electron::IsUtilityProcess())
    return URLLoaderBundle::GetInstance()->GetSharedURLLoaderFactory();

  CHECK(browser_context_);
  // Explicitly handle intercepted protocols here, even though
  // ProxyingURLLoaderFactory would handle them later on, so that we can
  // correctly intercept file:// scheme URLs.
  if (const bool bypass = request_options_ & kBypassCustomProtocolHandlers;
      !bypass) {
    const auto scheme = url.scheme();
    const auto* const protocol_registry =
        ProtocolRegistry::FromBrowserContext(browser_context_);

    if (const auto* const protocol_handler =
            protocol_registry->FindIntercepted(scheme)) {
      return network::SharedURLLoaderFactory::Create(
          std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
              ElectronURLLoaderFactory::Create(protocol_handler->first,
                                               protocol_handler->second)));
    }

    if (const auto* const protocol_handler =
            protocol_registry->FindRegistered(scheme)) {
      return network::SharedURLLoaderFactory::Create(
          std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
              ElectronURLLoaderFactory::Create(protocol_handler->first,
                                               protocol_handler->second)));
    }
  }

  if (url.SchemeIsFile()) {
    return network::SharedURLLoaderFactory::Create(
        std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
            AsarURLLoaderFactory::Create()));
  }

  return browser_context_->GetURLLoaderFactory();
}

// static
gin::Handle<SimpleURLLoaderWrapper> SimpleURLLoaderWrapper::Create(
    gin::Arguments* args) {
  gin_helper::Dictionary opts;
  if (!args->GetNext(&opts)) {
    args->ThrowTypeError("Expected a dictionary");
    return gin::Handle<SimpleURLLoaderWrapper>();
  }
  auto request = std::make_unique<network::ResourceRequest>();
  opts.Get("method", &request->method);
  opts.Get("url", &request->url);
  if (!request->url.is_valid()) {
    args->ThrowTypeError("Invalid URL");
    return gin::Handle<SimpleURLLoaderWrapper>();
  }
  request->site_for_cookies = net::SiteForCookies::FromUrl(request->url);
  opts.Get("referrer", &request->referrer);
  request->referrer_policy =
      blink::ReferrerUtils::GetDefaultNetReferrerPolicy();
  opts.Get("referrerPolicy", &request->referrer_policy);
  std::string origin;
  opts.Get("origin", &origin);
  if (!origin.empty()) {
    request->request_initiator = url::Origin::Create(GURL(origin));
  }
  bool has_user_activation;
  if (opts.Get("hasUserActivation", &has_user_activation)) {
    request->trusted_params = network::ResourceRequest::TrustedParams();
    request->trusted_params->has_user_activation = has_user_activation;
  }

  if (std::string mode; opts.Get("mode", &mode)) {
    using Val = network::mojom::RequestMode;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"cors", Val::kCors},
            {"navigate", Val::kNavigate},
            {"no-cors", Val::kNoCors},
            {"same-origin", Val::kSameOrigin},
        });
    if (auto* iter = Lookup.find(mode); iter != Lookup.end())
      request->mode = iter->second;
  }

  if (std::string destination; opts.Get("destination", &destination)) {
    using Val = network::mojom::RequestDestination;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"audio", Val::kAudio},
            {"audioworklet", Val::kAudioWorklet},
            {"document", Val::kDocument},
            {"embed", Val::kEmbed},
            {"empty", Val::kEmpty},
            {"font", Val::kFont},
            {"frame", Val::kFrame},
            {"iframe", Val::kIframe},
            {"image", Val::kImage},
            {"manifest", Val::kManifest},
            {"object", Val::kObject},
            {"paintworklet", Val::kPaintWorklet},
            {"report", Val::kReport},
            {"script", Val::kScript},
            {"serviceworker", Val::kServiceWorker},
            {"style", Val::kStyle},
            {"track", Val::kTrack},
            {"video", Val::kVideo},
            {"worker", Val::kWorker},
            {"xslt", Val::kXslt},
        });
    if (auto* iter = Lookup.find(destination); iter != Lookup.end())
      request->destination = iter->second;
  }

  bool credentials_specified =
      opts.Get("credentials", &request->credentials_mode);
  std::vector<std::pair<std::string, std::string>> extra_headers;
  if (opts.Get("extraHeaders", &extra_headers)) {
    for (const auto& it : extra_headers) {
      if (!net::HttpUtil::IsValidHeaderName(it.first) ||
          !net::HttpUtil::IsValidHeaderValue(it.second)) {
        args->ThrowTypeError("Invalid header name or value");
        return gin::Handle<SimpleURLLoaderWrapper>();
      }
      request->headers.SetHeader(it.first, it.second);
    }
  }

  blink::mojom::FetchCacheMode cache_mode =
      blink::mojom::FetchCacheMode::kDefault;
  opts.Get("cache", &cache_mode);
  switch (cache_mode) {
    case blink::mojom::FetchCacheMode::kNoStore:
      request->load_flags |= net::LOAD_DISABLE_CACHE;
      break;
    case blink::mojom::FetchCacheMode::kValidateCache:
      request->load_flags |= net::LOAD_VALIDATE_CACHE;
      break;
    case blink::mojom::FetchCacheMode::kBypassCache:
      request->load_flags |= net::LOAD_BYPASS_CACHE;
      break;
    case blink::mojom::FetchCacheMode::kForceCache:
      request->load_flags |= net::LOAD_SKIP_CACHE_VALIDATION;
      break;
    case blink::mojom::FetchCacheMode::kOnlyIfCached:
      request->load_flags |=
          net::LOAD_ONLY_FROM_CACHE | net::LOAD_SKIP_CACHE_VALIDATION;
      break;
    case blink::mojom::FetchCacheMode::kUnspecifiedOnlyIfCachedStrict:
      request->load_flags |= net::LOAD_ONLY_FROM_CACHE;
      break;
    case blink::mojom::FetchCacheMode::kDefault:
      break;
    case blink::mojom::FetchCacheMode::kUnspecifiedForceCacheMiss:
      request->load_flags |= net::LOAD_ONLY_FROM_CACHE | net::LOAD_BYPASS_CACHE;
      break;
  }

  bool use_session_cookies = false;
  opts.Get("useSessionCookies", &use_session_cookies);
  int options = network::mojom::kURLLoadOptionSniffMimeType;
  if (!credentials_specified && !use_session_cookies) {
    // This is the default case, as well as the case when credentials is not
    // specified and useSessionCookies is false. credentials_mode will be
    // kInclude, but cookies will be blocked.
    request->credentials_mode = network::mojom::CredentialsMode::kInclude;
    options |= network::mojom::kURLLoadOptionBlockAllCookies;
  }

  bool bypass_custom_protocol_handlers = false;
  opts.Get("bypassCustomProtocolHandlers", &bypass_custom_protocol_handlers);
  if (bypass_custom_protocol_handlers)
    options |= kBypassCustomProtocolHandlers;

  v8::Local<v8::Value> body;
  v8::Local<v8::Value> chunk_pipe_getter;
  if (opts.Get("body", &body)) {
    if (body->IsArrayBufferView()) {
      auto buffer_body = body.As<v8::ArrayBufferView>();
      auto backing_store = buffer_body->Buffer()->GetBackingStore();
      request->request_body = network::ResourceRequestBody::CreateFromBytes(
          static_cast<char*>(backing_store->Data()) + buffer_body->ByteOffset(),
          buffer_body->ByteLength());
    } else if (body->IsFunction()) {
      auto body_func = body.As<v8::Function>();

      mojo::PendingRemote<network::mojom::ChunkedDataPipeGetter>
          data_pipe_getter;
      chunk_pipe_getter = JSChunkedDataPipeGetter::Create(
                              args->isolate(), body_func,
                              data_pipe_getter.InitWithNewPipeAndPassReceiver())
                              .ToV8();
      request->request_body =
          base::MakeRefCounted<network::ResourceRequestBody>();
      request->request_body->SetToChunkedDataPipe(
          std::move(data_pipe_getter),
          network::ResourceRequestBody::ReadOnlyOnce(false));
    }
  }

  ElectronBrowserContext* browser_context = nullptr;
  if (electron::IsBrowserProcess()) {
    std::string partition;
    gin::Handle<Session> session;
    if (!opts.Get("session", &session)) {
      if (opts.Get("partition", &partition))
        session = Session::FromPartition(args->isolate(), partition);
      else  // default session
        session = Session::FromPartition(args->isolate(), "");
    }
    browser_context = session->browser_context();
  }

  auto ret = gin::CreateHandle(
      args->isolate(),
      new SimpleURLLoaderWrapper(browser_context, std::move(request), options));
  ret->Pin();
  if (!chunk_pipe_getter.IsEmpty()) {
    ret->PinBodyGetter(chunk_pipe_getter);
  }
  return ret;
}

void SimpleURLLoaderWrapper::OnDataReceived(std::string_view string_piece,
                                            base::OnceClosure resume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto array_buffer = v8::ArrayBuffer::New(isolate, string_piece.size());
  auto backing_store = array_buffer->GetBackingStore();
  memcpy(backing_store->Data(), string_piece.data(), string_piece.size());
  Emit("data", array_buffer,
       base::AdaptCallbackForRepeating(std::move(resume)));
}

void SimpleURLLoaderWrapper::OnComplete(bool success) {
  if (success) {
    Emit("complete");
  } else {
    Emit("error", net::ErrorToString(loader_->NetError()));
  }
  loader_.reset();
  pinned_wrapper_.Reset();
  pinned_chunk_pipe_getter_.Reset();
}

void SimpleURLLoaderWrapper::OnRetry(base::OnceClosure start_retry) {}

void SimpleURLLoaderWrapper::OnResponseStarted(
    const GURL& final_url,
    const network::mojom::URLResponseHead& response_head) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  auto dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("statusCode", response_head.headers->response_code());
  dict.Set("statusMessage", response_head.headers->GetStatusText());
  dict.Set("httpVersion", response_head.headers->GetHttpVersion());
  dict.Set("headers", response_head.headers.get());
  dict.Set("rawHeaders", response_head.raw_response_headers);
  dict.Set("mimeType", response_head.mime_type);
  Emit("response-started", final_url, dict);
}

void SimpleURLLoaderWrapper::OnRedirect(
    const GURL& url_before_redirect,
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* removed_headers) {
  Emit("redirect", redirect_info, response_head.headers.get());

  if (!loader_)
    // The redirect was aborted by JS.
    return;

  // Optimization: if both the old and new URLs are handled by the network
  // service, just FollowRedirect.
  if (network::IsURLHandledByNetworkService(redirect_info.new_url) &&
      network::IsURLHandledByNetworkService(request_->url))
    return;

  // Otherwise, restart the request (potentially picking a new
  // URLLoaderFactory). See
  // https://source.chromium.org/chromium/chromium/src/+/main:content/browser/loader/navigation_url_loader_impl.cc;l=534-550;drc=fbaec92ad5982f83aa4544d5c88d66d08034a9f4

  bool should_clear_upload = false;
  net::RedirectUtil::UpdateHttpRequest(
      request_->url, request_->method, redirect_info, *removed_headers,
      /* modified_headers = */ std::nullopt, &request_->headers,
      &should_clear_upload);
  if (should_clear_upload) {
    // The request body is no longer applicable.
    request_->request_body.reset();
  }

  request_->url = redirect_info.new_url;
  request_->method = redirect_info.new_method;
  request_->site_for_cookies = redirect_info.new_site_for_cookies;

  // See if navigation network isolation key needs to be updated.
  request_->trusted_params->isolation_info =
      request_->trusted_params->isolation_info.CreateForRedirect(
          url::Origin::Create(request_->url));

  request_->referrer = GURL(redirect_info.new_referrer);
  request_->referrer_policy = redirect_info.new_referrer_policy;
  request_->navigation_redirect_chain.push_back(redirect_info.new_url);

  Start();
}

void SimpleURLLoaderWrapper::OnUploadProgress(uint64_t position,
                                              uint64_t total) {
  Emit("upload-progress", position, total);
}

void SimpleURLLoaderWrapper::OnDownloadProgress(uint64_t current) {
  Emit("download-progress", current);
}

// static
gin::ObjectTemplateBuilder SimpleURLLoaderWrapper::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin_helper::EventEmitterMixin<
             SimpleURLLoaderWrapper>::GetObjectTemplateBuilder(isolate)
      .SetMethod("cancel", &SimpleURLLoaderWrapper::Cancel);
}

const char* SimpleURLLoaderWrapper::GetTypeName() {
  return "SimpleURLLoaderWrapper";
}

}  // namespace electron::api
