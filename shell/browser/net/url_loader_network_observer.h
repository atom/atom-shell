// Copyright (c) 2024 Microsoft, GmbH
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NET_URL_LOADER_NETWORK_OBSERVER_H_
#define ELECTRON_SHELL_BROWSER_NET_URL_LOADER_NETWORK_OBSERVER_H_

#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/network/public/mojom/url_loader_network_service_observer.mojom.h"

namespace electron {

class URLLoaderNetworkObserver
    : public network::mojom::URLLoaderNetworkServiceObserver {
 public:
  URLLoaderNetworkObserver();
  ~URLLoaderNetworkObserver() override;

  URLLoaderNetworkObserver(const URLLoaderNetworkObserver&) = delete;
  URLLoaderNetworkObserver& operator=(const URLLoaderNetworkObserver&) = delete;

  mojo::PendingRemote<network::mojom::URLLoaderNetworkServiceObserver> Bind();
  void set_process_id(base::ProcessId pid) { process_id_ = pid; }

 private:
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
  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderNetworkServiceObserver>
          observer) override;

  mojo::ReceiverSet<network::mojom::URLLoaderNetworkServiceObserver> receivers_;
  base::ProcessId process_id_ = base::kNullProcessId;
  base::WeakPtrFactory<URLLoaderNetworkObserver> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_NET_URL_LOADER_NETWORK_OBSERVER_H_
