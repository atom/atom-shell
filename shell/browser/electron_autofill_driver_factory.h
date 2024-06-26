// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_AUTOFILL_DRIVER_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_AUTOFILL_DRIVER_FACTORY_H_

#include <memory>
#include <unordered_map>

#include "base/functional/callback_forward.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "shell/common/api/api.mojom.h"

namespace electron {

class AutofillDriver;

class AutofillDriverFactory
    : private content::WebContentsObserver,
      public content::WebContentsUserData<AutofillDriverFactory> {
 public:
  typedef base::OnceCallback<std::unique_ptr<AutofillDriver>()>
      CreationCallback;

  ~AutofillDriverFactory() override;

  static void BindAutofillDriver(
      mojo::PendingAssociatedReceiver<mojom::ElectronAutofillDriver>
          pending_receiver,
      content::RenderFrameHost* render_frame_host);

  AutofillDriver* DriverForFrame(content::RenderFrameHost* render_frame_host);
  void AddDriverForFrame(content::RenderFrameHost* render_frame_host,
                         CreationCallback factory_method);
  void DeleteDriverForFrame(content::RenderFrameHost* render_frame_host);

  void CloseAllPopups();

  WEB_CONTENTS_USER_DATA_KEY_DECL();

 private:
  // content::WebContentsObserver:
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  explicit AutofillDriverFactory(content::WebContents* web_contents);
  friend class content::WebContentsUserData<AutofillDriverFactory>;

  std::unordered_map<content::RenderFrameHost*, std::unique_ptr<AutofillDriver>>
      driver_map_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_AUTOFILL_DRIVER_FACTORY_H_
