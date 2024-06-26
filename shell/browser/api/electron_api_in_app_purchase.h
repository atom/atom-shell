// Copyright (c) 2017 Amaplex Software, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_IN_APP_PURCHASE_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_IN_APP_PURCHASE_H_

#include <string>
#include <vector>

#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/browser/mac/in_app_purchase.h"
#include "shell/browser/mac/in_app_purchase_observer.h"
#include "shell/browser/mac/in_app_purchase_product.h"
#include "v8/include/v8.h"

namespace electron::api {

class InAppPurchase : public gin::Wrappable<InAppPurchase>,
                      public gin_helper::EventEmitterMixin<InAppPurchase>,
                      private in_app_purchase::TransactionObserver {
 public:
  static gin::Handle<InAppPurchase> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  InAppPurchase(const InAppPurchase&) = delete;
  InAppPurchase& operator=(const InAppPurchase&) = delete;

 protected:
  InAppPurchase();
  ~InAppPurchase() override;

  v8::Local<v8::Promise> PurchaseProduct(const std::string& product_id,
                                         gin::Arguments* args);

  v8::Local<v8::Promise> GetProducts(const std::vector<std::string>& productIDs,
                                     gin::Arguments* args);

  // TransactionObserver:
  void OnTransactionsUpdated(
      const std::vector<in_app_purchase::Transaction>& transactions) override;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_IN_APP_PURCHASE_H_
