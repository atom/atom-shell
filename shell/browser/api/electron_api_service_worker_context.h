// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_SERVICE_WORKER_CONTEXT_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_SERVICE_WORKER_CONTEXT_H_

#include "base/memory/raw_ptr.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/service_worker_context_observer.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"

namespace electron {

class ElectronBrowserContext;

namespace api {

class ServiceWorkerContext
    : public gin::Wrappable<ServiceWorkerContext>,
      public gin_helper::EventEmitterMixin<ServiceWorkerContext>,
      private content::ServiceWorkerContextObserver {
 public:
  static gin::Handle<ServiceWorkerContext> Create(
      v8::Isolate* isolate,
      ElectronBrowserContext* browser_context);

  v8::Local<v8::Value> GetAllRunningWorkerInfo(v8::Isolate* isolate);
  v8::Local<v8::Value> GetWorkerInfoFromID(gin_helper::ErrorThrower thrower,
                                           int64_t version_id);

  // content::ServiceWorkerContextObserver
  void OnReportConsoleMessage(int64_t version_id,
                              const GURL& scope,
                              const content::ConsoleMessage& message) override;
  void OnRegistrationCompleted(const GURL& scope) override;
  void OnDestruct(content::ServiceWorkerContext* context) override;

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  ServiceWorkerContext(const ServiceWorkerContext&) = delete;
  ServiceWorkerContext& operator=(const ServiceWorkerContext&) = delete;

 protected:
  explicit ServiceWorkerContext(v8::Isolate* isolate,
                                ElectronBrowserContext* browser_context);
  ~ServiceWorkerContext() override;

 private:
  raw_ptr<content::ServiceWorkerContext> service_worker_context_;

  base::WeakPtrFactory<ServiceWorkerContext> weak_ptr_factory_{this};
};

}  // namespace api

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_SERVICE_WORKER_CONTEXT_H_
