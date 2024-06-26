// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTROLLER_H_
#define ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTROLLER_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "services/device/public/mojom/usb_device.mojom.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/usb/electron_usb_delegate.h"
#include "shell/browser/usb/usb_chooser_context.h"
#include "third_party/blink/public/mojom/usb/web_usb_service.mojom.h"
#include "url/origin.h"

namespace content {
class RenderFrameHost;
}

namespace electron {

// UsbChooserController creates a chooser for WebUSB.
class UsbChooserController final : private UsbChooserContext::DeviceObserver,
                                   private content::WebContentsObserver {
 public:
  UsbChooserController(
      content::RenderFrameHost* render_frame_host,
      blink::mojom::WebUsbRequestDeviceOptionsPtr options,
      blink::mojom::WebUsbService::GetPermissionCallback callback,
      content::WebContents* web_contents,
      base::WeakPtr<ElectronUsbDelegate> usb_delegate);

  UsbChooserController(const UsbChooserController&) = delete;
  UsbChooserController& operator=(const UsbChooserController&) = delete;

  ~UsbChooserController() override;

  // UsbChooserContext::DeviceObserver implementation:
  void OnDeviceAdded(const device::mojom::UsbDeviceInfo& device_info) override;
  void OnDeviceRemoved(
      const device::mojom::UsbDeviceInfo& device_info) override;
  void OnBrowserContextShutdown() override;

  // content::WebContentsObserver:
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;

 private:
  api::Session* GetSession();
  void GotUsbDeviceList(std::vector<device::mojom::UsbDeviceInfoPtr> devices);
  bool DisplayDevice(const device::mojom::UsbDeviceInfo& device) const;
  void RunCallback(device::mojom::UsbDeviceInfoPtr device_info);
  void OnDeviceChosen(gin::Arguments* args);

  blink::mojom::WebUsbRequestDeviceOptionsPtr options_;
  blink::mojom::WebUsbService::GetPermissionCallback callback_;
  url::Origin origin_;

  base::WeakPtr<UsbChooserContext> chooser_context_;
  base::ScopedObservation<UsbChooserContext, UsbChooserContext::DeviceObserver>
      observation_{this};

  base::WeakPtr<ElectronUsbDelegate> usb_delegate_;

  content::GlobalRenderFrameHostId render_frame_host_id_;

  base::WeakPtrFactory<UsbChooserController> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTROLLER_H_
