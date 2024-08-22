// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_web_request.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/containers/contains.h"
#include "base/containers/fixed_flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "extensions/browser/api/web_request/web_request_info.h"
#include "extensions/browser/api/web_request/web_request_resource_type.h"
#include "extensions/common/url_pattern.h"
#include "gin/converter.h"
#include "gin/dictionary.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "net/http/http_content_disposition.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/api/electron_api_web_frame_main.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/net_converter.h"
#include "shell/common/gin_converters/std_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"

static constexpr auto ResourceTypes =
    base::MakeFixedFlatMap<std::string_view,
                           extensions::WebRequestResourceType>({
        {"cspReport", extensions::WebRequestResourceType::CSP_REPORT},
        {"font", extensions::WebRequestResourceType::FONT},
        {"image", extensions::WebRequestResourceType::IMAGE},
        {"mainFrame", extensions::WebRequestResourceType::MAIN_FRAME},
        {"media", extensions::WebRequestResourceType::MEDIA},
        {"object", extensions::WebRequestResourceType::OBJECT},
        {"ping", extensions::WebRequestResourceType::PING},
        {"script", extensions::WebRequestResourceType::SCRIPT},
        {"stylesheet", extensions::WebRequestResourceType::STYLESHEET},
        {"subFrame", extensions::WebRequestResourceType::SUB_FRAME},
        {"webSocket", extensions::WebRequestResourceType::WEB_SOCKET},
        {"xhr", extensions::WebRequestResourceType::XHR},
    });

namespace gin {

template <>
struct Converter<extensions::WebRequestResourceType> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   extensions::WebRequestResourceType type) {
    for (const auto& [name, val] : ResourceTypes)
      if (type == val)
        return StringToV8(isolate, name);

    return StringToV8(isolate, "other");
  }
};

}  // namespace gin

namespace electron::api {

namespace {

const char kUserDataKey[] = "WebRequest";

// BrowserContext <=> WebRequest relationship.
struct UserData : public base::SupportsUserData::Data {
  explicit UserData(WebRequest* data) : data(data) {}
  raw_ptr<WebRequest> data;
};

extensions::WebRequestResourceType ParseResourceType(std::string_view value) {
  if (const auto* iter = ResourceTypes.find(value); iter != ResourceTypes.end())
    return iter->second;

  return extensions::WebRequestResourceType::OTHER;
}

// Convert HttpResponseHeaders to V8.
//
// Note that while we already have converters for HttpResponseHeaders, we can
// not use it because it lowercases the header keys, while the webRequest has
// to pass the original keys.
v8::Local<v8::Value> HttpResponseHeadersToV8(
    net::HttpResponseHeaders* headers) {
  base::Value::Dict response_headers;
  if (headers) {
    size_t iter = 0;
    std::string key;
    std::string value;
    while (headers->EnumerateHeaderLines(&iter, &key, &value)) {
      // Note that Web servers not developed with nodejs allow non-utf8
      // characters in content-disposition's filename field. Use Chromium's
      // HttpContentDisposition class to decode the correct encoding instead of
      // arbitrarily converting it to UTF8. It should also be noted that if the
      // encoding is not specified, HttpContentDisposition will transcode
      // according to the system's encoding.
      if (base::EqualsCaseInsensitiveASCII("Content-Disposition", key) &&
          !value.empty()) {
        net::HttpContentDisposition header(value, std::string());
        std::string decodedFilename =
            header.is_attachment() ? " attachment" : " inline";
        // The filename must be encased in double quotes for serialization
        // to happen correctly.
        std::string filename = "\"" + header.filename() + "\"";
        value = decodedFilename + "; filename=" + filename;
      }
      response_headers.EnsureList(key)->Append(value);
    }
  }
  return gin::ConvertToV8(v8::Isolate::GetCurrent(), response_headers);
}

// Overloaded by multiple types to fill the |details| object.
void ToDictionary(gin_helper::Dictionary* details,
                  extensions::WebRequestInfo* info) {
  details->Set("id", info->id);
  details->Set("url", info->url);
  details->Set("method", info->method);
  details->Set("timestamp",
               base::Time::Now().InSecondsFSinceUnixEpoch() * 1000);
  details->Set("resourceType", info->web_request_type);
  if (!info->response_ip.empty())
    details->Set("ip", info->response_ip);
  if (info->response_headers) {
    details->Set("fromCache", info->response_from_cache);
    details->Set("statusLine", info->response_headers->GetStatusLine());
    details->Set("statusCode", info->response_headers->response_code());
    details->Set("responseHeaders",
                 HttpResponseHeadersToV8(info->response_headers.get()));
  }

  auto* render_frame_host = content::RenderFrameHost::FromID(
      info->render_process_id, info->frame_routing_id);
  if (render_frame_host) {
    details->SetGetter("frame", render_frame_host);
    auto* web_contents =
        content::WebContents::FromRenderFrameHost(render_frame_host);
    auto* api_web_contents = WebContents::From(web_contents);
    if (api_web_contents) {
      details->Set("webContents", api_web_contents);
      details->Set("webContentsId", api_web_contents->ID());
    }
  }
}

void ToDictionary(gin_helper::Dictionary* details,
                  const network::ResourceRequest& request) {
  details->Set("referrer", request.referrer);
  if (request.request_body)
    details->Set("uploadData", *request.request_body);
}

void ToDictionary(gin_helper::Dictionary* details,
                  const net::HttpRequestHeaders& headers) {
  details->Set("requestHeaders", headers);
}

void ToDictionary(gin_helper::Dictionary* details, const GURL& location) {
  details->Set("redirectURL", location);
}

void ToDictionary(gin_helper::Dictionary* details, int net_error) {
  details->Set("error", net::ErrorToString(net_error));
}

// Helper function to fill |details| with arbitrary |args|.
template <typename Arg>
void FillDetails(gin_helper::Dictionary* details, Arg arg) {
  ToDictionary(details, arg);
}

template <typename Arg, typename... Args>
void FillDetails(gin_helper::Dictionary* details, Arg arg, Args... args) {
  ToDictionary(details, arg);
  FillDetails(details, args...);
}

// Modified from extensions/browser/api/web_request/web_request_api_helpers.cc.
std::pair<std::set<std::string>, std::set<std::string>>
CalculateOnBeforeSendHeadersDelta(const net::HttpRequestHeaders* old_headers,
                                  const net::HttpRequestHeaders* new_headers) {
  // Newly introduced or overridden request headers.
  std::set<std::string> modified_request_headers;
  // Keys of request headers to be deleted.
  std::set<std::string> deleted_request_headers;

  // The event listener might not have passed any new headers if it
  // just wanted to cancel the request.
  if (new_headers) {
    // Find deleted headers.
    {
      net::HttpRequestHeaders::Iterator i(*old_headers);
      while (i.GetNext()) {
        if (!new_headers->HasHeader(i.name())) {
          deleted_request_headers.insert(i.name());
        }
      }
    }

    // Find modified headers.
    {
      net::HttpRequestHeaders::Iterator i(*new_headers);
      while (i.GetNext()) {
        if (i.value() != old_headers->GetHeader(i.name())) {
          modified_request_headers.insert(i.name());
        }
      }
    }
  }

  return std::make_pair(modified_request_headers, deleted_request_headers);
}

}  // namespace

gin::WrapperInfo WebRequest::kWrapperInfo = {gin::kEmbedderNativeGin};

WebRequest::RequestFilter::RequestFilter(
    std::set<URLPattern> url_patterns,
    std::set<extensions::WebRequestResourceType> types)
    : url_patterns_(std::move(url_patterns)), types_(std::move(types)) {}
WebRequest::RequestFilter::RequestFilter(const RequestFilter&) = default;
WebRequest::RequestFilter::RequestFilter() = default;
WebRequest::RequestFilter::~RequestFilter() = default;

void WebRequest::RequestFilter::AddUrlPattern(URLPattern pattern) {
  url_patterns_.emplace(std::move(pattern));
}

void WebRequest::RequestFilter::AddType(
    extensions::WebRequestResourceType type) {
  types_.insert(type);
}

bool WebRequest::RequestFilter::MatchesURL(const GURL& url) const {
  if (url_patterns_.empty())
    return true;

  for (const auto& pattern : url_patterns_) {
    if (pattern.MatchesURL(url))
      return true;
  }
  return false;
}

bool WebRequest::RequestFilter::MatchesType(
    extensions::WebRequestResourceType type) const {
  return types_.empty() || base::Contains(types_, type);
}

bool WebRequest::RequestFilter::MatchesRequest(
    extensions::WebRequestInfo* info) const {
  return MatchesURL(info->url) && MatchesType(info->web_request_type);
}

struct WebRequest::BlockedRequest {
  BlockedRequest() = default;
  raw_ptr<const extensions::WebRequestInfo> request = nullptr;
  net::CompletionOnceCallback callback;
  // Only used for onBeforeSendHeaders.
  BeforeSendHeadersCallback before_send_headers_callback;
  // Only used for onBeforeSendHeaders.
  raw_ptr<net::HttpRequestHeaders> request_headers = nullptr;
  // Only used for onHeadersReceived.
  scoped_refptr<const net::HttpResponseHeaders> original_response_headers;
  // Only used for onHeadersReceived.
  raw_ptr<scoped_refptr<net::HttpResponseHeaders>> override_response_headers =
      nullptr;
  std::string status_line;
  // Only used for onBeforeRequest.
  raw_ptr<GURL> new_url = nullptr;
};

WebRequest::SimpleListenerInfo::SimpleListenerInfo(RequestFilter filter_,
                                                   SimpleListener listener_)
    : filter(std::move(filter_)), listener(listener_) {}
WebRequest::SimpleListenerInfo::SimpleListenerInfo() = default;
WebRequest::SimpleListenerInfo::~SimpleListenerInfo() = default;

WebRequest::ResponseListenerInfo::ResponseListenerInfo(
    RequestFilter filter_,
    ResponseListener listener_)
    : filter(std::move(filter_)), listener(listener_) {}
WebRequest::ResponseListenerInfo::ResponseListenerInfo() = default;
WebRequest::ResponseListenerInfo::~ResponseListenerInfo() = default;

WebRequest::WebRequest(v8::Isolate* isolate,
                       content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  browser_context_->SetUserData(kUserDataKey, std::make_unique<UserData>(this));
}

WebRequest::~WebRequest() {
  browser_context_->RemoveUserData(kUserDataKey);
}

gin::ObjectTemplateBuilder WebRequest::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<WebRequest>::GetObjectTemplateBuilder(isolate)
      .SetMethod(
          "onBeforeRequest",
          &WebRequest::SetResponseListener<ResponseEvent::kOnBeforeRequest>)
      .SetMethod(
          "onBeforeSendHeaders",
          &WebRequest::SetResponseListener<ResponseEvent::kOnBeforeSendHeaders>)
      .SetMethod(
          "onHeadersReceived",
          &WebRequest::SetResponseListener<ResponseEvent::kOnHeadersReceived>)
      .SetMethod("onSendHeaders",
                 &WebRequest::SetSimpleListener<SimpleEvent::kOnSendHeaders>)
      .SetMethod("onBeforeRedirect",
                 &WebRequest::SetSimpleListener<SimpleEvent::kOnBeforeRedirect>)
      .SetMethod(
          "onResponseStarted",
          &WebRequest::SetSimpleListener<SimpleEvent::kOnResponseStarted>)
      .SetMethod("onErrorOccurred",
                 &WebRequest::SetSimpleListener<SimpleEvent::kOnErrorOccurred>)
      .SetMethod("onCompleted",
                 &WebRequest::SetSimpleListener<SimpleEvent::kOnCompleted>);
}

const char* WebRequest::GetTypeName() {
  return "WebRequest";
}

bool WebRequest::HasListener() const {
  return !(simple_listeners_.empty() && response_listeners_.empty());
}

int WebRequest::OnBeforeRequest(extensions::WebRequestInfo* info,
                                const network::ResourceRequest& request,
                                net::CompletionOnceCallback callback,
                                GURL* new_url) {
  return HandleOnBeforeRequestResponseEvent(info, request, std::move(callback),
                                            new_url);
}

int WebRequest::HandleOnBeforeRequestResponseEvent(
    extensions::WebRequestInfo* request_info,
    const network::ResourceRequest& request,
    net::CompletionOnceCallback callback,
    GURL* new_url) {
  const auto iter = response_listeners_.find(ResponseEvent::kOnBeforeRequest);
  if (iter == std::end(response_listeners_))
    return net::OK;

  const auto& info = iter->second;
  if (!info.filter.MatchesRequest(request_info))
    return net::OK;

  BlockedRequest blocked_request;
  blocked_request.callback = std::move(callback);
  blocked_request.new_url = new_url;
  blocked_requests_[request_info->id] = std::move(blocked_request);

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  gin_helper::Dictionary details(isolate, v8::Object::New(isolate));
  FillDetails(&details, request_info, request, *new_url);

  ResponseCallback response =
      base::BindOnce(&WebRequest::OnBeforeRequestListenerResult,
                     base::Unretained(this), request_info->id);
  info.listener.Run(gin::ConvertToV8(isolate, details), std::move(response));
  return net::ERR_IO_PENDING;
}

void WebRequest::OnBeforeRequestListenerResult(uint64_t id,
                                               v8::Local<v8::Value> response) {
  const auto iter = blocked_requests_.find(id);
  if (iter == std::end(blocked_requests_))
    return;

  auto& request = iter->second;

  int result = net::OK;
  if (response->IsObject()) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    gin::Dictionary dict(isolate, response.As<v8::Object>());

    bool cancel = false;
    dict.Get("cancel", &cancel);
    if (cancel) {
      result = net::ERR_BLOCKED_BY_CLIENT;
    } else {
      dict.Get("redirectURL", request.new_url.get());
    }
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(request.callback), result));
  blocked_requests_.erase(iter);
}

int WebRequest::OnBeforeSendHeaders(extensions::WebRequestInfo* info,
                                    const network::ResourceRequest& request,
                                    BeforeSendHeadersCallback callback,
                                    net::HttpRequestHeaders* headers) {
  return HandleOnBeforeSendHeadersResponseEvent(info, request,
                                                std::move(callback), headers);
}

int WebRequest::HandleOnBeforeSendHeadersResponseEvent(
    extensions::WebRequestInfo* request_info,
    const network::ResourceRequest& request,
    BeforeSendHeadersCallback callback,
    net::HttpRequestHeaders* headers) {
  const auto iter =
      response_listeners_.find(ResponseEvent::kOnBeforeSendHeaders);
  if (iter == std::end(response_listeners_))
    return net::OK;

  const auto& info = iter->second;
  if (!info.filter.MatchesRequest(request_info))
    return net::OK;

  BlockedRequest blocked_request;
  blocked_request.before_send_headers_callback = std::move(callback);
  blocked_request.request_headers = headers;
  blocked_requests_[request_info->id] = std::move(blocked_request);

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  gin_helper::Dictionary details(isolate, v8::Object::New(isolate));
  FillDetails(&details, request_info, request, *headers);

  ResponseCallback response =
      base::BindOnce(&WebRequest::OnBeforeSendHeadersListenerResult,
                     base::Unretained(this), request_info->id);
  info.listener.Run(gin::ConvertToV8(isolate, details), std::move(response));
  return net::ERR_IO_PENDING;
}

void WebRequest::OnBeforeSendHeadersListenerResult(
    uint64_t id,
    v8::Local<v8::Value> response) {
  const auto iter = blocked_requests_.find(id);
  if (iter == std::end(blocked_requests_))
    return;

  auto& request = iter->second;

  net::HttpRequestHeaders* old_headers = request.request_headers;
  net::HttpRequestHeaders new_headers;

  int result = net::OK;
  bool user_modified_headers = false;
  if (response->IsObject()) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    gin::Dictionary dict(isolate, response.As<v8::Object>());

    bool cancel = false;
    dict.Get("cancel", &cancel);
    if (cancel) {
      result = net::ERR_BLOCKED_BY_CLIENT;
    } else {
      v8::Local<v8::Value> value;
      if (dict.Get("requestHeaders", &value) && value->IsObject()) {
        user_modified_headers = true;
        gin::Converter<net::HttpRequestHeaders>::FromV8(isolate, value,
                                                        &new_headers);
      }
    }
  }

  // If the user passes |cancel|, |new_headers| should be nullptr.
  const auto updated_headers = CalculateOnBeforeSendHeadersDelta(
      old_headers,
      result == net::ERR_BLOCKED_BY_CLIENT ? nullptr : &new_headers);

  // Leave |request.request_headers| unchanged if the user didn't modify it.
  if (user_modified_headers)
    request.request_headers->Swap(&new_headers);

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(request.before_send_headers_callback),
                     updated_headers.first, updated_headers.second, result));
  blocked_requests_.erase(iter);
}

int WebRequest::OnHeadersReceived(
    extensions::WebRequestInfo* info,
    const network::ResourceRequest& request,
    net::CompletionOnceCallback callback,
    const net::HttpResponseHeaders* original_response_headers,
    scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
    GURL* allowed_unsafe_redirect_url) {
  return HandleOnHeadersReceivedResponseEvent(
      info, request, std::move(callback), original_response_headers,
      override_response_headers);
}

int WebRequest::HandleOnHeadersReceivedResponseEvent(
    extensions::WebRequestInfo* request_info,
    const network::ResourceRequest& request,
    net::CompletionOnceCallback callback,
    const net::HttpResponseHeaders* original_response_headers,
    scoped_refptr<net::HttpResponseHeaders>* override_response_headers) {
  const auto iter = response_listeners_.find(ResponseEvent::kOnHeadersReceived);
  if (iter == std::end(response_listeners_))
    return net::OK;

  const auto& info = iter->second;
  if (!info.filter.MatchesRequest(request_info))
    return net::OK;

  BlockedRequest blocked_request;
  blocked_request.callback = std::move(callback);
  blocked_request.override_response_headers = override_response_headers;
  blocked_request.status_line = original_response_headers
                                    ? original_response_headers->GetStatusLine()
                                    : std::string();
  blocked_requests_[request_info->id] = std::move(blocked_request);

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  gin_helper::Dictionary details(isolate, v8::Object::New(isolate));
  FillDetails(&details, request_info, request);

  ResponseCallback response =
      base::BindOnce(&WebRequest::OnHeadersReceivedListenerResult,
                     base::Unretained(this), request_info->id);
  info.listener.Run(gin::ConvertToV8(isolate, details), std::move(response));
  return net::ERR_IO_PENDING;
}

void WebRequest::OnHeadersReceivedListenerResult(
    uint64_t id,
    v8::Local<v8::Value> response) {
  const auto iter = blocked_requests_.find(id);
  if (iter == std::end(blocked_requests_))
    return;

  auto& request = iter->second;

  int result = net::OK;
  bool user_modified_headers = false;
  scoped_refptr<net::HttpResponseHeaders> override_headers(
      new net::HttpResponseHeaders(""));
  if (response->IsObject()) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    gin::Dictionary dict(isolate, response.As<v8::Object>());

    bool cancel = false;
    dict.Get("cancel", &cancel);
    if (cancel) {
      result = net::ERR_BLOCKED_BY_CLIENT;
    } else {
      std::string status_line;
      if (!dict.Get("statusLine", &status_line))
        status_line = request.status_line;
      v8::Local<v8::Value> value;
      if (dict.Get("responseHeaders", &value) && value->IsObject()) {
        user_modified_headers = true;
        override_headers->ReplaceStatusLine(status_line);
        gin::Converter<net::HttpResponseHeaders*>::FromV8(
            isolate, value, override_headers.get());
      }
    }
  }

  if (user_modified_headers)
    request.override_response_headers->swap(override_headers);

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(request.callback), result));
  blocked_requests_.erase(iter);
}

void WebRequest::OnSendHeaders(extensions::WebRequestInfo* info,
                               const network::ResourceRequest& request,
                               const net::HttpRequestHeaders& headers) {
  HandleSimpleEvent(SimpleEvent::kOnSendHeaders, info, request, headers);
}

void WebRequest::OnBeforeRedirect(extensions::WebRequestInfo* info,
                                  const network::ResourceRequest& request,
                                  const GURL& new_location) {
  HandleSimpleEvent(SimpleEvent::kOnBeforeRedirect, info, request,
                    new_location);
}

void WebRequest::OnResponseStarted(extensions::WebRequestInfo* info,
                                   const network::ResourceRequest& request) {
  HandleSimpleEvent(SimpleEvent::kOnResponseStarted, info, request);
}

void WebRequest::OnErrorOccurred(extensions::WebRequestInfo* info,
                                 const network::ResourceRequest& request,
                                 int net_error) {
  blocked_requests_.erase(info->id);

  HandleSimpleEvent(SimpleEvent::kOnErrorOccurred, info, request, net_error);
}

void WebRequest::OnCompleted(extensions::WebRequestInfo* info,
                             const network::ResourceRequest& request,
                             int net_error) {
  blocked_requests_.erase(info->id);

  HandleSimpleEvent(SimpleEvent::kOnCompleted, info, request, net_error);
}

void WebRequest::OnRequestWillBeDestroyed(extensions::WebRequestInfo* info) {
  blocked_requests_.erase(info->id);
}

template <WebRequest::SimpleEvent event>
void WebRequest::SetSimpleListener(gin::Arguments* args) {
  SetListener<SimpleListener>(event, &simple_listeners_, args);
}

template <WebRequest::ResponseEvent event>
void WebRequest::SetResponseListener(gin::Arguments* args) {
  SetListener<ResponseListener>(event, &response_listeners_, args);
}

template <typename Listener, typename Listeners, typename Event>
void WebRequest::SetListener(Event event,
                             Listeners* listeners,
                             gin::Arguments* args) {
  v8::Local<v8::Value> arg;

  // { urls, types }.
  std::set<std::string> filter_patterns, filter_types;
  gin::Dictionary dict(args->isolate());
  if (args->GetNext(&arg) && !arg->IsFunction()) {
    // Note that gin treats Function as Dictionary when doing conversions, so we
    // have to explicitly check if the argument is Function before trying to
    // convert it to Dictionary.
    if (gin::ConvertFromV8(args->isolate(), arg, &dict)) {
      if (!dict.Get("urls", &filter_patterns)) {
        args->ThrowTypeError("Parameter 'filter' must have property 'urls'.");
        return;
      }
      dict.Get("types", &filter_types);
      args->GetNext(&arg);
    }
  }

  RequestFilter filter;

  for (const std::string& filter_pattern : filter_patterns) {
    URLPattern pattern(URLPattern::SCHEME_ALL);
    const URLPattern::ParseResult result = pattern.Parse(filter_pattern);
    if (result == URLPattern::ParseResult::kSuccess) {
      filter.AddUrlPattern(std::move(pattern));
    } else {
      const char* error_type = URLPattern::GetParseResultString(result);
      args->ThrowTypeError("Invalid url pattern " + filter_pattern + ": " +
                           error_type);
      return;
    }
  }

  for (const std::string& filter_type : filter_types) {
    auto type = ParseResourceType(filter_type);
    if (type != extensions::WebRequestResourceType::OTHER) {
      filter.AddType(type);
    } else {
      args->ThrowTypeError("Invalid type " + filter_type);
      return;
    }
  }

  // Function or null.
  Listener listener;
  if (arg.IsEmpty() ||
      !(gin::ConvertFromV8(args->isolate(), arg, &listener) || arg->IsNull())) {
    args->ThrowTypeError("Must pass null or a Function");
    return;
  }

  if (listener.is_null())
    listeners->erase(event);
  else
    (*listeners)[event] = {std::move(filter), std::move(listener)};
}

template <typename... Args>
void WebRequest::HandleSimpleEvent(SimpleEvent event,
                                   extensions::WebRequestInfo* request_info,
                                   Args... args) {
  const auto iter = simple_listeners_.find(event);
  if (iter == std::end(simple_listeners_))
    return;

  const auto& info = iter->second;
  if (!info.filter.MatchesRequest(request_info))
    return;

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  gin_helper::Dictionary details(isolate, v8::Object::New(isolate));
  FillDetails(&details, request_info, args...);
  info.listener.Run(gin::ConvertToV8(isolate, details));
}

// static
gin::Handle<WebRequest> WebRequest::FromOrCreate(
    v8::Isolate* isolate,
    content::BrowserContext* browser_context) {
  gin::Handle<WebRequest> handle = From(isolate, browser_context);
  if (handle.IsEmpty()) {
    // Make sure the |Session| object has the |webRequest| property created.
    v8::Local<v8::Value> web_request =
        Session::CreateFrom(
            isolate, static_cast<ElectronBrowserContext*>(browser_context))
            ->WebRequest(isolate);
    gin::ConvertFromV8(isolate, web_request, &handle);
  }
  DCHECK(!handle.IsEmpty());
  return handle;
}

// static
gin::Handle<WebRequest> WebRequest::Create(
    v8::Isolate* isolate,
    content::BrowserContext* browser_context) {
  DCHECK(From(isolate, browser_context).IsEmpty())
      << "WebRequest already created";
  return gin::CreateHandle(isolate, new WebRequest(isolate, browser_context));
}

// static
gin::Handle<WebRequest> WebRequest::From(
    v8::Isolate* isolate,
    content::BrowserContext* browser_context) {
  if (!browser_context)
    return gin::Handle<WebRequest>();
  auto* user_data =
      static_cast<UserData*>(browser_context->GetUserData(kUserDataKey));
  if (!user_data)
    return gin::Handle<WebRequest>();
  return gin::CreateHandle(isolate, user_data->data.get());
}

}  // namespace electron::api
