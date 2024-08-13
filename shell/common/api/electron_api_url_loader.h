// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_URL_LOADER_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_URL_LOADER_H_

#include <memory>
#include <string>
#include <vector>

#include <string_view>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom-forward.h"
#include "services/network/public/mojom/url_loader_network_service_observer.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "shell/browser/event_emitter_mixin.h"
#include "url/gurl.h"
#include "v8/include/v8-forward.h"

namespace gin {
class Arguments;
template <typename T>
class Handle;
}  // namespace gin

namespace net {
class AuthChallengeInfo;
}  // namespace net

namespace network {
class SimpleURLLoader;
struct ResourceRequest;
class SharedURLLoaderFactory;
}  // namespace network

namespace electron {
class ElectronBrowserContext;
}

namespace electron::api {

/** Wraps a SimpleURLLoader to make it usable from JavaScript */
class SimpleURLLoaderWrapper
    : public gin::Wrappable<SimpleURLLoaderWrapper>,
      public gin_helper::EventEmitterMixin<SimpleURLLoaderWrapper>,
      private network::SimpleURLLoaderStreamConsumer,
      private network::mojom::URLLoaderNetworkServiceObserver {
 public:
  ~SimpleURLLoaderWrapper() override;
  static gin::Handle<SimpleURLLoaderWrapper> Create(gin::Arguments* args);

  void Cancel();

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

 private:
  SimpleURLLoaderWrapper(ElectronBrowserContext* browser_context,
                         std::unique_ptr<network::ResourceRequest> request,
                         int options);

  // SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view string_view,
                      base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override {}

  // network::mojom::URLLoaderNetworkServiceObserver:
  void OnAuthRequired(
      const std::optional<base::UnguessableToken>& window_id,
      int32_t request_id,
      const GURL& url,
      bool first_auth_attempt,
      const net::AuthChallengeInfo& auth_info,
      const scoped_refptr<net::HttpResponseHeaders>& head_headers,
      mojo::PendingRemote<network::mojom::AuthChallengeResponder>
          auth_challenge_responder) override;
  void OnSSLCertificateError(const GURL& url,
                             int net_error,
                             const net::SSLInfo& ssl_info,
                             bool fatal,
                             OnSSLCertificateErrorCallback response) override;
  void OnCertificateRequested(
      const std::optional<base::UnguessableToken>& window_id,
      const scoped_refptr<net::SSLCertRequestInfo>& cert_info,
      mojo::PendingRemote<network::mojom::ClientCertificateResponder>
          client_cert_responder) override {}
  void OnPrivateNetworkAccessPermissionRequired(
      const GURL& url,
      const net::IPAddress& ip_address,
      const std::optional<std::string>& private_network_device_id,
      const std::optional<std::string>& private_network_device_name,
      OnPrivateNetworkAccessPermissionRequiredCallback callback) override {}
  void OnClearSiteData(
      const GURL& url,
      const std::string& header_value,
      int32_t load_flags,
      const std::optional<net::CookiePartitionKey>& cookie_partition_key,
      bool partitioned_state_allowed_only,
      OnClearSiteDataCallback callback) override;
  void OnLoadingStateUpdate(network::mojom::LoadInfoPtr info,
                            OnLoadingStateUpdateCallback callback) override;
  void OnSharedStorageHeaderReceived(
      const url::Origin& request_origin,
      std::vector<network::mojom::SharedStorageOperationPtr> operations,
      OnSharedStorageHeaderReceivedCallback callback) override;
  void OnDataUseUpdate(int32_t network_traffic_annotation_id_hash,
                       int64_t recv_bytes,
                       int64_t sent_bytes) override {}
  void OnWebSocketConnectedToPrivateNetwork(
      network::mojom::IPAddressSpace ip_address_space) override {}
  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderNetworkServiceObserver>
          observer) override;

  scoped_refptr<network::SharedURLLoaderFactory> GetURLLoaderFactoryForURL(
      const GURL& url);

  // SimpleURLLoader callbacks
  void OnResponseStarted(const GURL& final_url,
                         const network::mojom::URLResponseHead& response_head);
  void OnRedirect(const GURL& url_before_redirect,
                  const net::RedirectInfo& redirect_info,
                  const network::mojom::URLResponseHead& response_head,
                  std::vector<std::string>* removed_headers);
  void OnUploadProgress(uint64_t position, uint64_t total);
  void OnDownloadProgress(uint64_t current);

  void Start();
  void Pin();
  void PinBodyGetter(v8::Local<v8::Value>);

  SEQUENCE_CHECKER(sequence_checker_);
  raw_ptr<ElectronBrowserContext> browser_context_;
  int request_options_;
  std::unique_ptr<network::ResourceRequest> request_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  v8::Global<v8::Value> pinned_wrapper_;
  v8::Global<v8::Value> pinned_chunk_pipe_getter_;

  mojo::ReceiverSet<network::mojom::URLLoaderNetworkServiceObserver>
      url_loader_network_observer_receivers_;
  base::WeakPtrFactory<SimpleURLLoaderWrapper> weak_factory_{this};
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_URL_LOADER_H_
