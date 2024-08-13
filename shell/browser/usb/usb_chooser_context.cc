// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/usb/usb_chooser_context.h"

#include <utility>
#include <vector>

#include <string_view>
#include "base/containers/contains.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "components/content_settings/core/common/content_settings.h"
#include "content/public/browser/device_service.h"
#include "services/device/public/cpp/usb/usb_ids.h"
#include "services/device/public/mojom/usb_device.mojom.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/common/electron_constants.h"
#include "shell/common/gin_converters/usb_device_info_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

constexpr char kDeviceNameKey[] = "productName";
constexpr char kDeviceIdKey[] = "deviceId";
constexpr int kUsbClassMassStorage = 0x08;

bool CanStorePersistentEntry(const device::mojom::UsbDeviceInfo& device_info) {
  return device_info.serial_number && !device_info.serial_number->empty();
}

bool IsMassStorageInterface(const device::mojom::UsbInterfaceInfo& interface) {
  for (auto& alternate : interface.alternates) {
    if (alternate->class_code == kUsbClassMassStorage)
      return true;
  }
  return false;
}

bool ShouldExposeDevice(const device::mojom::UsbDeviceInfo& device_info) {
  // blink::USBDevice::claimInterface() disallows claiming mass storage
  // interfaces, but explicitly prevent access in the browser process as
  // ChromeOS would allow these interfaces to be claimed.
  for (auto& configuration : device_info.configurations) {
    if (configuration->interfaces.size() == 0) {
      return true;
    }
    for (auto& interface : configuration->interfaces) {
      if (!IsMassStorageInterface(*interface))
        return true;
    }
  }
  return false;
}

}  // namespace

namespace electron {

UsbChooserContext::UsbChooserContext(ElectronBrowserContext* context)
    : browser_context_(context) {}

// static
base::Value UsbChooserContext::DeviceInfoToValue(
    const device::mojom::UsbDeviceInfo& device_info) {
  base::Value::Dict device_value;
  device_value.Set(kDeviceNameKey, device_info.product_name
                                       ? *device_info.product_name
                                       : std::u16string_view());
  device_value.Set(kDeviceVendorIdKey, device_info.vendor_id);
  device_value.Set(kDeviceProductIdKey, device_info.product_id);

  if (device_info.manufacturer_name) {
    device_value.Set("manufacturerName", *device_info.manufacturer_name);
  }

  // CanStorePersistentEntry checks if |device_info.serial_number| is not empty.
  if (CanStorePersistentEntry(device_info)) {
    device_value.Set(kDeviceSerialNumberKey, *device_info.serial_number);
  }

  device_value.Set(kDeviceIdKey, device_info.guid);

  device_value.Set("usbVersionMajor", device_info.usb_version_major);
  device_value.Set("usbVersionMinor", device_info.usb_version_minor);
  device_value.Set("usbVersionSubminor", device_info.usb_version_subminor);
  device_value.Set("deviceClass", device_info.class_code);
  device_value.Set("deviceSubclass", device_info.subclass_code);
  device_value.Set("deviceProtocol", device_info.protocol_code);
  device_value.Set("deviceVersionMajor", device_info.device_version_major);
  device_value.Set("deviceVersionMinor", device_info.device_version_minor);
  device_value.Set("deviceVersionSubminor",
                   device_info.device_version_subminor);
  return base::Value(std::move(device_value));
}

void UsbChooserContext::InitDeviceList(
    std::vector<device::mojom::UsbDeviceInfoPtr> devices) {
  for (auto& device_info : devices) {
    DCHECK(device_info);
    if (ShouldExposeDevice(*device_info)) {
      devices_.insert(
          std::make_pair(device_info->guid, std::move(device_info)));
    }
  }
  is_initialized_ = true;

  while (!pending_get_devices_requests_.empty()) {
    std::vector<device::mojom::UsbDeviceInfoPtr> device_list;
    for (const auto& entry : devices_) {
      device_list.push_back(entry.second->Clone());
    }
    std::move(pending_get_devices_requests_.front())
        .Run(std::move(device_list));
    pending_get_devices_requests_.pop();
  }
}

void UsbChooserContext::EnsureConnectionWithDeviceManager() {
  if (device_manager_)
    return;

  // Receive mojo::Remote<UsbDeviceManager> from DeviceService.
  content::GetDeviceService().BindUsbDeviceManager(
      device_manager_.BindNewPipeAndPassReceiver());

  SetUpDeviceManagerConnection();
}

void UsbChooserContext::SetUpDeviceManagerConnection() {
  DCHECK(device_manager_);
  device_manager_.set_disconnect_handler(
      base::BindOnce(&UsbChooserContext::OnDeviceManagerConnectionError,
                     base::Unretained(this)));

  // Listen for added/removed device events.
  DCHECK(!client_receiver_.is_bound());
  device_manager_->EnumerateDevicesAndSetClient(
      client_receiver_.BindNewEndpointAndPassRemote(),
      base::BindOnce(&UsbChooserContext::InitDeviceList,
                     weak_factory_.GetWeakPtr()));
}

UsbChooserContext::~UsbChooserContext() {
  OnDeviceManagerConnectionError();
  for (auto& observer : device_observer_list_) {
    observer.OnBrowserContextShutdown();
    DCHECK(!device_observer_list_.HasObserver(&observer));
  }
}

void UsbChooserContext::RevokeDevicePermissionWebInitiated(
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device) {
  DCHECK(base::Contains(devices_, device.guid));
  RevokeObjectPermissionInternal(origin, DeviceInfoToValue(device),
                                 /*revoked_by_website=*/true);
}

void UsbChooserContext::RevokeObjectPermissionInternal(
    const url::Origin& origin,
    const base::Value& object,
    bool revoked_by_website = false) {
  const base::Value::Dict* object_dict = object.GetIfDict();
  DCHECK(object_dict != nullptr);

  if (object_dict->FindString(kDeviceSerialNumberKey) != nullptr) {
    auto* permission_manager = static_cast<ElectronPermissionManager*>(
        browser_context_->GetPermissionControllerDelegate());
    permission_manager->RevokeDevicePermission(
        static_cast<blink::PermissionType>(
            WebContentsPermissionHelper::PermissionType::USB),
        origin, object, browser_context_);
  } else {
    const std::string* guid = object_dict->FindString(kDeviceIdKey);
    auto it = ephemeral_devices_.find(origin);
    if (it != ephemeral_devices_.end()) {
      it->second.erase(*guid);
      if (it->second.empty())
        ephemeral_devices_.erase(it);
    }
  }

  api::Session* session = api::Session::FromBrowserContext(browser_context_);
  if (session) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    auto details = gin_helper::Dictionary::CreateEmpty(isolate);
    details.Set("device", object);
    details.Set("origin", origin.Serialize());
    session->Emit("usb-device-revoked", details);
  }
}

void UsbChooserContext::GrantDevicePermission(
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device_info) {
  if (CanStorePersistentEntry(device_info)) {
    auto* permission_manager = static_cast<ElectronPermissionManager*>(
        browser_context_->GetPermissionControllerDelegate());
    permission_manager->GrantDevicePermission(
        static_cast<blink::PermissionType>(
            WebContentsPermissionHelper::PermissionType::USB),
        origin, DeviceInfoToValue(device_info), browser_context_);
  } else {
    ephemeral_devices_[origin].insert(device_info.guid);
  }
}

bool UsbChooserContext::HasDevicePermission(
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device_info) {
  auto it = ephemeral_devices_.find(origin);
  if (it != ephemeral_devices_.end() &&
      base::Contains(it->second, device_info.guid)) {
    return true;
  }

  auto* permission_manager = static_cast<ElectronPermissionManager*>(
      browser_context_->GetPermissionControllerDelegate());

  return permission_manager->CheckDevicePermission(
      static_cast<blink::PermissionType>(
          WebContentsPermissionHelper::PermissionType::USB),
      origin, DeviceInfoToValue(device_info), browser_context_);
}

void UsbChooserContext::GetDevices(
    device::mojom::UsbDeviceManager::GetDevicesCallback callback) {
  if (!is_initialized_) {
    EnsureConnectionWithDeviceManager();
    pending_get_devices_requests_.push(std::move(callback));
    return;
  }

  std::vector<device::mojom::UsbDeviceInfoPtr> device_list;
  for (const auto& pair : devices_) {
    device_list.push_back(pair.second->Clone());
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(device_list)));
}

void UsbChooserContext::GetDevice(
    const std::string& guid,
    base::span<const uint8_t> blocked_interface_classes,
    mojo::PendingReceiver<device::mojom::UsbDevice> device_receiver,
    mojo::PendingRemote<device::mojom::UsbDeviceClient> device_client) {
  EnsureConnectionWithDeviceManager();
  device_manager_->GetDevice(
      guid,
      std::vector<uint8_t>(blocked_interface_classes.begin(),
                           blocked_interface_classes.end()),
      std::move(device_receiver), std::move(device_client));
}

const device::mojom::UsbDeviceInfo* UsbChooserContext::GetDeviceInfo(
    const std::string& guid) {
  DCHECK(is_initialized_);
  auto it = devices_.find(guid);
  return it == devices_.end() ? nullptr : it->second.get();
}

void UsbChooserContext::AddObserver(DeviceObserver* observer) {
  EnsureConnectionWithDeviceManager();
  device_observer_list_.AddObserver(observer);
}

void UsbChooserContext::RemoveObserver(DeviceObserver* observer) {
  device_observer_list_.RemoveObserver(observer);
}

base::WeakPtr<UsbChooserContext> UsbChooserContext::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void UsbChooserContext::OnDeviceAdded(
    device::mojom::UsbDeviceInfoPtr device_info) {
  DCHECK(device_info);
  // Update the device list.
  DCHECK(!base::Contains(devices_, device_info->guid));
  if (!ShouldExposeDevice(*device_info))
    return;
  devices_.insert(std::make_pair(device_info->guid, device_info->Clone()));

  // Notify all observers.
  for (auto& observer : device_observer_list_)
    observer.OnDeviceAdded(*device_info);
}

void UsbChooserContext::OnDeviceRemoved(
    device::mojom::UsbDeviceInfoPtr device_info) {
  DCHECK(device_info);

  if (!ShouldExposeDevice(*device_info)) {
    DCHECK(!base::Contains(devices_, device_info->guid));
    return;
  }

  // Update the device list.
  DCHECK(base::Contains(devices_, device_info->guid));
  devices_.erase(device_info->guid);

  // Notify all device observers.
  for (auto& observer : device_observer_list_)
    observer.OnDeviceRemoved(*device_info);

  // If the device was persistent, return. Otherwise, notify all permission
  // observers that its permissions were revoked.
  if (device_info->serial_number &&
      !device_info->serial_number.value().empty()) {
    return;
  }
  for (auto& map_entry : ephemeral_devices_) {
    map_entry.second.erase(device_info->guid);
  }
}

void UsbChooserContext::OnDeviceManagerConnectionError() {
  device_manager_.reset();
  client_receiver_.reset();
  devices_.clear();
  is_initialized_ = false;

  ephemeral_devices_.clear();

  // Notify all device observers.
  for (auto& observer : device_observer_list_)
    observer.OnDeviceManagerConnectionError();
}

}  // namespace electron
