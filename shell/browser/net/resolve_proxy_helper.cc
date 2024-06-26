// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/net/resolve_proxy_helper.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_thread.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/base/network_anonymization_key.h"
#include "net/proxy_resolution/proxy_info.h"

using content::BrowserThread;

namespace electron {

ResolveProxyHelper::ResolveProxyHelper(
    network::mojom::NetworkContext* network_context)
    : network_context_(network_context) {}

ResolveProxyHelper::~ResolveProxyHelper() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!owned_self_);
  DCHECK(!receiver_.is_bound());
  // Clear all pending requests if the ProxyService is still alive.
  pending_requests_.clear();
}

void ResolveProxyHelper::ResolveProxy(const GURL& url,
                                      ResolveProxyCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Enqueue the pending request.
  pending_requests_.emplace_back(url, std::move(callback));

  // If nothing is in progress, start.
  if (!receiver_.is_bound()) {
    DCHECK_EQ(1u, pending_requests_.size());
    StartPendingRequest();
  }
}

void ResolveProxyHelper::StartPendingRequest() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!receiver_.is_bound());
  DCHECK(!pending_requests_.empty());

  // Start the request.
  mojo::PendingRemote<network::mojom::ProxyLookupClient> proxy_lookup_client =
      receiver_.BindNewPipeAndPassRemote();
  receiver_.set_disconnect_handler(
      base::BindOnce(&ResolveProxyHelper::OnProxyLookupComplete,
                     base::Unretained(this), net::ERR_ABORTED, std::nullopt));
  network_context_->LookUpProxyForURL(pending_requests_.front().url,
                                      net::NetworkAnonymizationKey(),
                                      std::move(proxy_lookup_client));
}

void ResolveProxyHelper::OnProxyLookupComplete(
    int32_t net_error,
    const std::optional<net::ProxyInfo>& proxy_info) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!pending_requests_.empty());

  receiver_.reset();

  // Clear the current (completed) request.
  PendingRequest completed_request = std::move(pending_requests_.front());
  pending_requests_.pop_front();

  std::string proxy;
  if (proxy_info)
    proxy = proxy_info->ToPacString();

  if (!completed_request.callback.is_null())
    std::move(completed_request.callback).Run(proxy);

  // Start the next request.
  if (!pending_requests_.empty())
    StartPendingRequest();
}

ResolveProxyHelper::PendingRequest::PendingRequest(
    const GURL& url,
    ResolveProxyCallback callback)
    : url(url), callback(std::move(callback)) {}

ResolveProxyHelper::PendingRequest::PendingRequest(
    ResolveProxyHelper::PendingRequest&& pending_request) noexcept = default;

ResolveProxyHelper::PendingRequest::~PendingRequest() noexcept = default;

ResolveProxyHelper::PendingRequest&
ResolveProxyHelper::PendingRequest::operator=(
    ResolveProxyHelper::PendingRequest&& pending_request) noexcept = default;

}  // namespace electron
