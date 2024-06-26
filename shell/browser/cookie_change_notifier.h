// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_COOKIE_CHANGE_NOTIFIER_H_
#define ELECTRON_SHELL_BROWSER_COOKIE_CHANGE_NOTIFIER_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/cookies/cookie_change_dispatcher.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace electron {

class ElectronBrowserContext;

// Sends cookie-change notifications on the UI thread.
class CookieChangeNotifier : private network::mojom::CookieChangeListener {
 public:
  explicit CookieChangeNotifier(ElectronBrowserContext* browser_context);
  ~CookieChangeNotifier() override;

  // disable copy
  CookieChangeNotifier(const CookieChangeNotifier&) = delete;
  CookieChangeNotifier& operator=(const CookieChangeNotifier&) = delete;

  // Register callbacks that needs to notified on any cookie store changes.
  base::CallbackListSubscription RegisterCookieChangeCallback(
      const base::RepeatingCallback<void(const net::CookieChangeInfo& change)>&
          cb);

 private:
  void StartListening();
  void OnConnectionError();

  // network::mojom::CookieChangeListener implementation.
  void OnCookieChange(const net::CookieChangeInfo& change) override;

  raw_ptr<ElectronBrowserContext> browser_context_;
  base::RepeatingCallbackList<void(const net::CookieChangeInfo& change)>
      cookie_change_sub_list_;

  mojo::Receiver<network::mojom::CookieChangeListener> receiver_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_COOKIE_CHANGE_NOTIFIER_H_
