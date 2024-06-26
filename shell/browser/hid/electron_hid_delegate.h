// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_HID_ELECTRON_HID_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_HID_ELECTRON_HID_DELEGATE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/containers/flat_map.h"
#include "content/public/browser/hid_chooser.h"
#include "content/public/browser/hid_delegate.h"
#include "services/device/public/mojom/hid.mojom-forward.h"
#include "shell/browser/hid/hid_chooser_context.h"
#include "third_party/blink/public/mojom/hid/hid.mojom-forward.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace electron {

class HidChooserController;

class ElectronHidDelegate : public content::HidDelegate {
 public:
  ElectronHidDelegate();
  ElectronHidDelegate(ElectronHidDelegate&) = delete;
  ElectronHidDelegate& operator=(ElectronHidDelegate&) = delete;
  ~ElectronHidDelegate() override;

  // content::HidDelegate:
  std::unique_ptr<content::HidChooser> RunChooser(
      content::RenderFrameHost* render_frame_host,
      std::vector<blink::mojom::HidDeviceFilterPtr> filters,
      std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
      content::HidChooser::Callback callback) override;
  bool CanRequestDevicePermission(content::BrowserContext* browser_context,
                                  const url::Origin& origin) override;
  bool HasDevicePermission(content::BrowserContext* browser_context,
                           content::RenderFrameHost* render_frame_host,
                           const url::Origin& origin,
                           const device::mojom::HidDeviceInfo& device) override;
  void RevokeDevicePermission(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* render_frame_host,
      const url::Origin& origin,
      const device::mojom::HidDeviceInfo& device) override;
  device::mojom::HidManager* GetHidManager(
      content::BrowserContext* browser_context) override;
  void AddObserver(content::BrowserContext* browser_context,
                   content::HidDelegate::Observer* observer) override;
  void RemoveObserver(content::BrowserContext* browser_context,
                      content::HidDelegate::Observer* observer) override;
  const device::mojom::HidDeviceInfo* GetDeviceInfo(
      content::BrowserContext* browser_context,
      const std::string& guid) override;
  bool IsFidoAllowedForOrigin(content::BrowserContext* browser_context,
                              const url::Origin& origin) override;
  bool IsServiceWorkerAllowedForOrigin(const url::Origin& origin) override;
  void IncrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override {}
  void DecrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override {}

  void DeleteControllerForFrame(content::RenderFrameHost* render_frame_host);

 private:
  class ContextObservation;

  ContextObservation* GetContextObserver(
      content::BrowserContext* browser_context);

  base::flat_map<content::BrowserContext*, std::unique_ptr<ContextObservation>>
      observations_;

  HidChooserController* ControllerForFrame(
      content::RenderFrameHost* render_frame_host);

  HidChooserController* AddControllerForFrame(
      content::RenderFrameHost* render_frame_host,
      std::vector<blink::mojom::HidDeviceFilterPtr> filters,
      std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
      content::HidChooser::Callback callback);

  std::unordered_map<content::RenderFrameHost*,
                     std::unique_ptr<HidChooserController>>
      controller_map_;

  base::WeakPtrFactory<ElectronHidDelegate> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_HID_ELECTRON_HID_DELEGATE_H_
