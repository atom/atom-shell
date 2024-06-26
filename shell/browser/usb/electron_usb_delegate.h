// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_USB_ELECTRON_USB_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_USB_ELECTRON_USB_DELEGATE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/usb_chooser.h"
#include "content/public/browser/usb_delegate.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/device/public/mojom/usb_device.mojom-forward.h"
#include "services/device/public/mojom/usb_enumeration_options.mojom-forward.h"
#include "services/device/public/mojom/usb_manager.mojom-forward.h"
#include "third_party/blink/public/mojom/usb/web_usb_service.mojom.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace electron {

class UsbChooserController;

class ElectronUsbDelegate : public content::UsbDelegate {
 public:
  ElectronUsbDelegate();
  ElectronUsbDelegate(ElectronUsbDelegate&&) = delete;
  ElectronUsbDelegate& operator=(ElectronUsbDelegate&) = delete;
  ~ElectronUsbDelegate() override;

  // content::UsbDelegate:
  void AdjustProtectedInterfaceClasses(content::BrowserContext* browser_context,
                                       const url::Origin& origin,
                                       content::RenderFrameHost* frame,
                                       std::vector<uint8_t>& classes) override;
  std::unique_ptr<content::UsbChooser> RunChooser(
      content::RenderFrameHost& frame,
      blink::mojom::WebUsbRequestDeviceOptionsPtr options,
      blink::mojom::WebUsbService::GetPermissionCallback callback) override;
  bool CanRequestDevicePermission(content::BrowserContext* browser_context,
                                  const url::Origin& origin) override;
  void RevokeDevicePermissionWebInitiated(
      content::BrowserContext* browser_context,
      const url::Origin& origin,
      const device::mojom::UsbDeviceInfo& device) override;
  const device::mojom::UsbDeviceInfo* GetDeviceInfo(
      content::BrowserContext* browser_context,
      const std::string& guid) override;
  bool HasDevicePermission(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      const url::Origin& origin,
      const device::mojom::UsbDeviceInfo& device_info) override;
  void GetDevices(
      content::BrowserContext* browser_context,
      blink::mojom::WebUsbService::GetDevicesCallback callback) override;
  void GetDevice(
      content::BrowserContext* browser_context,
      const std::string& guid,
      base::span<const uint8_t> blocked_interface_classes,
      mojo::PendingReceiver<device::mojom::UsbDevice> device_receiver,
      mojo::PendingRemote<device::mojom::UsbDeviceClient> device_client)
      override;
  void AddObserver(content::BrowserContext* browser_context,
                   Observer* observer) override;
  void RemoveObserver(content::BrowserContext* browser_context,
                      Observer* observer) override;

  // TODO: See if we can separate these from Profiles upstream.
  void IncrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override {}

  void DecrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override {}

  bool IsServiceWorkerAllowedForOrigin(const url::Origin& origin) override;

  void DeleteControllerForFrame(content::RenderFrameHost* render_frame_host);

  bool PageMayUseUsb(content::Page& page) override;

 private:
  UsbChooserController* ControllerForFrame(
      content::RenderFrameHost* render_frame_host);

  UsbChooserController* AddControllerForFrame(
      content::RenderFrameHost* render_frame_host,
      blink::mojom::WebUsbRequestDeviceOptionsPtr options,
      blink::mojom::WebUsbService::GetPermissionCallback callback);

  class ContextObservation;

  ContextObservation* GetContextObserver(
      content::BrowserContext* browser_context);

  base::flat_map<content::BrowserContext*, std::unique_ptr<ContextObservation>>
      observations_;

  std::unordered_map<content::RenderFrameHost*,
                     std::unique_ptr<UsbChooserController>>
      controller_map_;

  base::WeakPtrFactory<ElectronUsbDelegate> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_USB_ELECTRON_USB_DELEGATE_H_
