// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTEXT_H_
#define ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTEXT_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/containers/queue.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/usb_manager.mojom.h"
#include "services/device/public/mojom/usb_manager_client.mojom.h"
#include "url/origin.h"

namespace mojo {
template <typename T>
class PendingReciever;
template <typename T>
class PendingRemote;
}  // namespace mojo

namespace electron {

class ElectronBrowserContext;

class UsbChooserContext : public KeyedService,
                          public device::mojom::UsbDeviceManagerClient {
 public:
  explicit UsbChooserContext(ElectronBrowserContext* context);

  UsbChooserContext(const UsbChooserContext&) = delete;
  UsbChooserContext& operator=(const UsbChooserContext&) = delete;

  ~UsbChooserContext() override;

  // This observer can be used to be notified of changes to USB devices that are
  // connected.
  class DeviceObserver : public base::CheckedObserver {
   public:
    virtual void OnDeviceAdded(const device::mojom::UsbDeviceInfo&) {}
    virtual void OnDeviceRemoved(const device::mojom::UsbDeviceInfo&) {}
    virtual void OnDeviceManagerConnectionError() {}

    // Called when the BrowserContext is shutting down. Observers must remove
    // themselves before returning.
    virtual void OnBrowserContextShutdown() = 0;
  };

  static base::Value DeviceInfoToValue(
      const device::mojom::UsbDeviceInfo& device_info);

  // Grants |origin| access to the USB device.
  void GrantDevicePermission(const url::Origin& origin,
                             const device::mojom::UsbDeviceInfo& device_info);

  // Checks if |origin| has access to a device with |device_info|.
  bool HasDevicePermission(const url::Origin& origin,
                           const device::mojom::UsbDeviceInfo& device_info);

  // Revokes |origin| access to the USB device ordered by website.
  void RevokeDevicePermissionWebInitiated(
      const url::Origin& origin,
      const device::mojom::UsbDeviceInfo& device);

  void AddObserver(DeviceObserver* observer);
  void RemoveObserver(DeviceObserver* observer);

  // Forward UsbDeviceManager methods.
  void GetDevices(device::mojom::UsbDeviceManager::GetDevicesCallback callback);
  void GetDevice(
      const std::string& guid,
      base::span<const uint8_t> blocked_interface_classes,
      mojo::PendingReceiver<device::mojom::UsbDevice> device_receiver,
      mojo::PendingRemote<device::mojom::UsbDeviceClient> device_client);

  // This method should only be called when you are sure that |devices_| has
  // been initialized. It will return nullptr if the guid cannot be found.
  const device::mojom::UsbDeviceInfo* GetDeviceInfo(const std::string& guid);

  base::WeakPtr<UsbChooserContext> AsWeakPtr();

  void InitDeviceList(std::vector<::device::mojom::UsbDeviceInfoPtr> devices);

 private:
  // device::mojom::UsbDeviceManagerClient implementation.
  void OnDeviceAdded(device::mojom::UsbDeviceInfoPtr device_info) override;
  void OnDeviceRemoved(device::mojom::UsbDeviceInfoPtr device_info) override;

  void RevokeObjectPermissionInternal(const url::Origin& origin,
                                      const base::Value& object,
                                      bool revoked_by_website);

  void OnDeviceManagerConnectionError();
  void EnsureConnectionWithDeviceManager();
  void SetUpDeviceManagerConnection();

  bool is_initialized_ = false;
  base::queue<device::mojom::UsbDeviceManager::GetDevicesCallback>
      pending_get_devices_requests_;

  std::map<url::Origin, std::set<std::string>> ephemeral_devices_;
  std::map<std::string, device::mojom::UsbDeviceInfoPtr> devices_;

  // Connection to |device_manager_instance_|.
  mojo::Remote<device::mojom::UsbDeviceManager> device_manager_;
  mojo::AssociatedReceiver<device::mojom::UsbDeviceManagerClient>
      client_receiver_{this};
  base::ObserverList<DeviceObserver> device_observer_list_;

  raw_ptr<ElectronBrowserContext> browser_context_;

  base::WeakPtrFactory<UsbChooserContext> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_USB_USB_CHOOSER_CONTEXT_H_
