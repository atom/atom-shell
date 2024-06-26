// Copyright (c) 2017 Amaplex Software, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/mac/in_app_purchase.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/sys_string_conversions.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

#import <CommonCrypto/CommonCrypto.h>
#import <StoreKit/StoreKit.h>

// ============================================================================
//                             InAppPurchase
// ============================================================================

// --------------------------------- Interface --------------------------------

@interface InAppPurchase : NSObject <SKProductsRequestDelegate> {
 @private
  in_app_purchase::InAppPurchaseCallback callback_;
  NSInteger quantity_;
  NSString* username_;
  InAppPurchase __strong* self_;
}

- (id)initWithCallback:(in_app_purchase::InAppPurchaseCallback)callback
              quantity:(NSInteger)quantity
              username:(NSString*)username;

- (void)purchaseProduct:(NSString*)productID;

@end

// ------------------------------- Implementation -----------------------------

@implementation InAppPurchase

/**
 * Init with a callback.
 *
 * @param callback - The callback that will be called when the payment is added
 * to the queue.
 */
- (id)initWithCallback:(in_app_purchase::InAppPurchaseCallback)callback
              quantity:(NSInteger)quantity
              username:(NSString*)username {
  if ((self = [super init])) {
    callback_ = std::move(callback);
    quantity_ = quantity;
    username_ = [username copy];
    self_ = self;
  }

  return self;
}

/**
 * Start the in-app purchase process.
 *
 * @param productID - The id of the product to purchase (the id of
 * com.example.app.product1 is product1).
 */
- (void)purchaseProduct:(NSString*)productID {
  // Retrieve the product information. (The products request retrieves,
  // information about valid products along with a list of the invalid product
  // identifiers, and then calls its delegate to process the result).
  SKProductsRequest* productsRequest;
  productsRequest = [[SKProductsRequest alloc]
      initWithProductIdentifiers:[NSSet setWithObject:productID]];

  productsRequest.delegate = self;
  [productsRequest start];
}

/**
 * Process product information and start the payment.
 *
 * @param request - The product request.
 * @param response - The information about the list of products.
 */
- (void)productsRequest:(SKProductsRequest*)request
     didReceiveResponse:(SKProductsResponse*)response {
  // Get the first product.
  NSArray* products = response.products;
  SKProduct* product = [products count] == 1 ? [products firstObject] : nil;

  // Return if the product is not found or invalid.
  if (product == nil) {
    [self runCallback:false];
    self_ = nil;
    return;
  }

  // Start the payment process.
  [self checkout:product];
}

/**
 * Submit a payment request to the App Store.
 *
 * @param product - The product to purchase.
 */
- (void)checkout:(SKProduct*)product {
  // Add the payment to the transaction queue. (The observer will be called
  // when the transaction is finished).
  SKMutablePayment* payment = [SKMutablePayment paymentWithProduct:product];
  payment.quantity = quantity_;
  payment.applicationUsername = username_;

  [[SKPaymentQueue defaultQueue] addPayment:payment];

  // Notify that the payment has been added to the queue with success.
  [self runCallback:true];
  self_ = nil;
}

/**
 * Submit a payment request to the App Store.
 *
 * @param product - The product to purchase.
 */
- (void)runCallback:(bool)isProductValid {
  if (callback_) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback_), isProductValid));
  }
}

@end

// ============================================================================
//                             C++ in_app_purchase
// ============================================================================

namespace in_app_purchase {

bool CanMakePayments() {
  return [SKPaymentQueue canMakePayments];
}

void RestoreCompletedTransactions() {
  [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
}

void FinishAllTransactions() {
  for (SKPaymentTransaction* transaction in SKPaymentQueue.defaultQueue
           .transactions) {
    [[SKPaymentQueue defaultQueue] finishTransaction:transaction];
  }
}

void FinishTransactionByDate(const std::string& date) {
  // Create the date formatter.
  NSDateFormatter* dateFormatter = [[NSDateFormatter alloc] init];
  NSLocale* enUSPOSIXLocale =
      [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
  [dateFormatter setLocale:enUSPOSIXLocale];
  [dateFormatter setDateFormat:@"yyyy-MM-dd'T'HH:mm:ssZZZZZ"];

  // Remove the transaction.
  NSString* transactionDate = base::SysUTF8ToNSString(date);

  for (SKPaymentTransaction* transaction in SKPaymentQueue.defaultQueue
           .transactions) {
    if ([transactionDate
            isEqualToString:[dateFormatter
                                stringFromDate:transaction.transactionDate]]) {
      [[SKPaymentQueue defaultQueue] finishTransaction:transaction];
    }
  }
}

std::string GetReceiptURL() {
  NSURL* receiptURL = [[NSBundle mainBundle] appStoreReceiptURL];
  if (receiptURL != nil) {
    return std::string([[receiptURL path] UTF8String]);
  } else {
    return "";
  }
}

void PurchaseProduct(const std::string& productID,
                     int quantity,
                     const std::string& username,
                     InAppPurchaseCallback callback) {
  auto* iap = [[InAppPurchase alloc]
      initWithCallback:std::move(callback)
              quantity:quantity
              username:base::SysUTF8ToNSString(username)];

  [iap purchaseProduct:base::SysUTF8ToNSString(productID)];
}

}  // namespace in_app_purchase
