// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_AUTOFILL_DRIVER_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_AUTOFILL_DRIVER_H_

#include <memory>
#include <vector>

#if defined(TOOLKIT_VIEWS)
#include "shell/browser/ui/autofill_popup.h"
#endif

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "shell/common/api/api.mojom.h"

namespace electron {

class AutofillDriver : public mojom::ElectronAutofillDriver {
 public:
  explicit AutofillDriver(content::RenderFrameHost* render_frame_host);
  AutofillDriver(const AutofillDriver&) = delete;
  AutofillDriver& operator=(const AutofillDriver&) = delete;
  ~AutofillDriver() override;

  void BindPendingReceiver(
      mojo::PendingAssociatedReceiver<mojom::ElectronAutofillDriver>
          pending_receiver);

  // mojom::ElectronAutofillDriver
  void ShowAutofillPopup(const gfx::RectF& bounds,
                         const std::vector<std::u16string>& values,
                         const std::vector<std::u16string>& labels) override;
  void HideAutofillPopup() override;

 private:
  raw_ptr<content::RenderFrameHost> const render_frame_host_;

#if defined(TOOLKIT_VIEWS)
  std::unique_ptr<AutofillPopup> autofill_popup_;
#endif

  mojo::AssociatedReceiver<mojom::ElectronAutofillDriver> receiver_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_AUTOFILL_DRIVER_H_
