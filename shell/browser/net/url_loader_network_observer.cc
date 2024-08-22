// Copyright (c) 2024 Microsoft, GmbH
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/net/url_loader_network_observer.h"

#include "base/functional/bind.h"
#include "content/public/browser/browser_thread.h"
#include "shell/browser/login_handler.h"

namespace electron {

namespace {

class LoginHandlerDelegate {
 public:
  LoginHandlerDelegate(
      mojo::PendingRemote<network::mojom::AuthChallengeResponder>
          auth_challenge_responder,
      const net::AuthChallengeInfo& auth_info,
      const GURL& url,
      scoped_refptr<net::HttpResponseHeaders> response_headers,
      base::ProcessId process_id,
      bool first_auth_attempt)
      : auth_challenge_responder_(std::move(auth_challenge_responder)) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    auth_challenge_responder_.set_disconnect_handler(base::BindOnce(
        &LoginHandlerDelegate::OnRequestCancelled, base::Unretained(this)));

    login_handler_ = std::make_unique<LoginHandler>(
        auth_info, nullptr, false, process_id, url, response_headers,
        first_auth_attempt,
        base::BindOnce(&LoginHandlerDelegate::OnAuthCredentials,
                       weak_factory_.GetWeakPtr()));
  }

 private:
  void OnRequestCancelled() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    delete this;
  }

  void OnAuthCredentials(
      const std::optional<net::AuthCredentials>& auth_credentials) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    auth_challenge_responder_->OnAuthCredentials(auth_credentials);
    delete this;
  }

  mojo::Remote<network::mojom::AuthChallengeResponder>
      auth_challenge_responder_;
  std::unique_ptr<LoginHandler> login_handler_;
  base::WeakPtrFactory<LoginHandlerDelegate> weak_factory_{this};
};

}  // namespace

URLLoaderNetworkObserver::URLLoaderNetworkObserver() = default;
URLLoaderNetworkObserver::~URLLoaderNetworkObserver() = default;

mojo::PendingRemote<network::mojom::URLLoaderNetworkServiceObserver>
URLLoaderNetworkObserver::Bind() {
  mojo::PendingRemote<network::mojom::URLLoaderNetworkServiceObserver>
      pending_remote;
  receivers_.Add(this, pending_remote.InitWithNewPipeAndPassReceiver());
  return pending_remote;
}

void URLLoaderNetworkObserver::OnAuthRequired(
    const std::optional<base::UnguessableToken>& window_id,
    int32_t request_id,
    const GURL& url,
    bool first_auth_attempt,
    const net::AuthChallengeInfo& auth_info,
    const scoped_refptr<net::HttpResponseHeaders>& head_headers,
    mojo::PendingRemote<network::mojom::AuthChallengeResponder>
        auth_challenge_responder) {
  new LoginHandlerDelegate(std::move(auth_challenge_responder), auth_info, url,
                           head_headers, process_id_, first_auth_attempt);
}

void URLLoaderNetworkObserver::OnSSLCertificateError(
    const GURL& url,
    int net_error,
    const net::SSLInfo& ssl_info,
    bool fatal,
    OnSSLCertificateErrorCallback response) {
  std::move(response).Run(net_error);
}

void URLLoaderNetworkObserver::OnClearSiteData(
    const GURL& url,
    const std::string& header_value,
    int32_t load_flags,
    const std::optional<net::CookiePartitionKey>& cookie_partition_key,
    bool partitioned_state_allowed_only,
    OnClearSiteDataCallback callback) {
  std::move(callback).Run();
}

void URLLoaderNetworkObserver::OnLoadingStateUpdate(
    network::mojom::LoadInfoPtr info,
    OnLoadingStateUpdateCallback callback) {
  std::move(callback).Run();
}

void URLLoaderNetworkObserver::OnSharedStorageHeaderReceived(
    const url::Origin& request_origin,
    std::vector<network::mojom::SharedStorageOperationPtr> operations,
    OnSharedStorageHeaderReceivedCallback callback) {
  std::move(callback).Run();
}

void URLLoaderNetworkObserver::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderNetworkServiceObserver>
        observer) {
  receivers_.Add(this, std::move(observer));
}

}  // namespace electron
