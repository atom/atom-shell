// Copyright (c) 2020 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_SERIAL_SERIAL_CHOOSER_CONTROLLER_H_
#define ELECTRON_SHELL_BROWSER_SERIAL_SERIAL_CHOOSER_CONTROLLER_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/serial_chooser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "services/device/public/mojom/serial.mojom-forward.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/serial/electron_serial_delegate.h"
#include "shell/browser/serial/serial_chooser_context.h"
#include "third_party/blink/public/mojom/serial/serial.mojom.h"

namespace content {
class RenderFrameHost;
}

namespace electron {

class ElectronSerialDelegate;

// SerialChooserController provides data for the Serial API permission prompt.
class SerialChooserController final
    : private SerialChooserContext::PortObserver,
      private content::WebContentsObserver {
 public:
  SerialChooserController(
      content::RenderFrameHost* render_frame_host,
      std::vector<blink::mojom::SerialPortFilterPtr> filters,
      std::vector<::device::BluetoothUUID> allowed_bluetooth_service_class_ids,
      content::SerialChooser::Callback callback,
      content::WebContents* web_contents,
      base::WeakPtr<ElectronSerialDelegate> serial_delegate);
  ~SerialChooserController() override;

  // disable copy
  SerialChooserController(const SerialChooserController&) = delete;
  SerialChooserController& operator=(const SerialChooserController&) = delete;

  // SerialChooserContext::PortObserver:
  void OnPortAdded(const device::mojom::SerialPortInfo& port) override;
  void OnPortRemoved(const device::mojom::SerialPortInfo& port) override;
  void OnPortConnectedStateChanged(
      const device::mojom::SerialPortInfo& port) override {}
  void OnPortManagerConnectionError() override;
  void OnPermissionRevoked(const url::Origin& origin) override {}
  void OnSerialChooserContextShutdown() override;

 private:
  api::Session* GetSession();
  void OnGetDevices(std::vector<device::mojom::SerialPortInfoPtr> ports);
  bool DisplayDevice(const device::mojom::SerialPortInfo& port) const;
  void RunCallback(device::mojom::SerialPortInfoPtr port);
  void OnDeviceChosen(const std::string& port_id);

  std::vector<blink::mojom::SerialPortFilterPtr> filters_;
  std::vector<::device::BluetoothUUID> allowed_bluetooth_service_class_ids_;
  content::SerialChooser::Callback callback_;
  url::Origin origin_;

  base::WeakPtr<SerialChooserContext> chooser_context_;

  base::ScopedObservation<SerialChooserContext,
                          SerialChooserContext::PortObserver>
      observation_{this};

  std::vector<device::mojom::SerialPortInfoPtr> ports_;

  base::WeakPtr<ElectronSerialDelegate> serial_delegate_;

  content::GlobalRenderFrameHostId render_frame_host_id_;

  base::WeakPtrFactory<SerialChooserController> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_SERIAL_SERIAL_CHOOSER_CONTROLLER_H_
