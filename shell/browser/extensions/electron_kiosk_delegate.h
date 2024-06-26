// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_KIOSK_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_KIOSK_DELEGATE_H_

#include "extensions/browser/kiosk/kiosk_delegate.h"
#include "extensions/common/extension_id.h"

namespace electron {

// Delegate in Electron that provides an extension/app API with Kiosk mode
// functionality.
class ElectronKioskDelegate : public extensions::KioskDelegate {
 public:
  ElectronKioskDelegate();
  ~ElectronKioskDelegate() override;

  // extensions::KioskDelegate
  bool IsAutoLaunchedKioskApp(const extensions::ExtensionId& id) const override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_KIOSK_DELEGATE_H_
