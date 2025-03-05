// Copyright (c) 2025 Salesforce, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_service_worker_main.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"  // nogncheck
#include "content/browser/service_worker/service_worker_version.h"  // nogncheck
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "shell/browser/api/message_port.h"
#include "shell/browser/browser.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/api/api.mojom.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_includes.h"
#include "shell/common/v8_util.h"

namespace {

// Use private API to get the live version of the service worker. This will
// exist while in starting, stopping, or stopped running status.
content::ServiceWorkerVersion* GetLiveVersion(
    content::ServiceWorkerContext* service_worker_context,
    int64_t version_id) {
  auto* wrapper = static_cast<content::ServiceWorkerContextWrapper*>(
      service_worker_context);
  return wrapper->GetLiveVersion(version_id);
}

// Get a public ServiceWorkerVersionBaseInfo object directly from the service
// worker.
std::optional<content::ServiceWorkerVersionBaseInfo> GetLiveVersionInfo(
    content::ServiceWorkerContext* service_worker_context,
    int64_t version_id) {
  auto* version = GetLiveVersion(service_worker_context, version_id);
  if (version) {
    return version->GetInfo();
  }
  return std::nullopt;
}

}  // namespace

namespace electron::api {

// ServiceWorkerKey -> ServiceWorkerMain*
typedef std::unordered_map<ServiceWorkerKey,
                           ServiceWorkerMain*,
                           ServiceWorkerKey::Hasher>
    VersionIdMap;

VersionIdMap& GetVersionIdMap() {
  static base::NoDestructor<VersionIdMap> instance;
  return *instance;
}

ServiceWorkerMain* FromServiceWorkerKey(const ServiceWorkerKey& key) {
  VersionIdMap& version_map = GetVersionIdMap();
  auto iter = version_map.find(key);
  auto* service_worker = iter == version_map.end() ? nullptr : iter->second;
  return service_worker;
}

// static
ServiceWorkerMain* ServiceWorkerMain::FromVersionID(
    int64_t version_id,
    const content::StoragePartition* storage_partition) {
  ServiceWorkerKey key(version_id, storage_partition);
  return FromServiceWorkerKey(key);
}

gin::WrapperInfo ServiceWorkerMain::kWrapperInfo = {gin::kEmbedderNativeGin};

ServiceWorkerMain::ServiceWorkerMain(content::ServiceWorkerContext* sw_context,
                                     int64_t version_id,
                                     const ServiceWorkerKey& key)
    : version_id_(version_id), key_(key), service_worker_context_(sw_context) {
  GetVersionIdMap().emplace(key_, this);
  InvalidateVersionInfo();
}

ServiceWorkerMain::~ServiceWorkerMain() {
  Destroy();
}

void ServiceWorkerMain::Destroy() {
  version_destroyed_ = true;
  InvalidateVersionInfo();
  MaybeDisconnectRemote();
  GetVersionIdMap().erase(key_);
  Unpin();
}

void ServiceWorkerMain::MaybeDisconnectRemote() {
  if (remote_.is_bound() &&
      (version_destroyed_ ||
       (!service_worker_context_->IsLiveStartingServiceWorker(version_id_) &&
        !service_worker_context_->IsLiveRunningServiceWorker(version_id_)))) {
    remote_.reset();
  }
}

mojom::ElectronRenderer* ServiceWorkerMain::GetRendererApi() {
  if (!remote_.is_bound()) {
    if (!service_worker_context_->IsLiveRunningServiceWorker(version_id_)) {
      return nullptr;
    }

    service_worker_context_->GetRemoteAssociatedInterfaces(version_id_)
        .GetInterface(&remote_);
  }
  return remote_.get();
}

void ServiceWorkerMain::Send(v8::Isolate* isolate,
                             bool internal,
                             const std::string& channel,
                             v8::Local<v8::Value> args) {
  blink::CloneableMessage message;
  if (!gin::ConvertFromV8(isolate, args, &message)) {
    isolate->ThrowException(v8::Exception::Error(
        gin::StringToV8(isolate, "Failed to serialize arguments")));
    return;
  }

  auto* renderer_api_remote = GetRendererApi();
  if (!renderer_api_remote) {
    return;
  }

  renderer_api_remote->Message(internal, channel, std::move(message));
}

void ServiceWorkerMain::InvalidateVersionInfo() {
  if (version_info_ != nullptr) {
    version_info_.reset();
  }

  if (version_destroyed_)
    return;

  auto version_info = GetLiveVersionInfo(service_worker_context_, version_id_);
  if (version_info) {
    version_info_ =
        std::make_unique<content::ServiceWorkerVersionBaseInfo>(*version_info);
  } else {
    // When ServiceWorkerContextCore::RemoveLiveVersion is called, it posts a
    // task to notify that the service worker has stopped. At this point, the
    // live version will no longer exist.
    Destroy();
  }
}

void ServiceWorkerMain::OnRunningStatusChanged(
    blink::EmbeddedWorkerStatus running_status) {
  // Disconnect remote when content::ServiceWorkerHost has terminated.
  MaybeDisconnectRemote();

  InvalidateVersionInfo();

  // Redundant worker has been marked for deletion. Now that it's stopped, let's
  // destroy our wrapper.
  if (redundant_ && running_status == blink::EmbeddedWorkerStatus::kStopped) {
    Destroy();
  }
}

void ServiceWorkerMain::OnVersionRedundant() {
  // Redundant service workers have been either unregistered or replaced. A new
  // ServiceWorkerMain will need to be created.
  // Set internal state to mark it for deletion once it has fully stopped.
  redundant_ = true;
}

bool ServiceWorkerMain::IsDestroyed() const {
  return version_destroyed_;
}

const blink::StorageKey ServiceWorkerMain::GetStorageKey() {
  const GURL& scope = version_info_ ? version_info()->scope : GURL::EmptyGURL();
  return blink::StorageKey::CreateFirstParty(url::Origin::Create(scope));
}

gin_helper::Dictionary ServiceWorkerMain::StartExternalRequest(
    v8::Isolate* isolate,
    bool has_timeout) {
  auto details = gin_helper::Dictionary::CreateEmpty(isolate);

  if (version_destroyed_) {
    isolate->ThrowException(v8::Exception::TypeError(
        gin::StringToV8(isolate, "ServiceWorkerMain is destroyed")));
    return details;
  }

  auto request_uuid = base::Uuid::GenerateRandomV4();
  auto timeout_type =
      has_timeout
          ? content::ServiceWorkerExternalRequestTimeoutType::kDefault
          : content::ServiceWorkerExternalRequestTimeoutType::kDoesNotTimeout;

  content::ServiceWorkerExternalRequestResult start_result =
      service_worker_context_->StartingExternalRequest(
          version_id_, timeout_type, request_uuid);

  details.Set("id", request_uuid.AsLowercaseString());
  details.Set("ok",
              start_result == content::ServiceWorkerExternalRequestResult::kOk);

  return details;
}

void ServiceWorkerMain::FinishExternalRequest(v8::Isolate* isolate,
                                              std::string uuid) {
  base::Uuid request_uuid = base::Uuid::ParseLowercase(uuid);
  if (!request_uuid.is_valid()) {
    isolate->ThrowException(v8::Exception::TypeError(
        gin::StringToV8(isolate, "Invalid external request UUID")));
    return;
  }

  DCHECK(service_worker_context_);
  if (!service_worker_context_)
    return;

  content::ServiceWorkerExternalRequestResult result =
      service_worker_context_->FinishedExternalRequest(version_id_,
                                                       request_uuid);

  std::string error;
  switch (result) {
    case content::ServiceWorkerExternalRequestResult::kOk:
      break;
    case content::ServiceWorkerExternalRequestResult::kBadRequestId:
      error = "Unknown external request UUID";
      break;
    case content::ServiceWorkerExternalRequestResult::kWorkerNotRunning:
      error = "Service worker is no longer running";
      break;
    case content::ServiceWorkerExternalRequestResult::kWorkerNotFound:
      error = "Service worker was not found";
      break;
    case content::ServiceWorkerExternalRequestResult::kNullContext:
    default:
      error = "Service worker context is unavailable and may be shutting down";
      break;
  }

  if (!error.empty()) {
    isolate->ThrowException(
        v8::Exception::TypeError(gin::StringToV8(isolate, error)));
  }
}

size_t ServiceWorkerMain::CountExternalRequestsForTest() {
  if (version_destroyed_)
    return 0;
  auto& storage_key = GetStorageKey();
  return service_worker_context_->CountExternalRequestsForTest(storage_key);
}

int64_t ServiceWorkerMain::VersionID() const {
  return version_id_;
}

GURL ServiceWorkerMain::ScopeURL() const {
  if (version_destroyed_)
    return {};
  return version_info()->scope;
}

GURL ServiceWorkerMain::ScriptURL() const {
  if (version_destroyed_)
    return {};
  return version_info()->script_url;
}

// static
gin::Handle<ServiceWorkerMain> ServiceWorkerMain::New(v8::Isolate* isolate) {
  return gin::Handle<ServiceWorkerMain>();
}

// static
gin::Handle<ServiceWorkerMain> ServiceWorkerMain::From(
    v8::Isolate* isolate,
    content::ServiceWorkerContext* sw_context,
    const content::StoragePartition* storage_partition,
    int64_t version_id) {
  ServiceWorkerKey service_worker_key(version_id, storage_partition);

  auto* service_worker = FromServiceWorkerKey(service_worker_key);
  if (service_worker)
    return gin::CreateHandle(isolate, service_worker);

  // Ensure ServiceWorkerVersion exists and is not redundant (pending deletion)
  auto* live_version = GetLiveVersion(sw_context, version_id);
  if (!live_version || live_version->is_redundant()) {
    return gin::Handle<ServiceWorkerMain>();
  }

  auto handle = gin::CreateHandle(
      isolate,
      new ServiceWorkerMain(sw_context, version_id, service_worker_key));

  // Prevent garbage collection of worker until it has been deleted internally.
  handle->Pin(isolate);

  return handle;
}

// static
void ServiceWorkerMain::FillObjectTemplate(
    v8::Isolate* isolate,
    v8::Local<v8::ObjectTemplate> templ) {
  gin_helper::ObjectTemplateBuilder(isolate, templ)
      .SetMethod("_send", &ServiceWorkerMain::Send)
      .SetMethod("isDestroyed", &ServiceWorkerMain::IsDestroyed)
      .SetMethod("_startExternalRequest",
                 &ServiceWorkerMain::StartExternalRequest)
      .SetMethod("_finishExternalRequest",
                 &ServiceWorkerMain::FinishExternalRequest)
      .SetMethod("_countExternalRequests",
                 &ServiceWorkerMain::CountExternalRequestsForTest)
      .SetProperty("versionId", &ServiceWorkerMain::VersionID)
      .SetProperty("scope", &ServiceWorkerMain::ScopeURL)
      .SetProperty("scriptURL", &ServiceWorkerMain::ScriptURL)
      .Build();
}

const char* ServiceWorkerMain::GetTypeName() {
  return GetClassName();
}

}  // namespace electron::api

namespace {

using electron::api::ServiceWorkerMain;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("ServiceWorkerMain", ServiceWorkerMain::GetConstructor(context));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_service_worker_main,
                                  Initialize)
