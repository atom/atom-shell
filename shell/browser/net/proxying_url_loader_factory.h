// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NET_PROXYING_URL_LOADER_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_NET_PROXYING_URL_LOADER_FACTORY_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/content_browser_client.h"
#include "extensions/browser/api/web_request/web_request_info.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/completion_once_callback.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"
#include "services/network/url_loader_factory.h"
#include "shell/browser/net/electron_url_loader_factory.h"
#include "shell/browser/net/web_request_api_interface.h"
#include "url/gurl.h"

namespace mojo {
template <typename T>
class PendingReceiver;
template <typename T>
class PendingRemote;
}  // namespace mojo

namespace electron {

const uint32_t kBypassCustomProtocolHandlers = 1 << 30;

// This class is responsible for following tasks when NetworkService is enabled:
// 1. handling intercepted protocols;
// 2. implementing webRequest module;
//
// For the task #2, the code is referenced from the
// extensions::WebRequestProxyingURLLoaderFactory class.
class ProxyingURLLoaderFactory
    : public network::mojom::URLLoaderFactory,
      public network::mojom::TrustedURLLoaderHeaderClient {
 public:
  class InProgressRequest : public network::mojom::URLLoader,
                            public network::mojom::URLLoaderClient,
                            public network::mojom::TrustedHeaderClient {
   public:
    // For usual requests
    InProgressRequest(
        ProxyingURLLoaderFactory* factory,
        mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory,
        uint64_t web_request_id,
        int32_t frame_routing_id,
        int32_t network_service_request_id,
        uint32_t options,
        const network::ResourceRequest& request,
        const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
        mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
        mojo::PendingRemote<network::mojom::URLLoaderClient> client);
    // For CORS preflights
    InProgressRequest(ProxyingURLLoaderFactory* factory,
                      uint64_t request_id,
                      int32_t frame_routing_id,
                      const network::ResourceRequest& request);
    ~InProgressRequest() override;

    // disable copy
    InProgressRequest(const InProgressRequest&) = delete;
    InProgressRequest& operator=(const InProgressRequest&) = delete;

    void Restart();

    // network::mojom::URLLoader:
    void FollowRedirect(
        const std::vector<std::string>& removed_headers,
        const net::HttpRequestHeaders& modified_headers,
        const net::HttpRequestHeaders& modified_cors_exempt_headers,
        const std::optional<GURL>& new_url) override;
    void SetPriority(net::RequestPriority priority,
                     int32_t intra_priority_value) override;

    // network::mojom::URLLoaderClient:
    void OnReceiveEarlyHints(
        network::mojom::EarlyHintsPtr early_hints) override;
    void OnReceiveResponse(
        network::mojom::URLResponseHeadPtr head,
        mojo::ScopedDataPipeConsumerHandle body,
        std::optional<mojo_base::BigBuffer> cached_metadata) override;
    void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                           network::mojom::URLResponseHeadPtr head) override;
    void OnUploadProgress(int64_t current_position,
                          int64_t total_size,
                          OnUploadProgressCallback callback) override;
    void OnTransferSizeUpdated(int32_t transfer_size_diff) override;
    void OnComplete(const network::URLLoaderCompletionStatus& status) override;

    void OnLoaderCreated(
        mojo::PendingReceiver<network::mojom::TrustedHeaderClient> receiver);

    // network::mojom::TrustedHeaderClient:
    void OnBeforeSendHeaders(const net::HttpRequestHeaders& headers,
                             OnBeforeSendHeadersCallback callback) override;
    void OnHeadersReceived(const std::string& headers,
                           const net::IPEndPoint& endpoint,
                           OnHeadersReceivedCallback callback) override;

   private:
    // These two methods combined form the implementation of Restart().
    void UpdateRequestInfo();
    void RestartInternal();

    void ContinueToBeforeSendHeaders(int error_code);
    void ContinueToSendHeaders(const std::set<std::string>& removed_headers,
                               const std::set<std::string>& set_headers,
                               int error_code);
    void ContinueToStartRequest(int error_code);
    void ContinueToHandleOverrideHeaders(int error_code);
    void ContinueToResponseStarted(int error_code);
    void ContinueToBeforeRedirect(const net::RedirectInfo& redirect_info,
                                  int error_code);
    void HandleResponseOrRedirectHeaders(
        net::CompletionOnceCallback continuation);
    void OnRequestError(const network::URLLoaderCompletionStatus& status);
    void HandleBeforeRequestRedirect();

    raw_ptr<ProxyingURLLoaderFactory> const factory_;
    mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
    network::ResourceRequest request_;
    const std::optional<url::Origin> original_initiator_;
    const uint64_t request_id_ = 0;
    const int32_t network_service_request_id_ = 0;
    const int32_t frame_routing_id_ = MSG_ROUTING_NONE;
    const uint32_t options_ = 0;
    const net::MutableNetworkTrafficAnnotationTag traffic_annotation_;
    mojo::Receiver<network::mojom::URLLoader> proxied_loader_receiver_;
    mojo::Remote<network::mojom::URLLoaderClient> target_client_;

    std::optional<extensions::WebRequestInfo> info_;

    mojo::Receiver<network::mojom::URLLoaderClient> proxied_client_receiver_{
        this};
    mojo::Remote<network::mojom::URLLoader> target_loader_;

    network::mojom::URLResponseHeadPtr current_response_;
    mojo::ScopedDataPipeConsumerHandle current_body_;
    scoped_refptr<net::HttpResponseHeaders> override_headers_;
    GURL redirect_url_;

    const bool for_cors_preflight_ = false;

    // If |has_any_extra_headers_listeners_| is set to true, the request will be
    // sent with the network::mojom::kURLLoadOptionUseHeaderClient option, and
    // we expect events to come through the
    // network::mojom::TrustedURLLoaderHeaderClient binding on the factory. This
    // is only set to true if there is a listener that needs to view or modify
    // headers set in the network process.
    bool has_any_extra_headers_listeners_ = false;
    bool current_request_uses_header_client_ = false;
    OnBeforeSendHeadersCallback on_before_send_headers_callback_;
    OnHeadersReceivedCallback on_headers_received_callback_;
    mojo::Receiver<network::mojom::TrustedHeaderClient> header_client_receiver_{
        this};

    // If |has_any_extra_headers_listeners_| is set to false and a redirect is
    // in progress, this stores the parameters to FollowRedirect that came from
    // the client. That way we can combine it with any other changes that
    // extensions made to headers in their callbacks.
    struct FollowRedirectParams {
      FollowRedirectParams();
      ~FollowRedirectParams();
      std::vector<std::string> removed_headers;
      net::HttpRequestHeaders modified_headers;
      net::HttpRequestHeaders modified_cors_exempt_headers;
      std::optional<GURL> new_url;

      // disable copy
      FollowRedirectParams(const FollowRedirectParams&) = delete;
      FollowRedirectParams& operator=(const FollowRedirectParams&) = delete;
    };
    std::unique_ptr<FollowRedirectParams> pending_follow_redirect_params_;

    std::optional<mojo_base::BigBuffer> current_cached_metadata_;

    base::WeakPtrFactory<InProgressRequest> weak_factory_{this};
  };

  ProxyingURLLoaderFactory(
      WebRequestAPI* web_request_api,
      const HandlersMap& intercepted_handlers,
      int render_process_id,
      int frame_routing_id,
      uint64_t* request_id_generator,
      std::unique_ptr<extensions::ExtensionNavigationUIData> navigation_ui_data,
      std::optional<int64_t> navigation_id,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_request,
      mojo::PendingRemote<network::mojom::URLLoaderFactory>
          target_factory_remote,
      mojo::PendingReceiver<network::mojom::TrustedURLLoaderHeaderClient>
          header_client_receiver,
      content::ContentBrowserClient::URLLoaderFactoryType loader_factory_type);

  ~ProxyingURLLoaderFactory() override;

  // disable copy
  ProxyingURLLoaderFactory(const ProxyingURLLoaderFactory&) = delete;
  ProxyingURLLoaderFactory& operator=(const ProxyingURLLoaderFactory&) = delete;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory>
                 loader_receiver) override;

  // network::mojom::TrustedURLLoaderHeaderClient:
  void OnLoaderCreated(
      int32_t request_id,
      mojo::PendingReceiver<network::mojom::TrustedHeaderClient> receiver)
      override;
  void OnLoaderForCorsPreflightCreated(
      const network::ResourceRequest& request,
      mojo::PendingReceiver<network::mojom::TrustedHeaderClient> receiver)
      override;

  WebRequestAPI* web_request_api() { return web_request_api_; }

  bool IsForServiceWorkerScript() const;

 private:
  void OnTargetFactoryError();
  void OnProxyBindingError();
  void RemoveRequest(int32_t network_service_request_id, uint64_t request_id);
  void MaybeDeleteThis();

  bool ShouldIgnoreConnectionsLimit(const network::ResourceRequest& request);

  // Passed from api::WebRequest.
  raw_ptr<WebRequestAPI> web_request_api_;

  // This is passed from api::Protocol.
  //
  // The Protocol instance lives through the lifetime of BrowserContext,
  // which is guaranteed to cover the lifetime of URLLoaderFactory, so the
  // reference is guaranteed to be valid.
  //
  // In this way we can avoid using code from api namespace in this file.
  const raw_ref<const HandlersMap> intercepted_handlers_;

  const int render_process_id_;
  const int frame_routing_id_;
  raw_ptr<uint64_t> request_id_generator_;  // managed by ElectronBrowserClient
  std::unique_ptr<extensions::ExtensionNavigationUIData> navigation_ui_data_;
  std::optional<int64_t> navigation_id_;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> proxy_receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
  mojo::Receiver<network::mojom::TrustedURLLoaderHeaderClient>
      url_loader_header_client_receiver_{this};
  const content::ContentBrowserClient::URLLoaderFactoryType
      loader_factory_type_;

  // Mapping from our own internally generated request ID to an
  // InProgressRequest instance.
  base::flat_map<uint64_t, std::unique_ptr<InProgressRequest>> requests_;

  // A mapping from the network stack's notion of request ID to our own
  // internally generated request ID for the same request.
  base::flat_map<int32_t, uint64_t> network_request_id_to_web_request_id_;

  std::vector<std::string> ignore_connections_limit_domains_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NET_PROXYING_URL_LOADER_FACTORY_H_
