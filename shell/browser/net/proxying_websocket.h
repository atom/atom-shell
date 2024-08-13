// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NET_PROXYING_WEBSOCKET_H_
#define ELECTRON_SHELL_BROWSER_NET_PROXYING_WEBSOCKET_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/content_browser_client.h"
#include "extensions/browser/api/web_request/web_request_info.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"
#include "shell/browser/net/web_request_api_interface.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace electron {

// A ProxyingWebSocket proxies a WebSocket connection and dispatches
// WebRequest API events.
//
// The code is referenced from the
// extensions::WebRequestProxyingWebSocket class.
class ProxyingWebSocket : public network::mojom::WebSocketHandshakeClient,
                          public network::mojom::WebSocketAuthenticationHandler,
                          public network::mojom::TrustedHeaderClient {
 public:
  using WebSocketFactory = content::ContentBrowserClient::WebSocketFactory;

  // AuthRequiredResponse indicates how an OnAuthRequired call is handled.
  enum class AuthRequiredResponse {
    // No credentials were provided.
    kNoAction,
    // AuthCredentials is filled in with a username and password, which should
    // be used in a response to the provided auth challenge.
    kSetAuth,
    // The request should be canceled.
    kCancelAuth,
    // The action will be decided asynchronously. |callback| will be invoked
    // when the decision is made, and one of the other AuthRequiredResponse
    // values will be passed in with the same semantics as described above.
    kIoPending,
  };

  ProxyingWebSocket(
      WebRequestAPI* web_request_api,
      WebSocketFactory factory,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      bool has_extra_headers,
      int process_id,
      int render_frame_id,
      content::BrowserContext* browser_context,
      uint64_t* request_id_generator);
  ~ProxyingWebSocket() override;

  // disable copy
  ProxyingWebSocket(const ProxyingWebSocket&) = delete;
  ProxyingWebSocket& operator=(const ProxyingWebSocket&) = delete;

  void Start();

  // network::mojom::WebSocketHandshakeClient methods:
  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr request) override;
  void OnFailure(const std::string& message,
                 int32_t net_error,
                 int32_t response_code) override {}
  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> websocket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr response,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override;

  // network::mojom::WebSocketAuthenticationHandler method:
  void OnAuthRequired(const net::AuthChallengeInfo& auth_info,
                      const scoped_refptr<net::HttpResponseHeaders>& headers,
                      const net::IPEndPoint& remote_endpoint,
                      OnAuthRequiredCallback callback) override;

  // network::mojom::TrustedHeaderClient methods:
  void OnBeforeSendHeaders(const net::HttpRequestHeaders& headers,
                           OnBeforeSendHeadersCallback callback) override;
  void OnHeadersReceived(const std::string& headers,
                         const net::IPEndPoint& endpoint,
                         OnHeadersReceivedCallback callback) override;

  static void StartProxying(
      WebRequestAPI* web_request_api,
      WebSocketFactory factory,
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const std::optional<std::string>& user_agent,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      bool has_extra_headers,
      int process_id,
      int render_frame_id,
      const url::Origin& origin,
      content::BrowserContext* browser_context,
      uint64_t* request_id_generator);

  WebRequestAPI* web_request_api() { return web_request_api_; }

 private:
  void OnBeforeRequestComplete(int error_code);
  void OnBeforeSendHeadersComplete(const std::set<std::string>& removed_headers,
                                   const std::set<std::string>& set_headers,
                                   int error_code);
  void ContinueToStartRequest(int error_code);
  void OnHeadersReceivedComplete(int error_code);
  void ContinueToHeadersReceived();
  void OnAuthRequiredComplete(AuthRequiredResponse rv);
  void OnHeadersReceivedCompleteForAuth(const net::AuthChallengeInfo& auth_info,
                                        int rv);
  void ContinueToCompleted();

  void PauseIncomingMethodCallProcessing();
  void ResumeIncomingMethodCallProcessing();
  void OnError(int error_code);
  // This is used for detecting errors on mojo connection with the network
  // service.
  void OnMojoConnectionErrorWithCustomReason(uint32_t custom_reason,
                                             const std::string& description);
  // This is used for detecting errors on mojo connection with original client
  // (i.e., renderer).
  void OnMojoConnectionError();

  // Passed from api::WebRequest.
  raw_ptr<WebRequestAPI> web_request_api_;

  // Saved to feed the api::WebRequest.
  network::ResourceRequest request_;

  WebSocketFactory factory_;
  mojo::Remote<network::mojom::WebSocketHandshakeClient>
      forwarding_handshake_client_;
  mojo::Receiver<network::mojom::WebSocketHandshakeClient>
      receiver_as_handshake_client_{this};
  mojo::Receiver<network::mojom::WebSocketAuthenticationHandler>
      receiver_as_auth_handler_{this};
  mojo::Receiver<network::mojom::TrustedHeaderClient>
      receiver_as_header_client_{this};

  net::HttpRequestHeaders request_headers_;
  network::mojom::URLResponseHeadPtr response_;
  net::AuthCredentials auth_credentials_;
  OnAuthRequiredCallback auth_required_callback_;
  scoped_refptr<net::HttpResponseHeaders> override_headers_;
  std::vector<network::mojom::HttpHeaderPtr> additional_headers_;

  OnBeforeSendHeadersCallback on_before_send_headers_callback_;
  OnHeadersReceivedCallback on_headers_received_callback_;

  GURL redirect_url_;
  bool is_done_ = false;
  bool has_extra_headers_;
  mojo::PendingRemote<network::mojom::WebSocket> websocket_;
  mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver_;
  network::mojom::WebSocketHandshakeResponsePtr handshake_response_ = nullptr;
  mojo::ScopedDataPipeConsumerHandle readable_;
  mojo::ScopedDataPipeProducerHandle writable_;

  extensions::WebRequestInfo info_;

  base::WeakPtrFactory<ProxyingWebSocket> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NET_PROXYING_WEBSOCKET_H_
