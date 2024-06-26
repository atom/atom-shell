// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_REQUEST_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_REQUEST_H_

#include <map>
#include <set>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "extensions/common/url_pattern.h"
#include "gin/arguments.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/net/web_request_api_interface.h"

namespace content {
class BrowserContext;
}

namespace electron::api {

class WebRequest : public gin::Wrappable<WebRequest>, public WebRequestAPI {
 public:
  // Return the WebRequest object attached to |browser_context|, create if there
  // is no one.
  // Note that the lifetime of WebRequest object is managed by Session, instead
  // of the caller.
  static gin::Handle<WebRequest> FromOrCreate(
      v8::Isolate* isolate,
      content::BrowserContext* browser_context);

  // Return a new WebRequest object, this should only be called by Session.
  static gin::Handle<WebRequest> Create(
      v8::Isolate* isolate,
      content::BrowserContext* browser_context);

  // Find the WebRequest object attached to |browser_context|.
  static gin::Handle<WebRequest> From(v8::Isolate* isolate,
                                      content::BrowserContext* browser_context);

  // gin::Wrappable:
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // WebRequestAPI:
  bool HasListener() const override;
  int OnBeforeRequest(extensions::WebRequestInfo* info,
                      const network::ResourceRequest& request,
                      net::CompletionOnceCallback callback,
                      GURL* new_url) override;
  int OnBeforeSendHeaders(extensions::WebRequestInfo* info,
                          const network::ResourceRequest& request,
                          BeforeSendHeadersCallback callback,
                          net::HttpRequestHeaders* headers) override;
  int OnHeadersReceived(
      extensions::WebRequestInfo* info,
      const network::ResourceRequest& request,
      net::CompletionOnceCallback callback,
      const net::HttpResponseHeaders* original_response_headers,
      scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
      GURL* allowed_unsafe_redirect_url) override;
  void OnSendHeaders(extensions::WebRequestInfo* info,
                     const network::ResourceRequest& request,
                     const net::HttpRequestHeaders& headers) override;
  void OnBeforeRedirect(extensions::WebRequestInfo* info,
                        const network::ResourceRequest& request,
                        const GURL& new_location) override;
  void OnResponseStarted(extensions::WebRequestInfo* info,
                         const network::ResourceRequest& request) override;
  void OnErrorOccurred(extensions::WebRequestInfo* info,
                       const network::ResourceRequest& request,
                       int net_error) override;
  void OnCompleted(extensions::WebRequestInfo* info,
                   const network::ResourceRequest& request,
                   int net_error) override;
  void OnRequestWillBeDestroyed(extensions::WebRequestInfo* info) override;

 private:
  WebRequest(v8::Isolate* isolate, content::BrowserContext* browser_context);
  ~WebRequest() override;

  enum class SimpleEvent {
    kOnSendHeaders,
    kOnBeforeRedirect,
    kOnResponseStarted,
    kOnCompleted,
    kOnErrorOccurred,
  };
  enum class ResponseEvent {
    kOnBeforeRequest,
    kOnBeforeSendHeaders,
    kOnHeadersReceived,
  };

  using SimpleListener = base::RepeatingCallback<void(v8::Local<v8::Value>)>;
  using ResponseCallback = base::OnceCallback<void(v8::Local<v8::Value>)>;
  using ResponseListener =
      base::RepeatingCallback<void(v8::Local<v8::Value>, ResponseCallback)>;

  template <SimpleEvent event>
  void SetSimpleListener(gin::Arguments* args);
  template <ResponseEvent event>
  void SetResponseListener(gin::Arguments* args);
  template <typename Listener, typename Listeners, typename Event>
  void SetListener(Event event, Listeners* listeners, gin::Arguments* args);

  template <typename... Args>
  void HandleSimpleEvent(SimpleEvent event,
                         extensions::WebRequestInfo* info,
                         Args... args);
  template <typename Out, typename... Args>
  int HandleResponseEvent(ResponseEvent event,
                          extensions::WebRequestInfo* info,
                          net::CompletionOnceCallback callback,
                          Out out,
                          Args... args);

  template <typename T>
  void OnListenerResult(uint64_t id, T out, v8::Local<v8::Value> response);

  class RequestFilter {
   public:
    RequestFilter(std::set<URLPattern>,
                  std::set<extensions::WebRequestResourceType>);
    RequestFilter(const RequestFilter&);
    RequestFilter();
    ~RequestFilter();

    void AddUrlPattern(URLPattern pattern);
    void AddType(extensions::WebRequestResourceType type);

    bool MatchesRequest(extensions::WebRequestInfo* info) const;

   private:
    bool MatchesURL(const GURL& url) const;
    bool MatchesType(extensions::WebRequestResourceType type) const;

    std::set<URLPattern> url_patterns_;
    std::set<extensions::WebRequestResourceType> types_;
  };

  struct SimpleListenerInfo {
    RequestFilter filter;
    SimpleListener listener;

    SimpleListenerInfo(RequestFilter, SimpleListener);
    SimpleListenerInfo();
    ~SimpleListenerInfo();
  };

  struct ResponseListenerInfo {
    RequestFilter filter;
    ResponseListener listener;

    ResponseListenerInfo(RequestFilter, ResponseListener);
    ResponseListenerInfo();
    ~ResponseListenerInfo();
  };

  std::map<SimpleEvent, SimpleListenerInfo> simple_listeners_;
  std::map<ResponseEvent, ResponseListenerInfo> response_listeners_;
  std::map<uint64_t, net::CompletionOnceCallback> callbacks_;

  // Weak-ref, it manages us.
  raw_ptr<content::BrowserContext> browser_context_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_WEB_REQUEST_H_
