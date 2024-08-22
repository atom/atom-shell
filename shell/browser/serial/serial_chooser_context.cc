// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/serial/serial_chooser_context.h"

#include <string>
#include <utility>

#include <string_view>
#include "base/base64.h"
#include "base/containers/contains.h"
#include "base/values.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_converters/serial_port_info_converter.h"

namespace electron {

constexpr char kPortNameKey[] = "name";
constexpr char kTokenKey[] = "token";
constexpr char kBluetoothDevicePathKey[] = "bluetooth_device_path";
#if BUILDFLAG(IS_WIN)
constexpr char kDeviceInstanceIdKey[] = "device_instance_id";
#else
constexpr char kVendorIdKey[] = "vendor_id";
constexpr char kProductIdKey[] = "product_id";
constexpr char kSerialNumberKey[] = "serial_number";
#if BUILDFLAG(IS_MAC)
constexpr char kUsbDriverKey[] = "usb_driver";
#endif  // BUILDFLAG(IS_MAC)
#endif  // BUILDFLAG(IS_WIN)

namespace {

std::string EncodeToken(const base::UnguessableToken& token) {
  const uint64_t data[2] = {token.GetHighForSerialization(),
                            token.GetLowForSerialization()};
  return base::Base64Encode(
      std::string_view(reinterpret_cast<const char*>(&data[0]), sizeof(data)));
}

base::Value PortInfoToValue(const device::mojom::SerialPortInfo& port) {
  base::Value::Dict value;
  if (port.display_name && !port.display_name->empty()) {
    value.Set(kPortNameKey, *port.display_name);
  } else {
    value.Set(kPortNameKey, port.path.LossyDisplayName());
  }

  if (!SerialChooserContext::CanStorePersistentEntry(port)) {
    value.Set(kTokenKey, EncodeToken(port.token));
    return base::Value(std::move(value));
  }

  if (port.bluetooth_service_class_id &&
      port.bluetooth_service_class_id->IsValid()) {
    value.Set(kBluetoothDevicePathKey, port.path.LossyDisplayName());
  } else {
#if BUILDFLAG(IS_WIN)
    // Windows provides a handy device identifier which we can rely on to be
    // sufficiently stable for identifying devices across restarts.
    value.Set(kDeviceInstanceIdKey, port.device_instance_id);
#else
    CHECK(port.has_vendor_id);
    value.Set(kVendorIdKey, port.vendor_id);
    CHECK(port.has_product_id);
    value.Set(kProductIdKey, port.product_id);
    CHECK(port.serial_number);
    value.Set(kSerialNumberKey, *port.serial_number);
#if BUILDFLAG(IS_MAC)
    CHECK(port.usb_driver_name && !port.usb_driver_name->empty());
    value.Set(kUsbDriverKey, *port.usb_driver_name);
#endif  // BUILDFLAG(IS_MAC)
#endif  // BUILDFLAG(IS_WIN)
  }
  return base::Value(std::move(value));
}

}  // namespace

SerialChooserContext::SerialChooserContext(ElectronBrowserContext* context)
    : browser_context_(context) {}

SerialChooserContext::~SerialChooserContext() {
  // Notify observers that the chooser context is about to be destroyed.
  // Observers must remove themselves from the observer lists.
  for (auto& observer : port_observer_list_) {
    observer.OnSerialChooserContextShutdown();
    DCHECK(!port_observer_list_.HasObserver(&observer));
  }
}

void SerialChooserContext::GrantPortPermission(
    const url::Origin& origin,
    const device::mojom::SerialPortInfo& port,
    content::RenderFrameHost* render_frame_host) {
  port_info_.insert({port.token, port.Clone()});

  if (CanStorePersistentEntry(port)) {
    auto* permission_manager = static_cast<ElectronPermissionManager*>(
        browser_context_->GetPermissionControllerDelegate());
    permission_manager->GrantDevicePermission(
        static_cast<blink::PermissionType>(
            WebContentsPermissionHelper::PermissionType::SERIAL),
        origin, PortInfoToValue(port), browser_context_);
    return;
  }

  ephemeral_ports_[origin].insert(port.token);
}

bool SerialChooserContext::HasPortPermission(
    const url::Origin& origin,
    const device::mojom::SerialPortInfo& port,
    content::RenderFrameHost* render_frame_host) {
  auto it = ephemeral_ports_.find(origin);
  if (it != ephemeral_ports_.end()) {
    const std::set<base::UnguessableToken>& ports = it->second;
    if (base::Contains(ports, port.token))
      return true;
  }

  if (!CanStorePersistentEntry(port))
    return false;

  auto* permission_manager = static_cast<ElectronPermissionManager*>(
      browser_context_->GetPermissionControllerDelegate());
  return permission_manager->CheckDevicePermission(
      static_cast<blink::PermissionType>(
          WebContentsPermissionHelper::PermissionType::SERIAL),
      origin, PortInfoToValue(port), browser_context_);
}

void SerialChooserContext::RevokePortPermissionWebInitiated(
    const url::Origin& origin,
    const base::UnguessableToken& token,
    content::RenderFrameHost* render_frame_host) {
  auto it = port_info_.find(token);
  if (it != port_info_.end()) {
    auto* permission_manager = static_cast<ElectronPermissionManager*>(
        browser_context_->GetPermissionControllerDelegate());
    permission_manager->RevokeDevicePermission(
        static_cast<blink::PermissionType>(
            WebContentsPermissionHelper::PermissionType::SERIAL),
        origin, PortInfoToValue(*it->second), browser_context_);
  }

  auto ephemeral = ephemeral_ports_.find(origin);
  if (ephemeral != ephemeral_ports_.end()) {
    std::set<base::UnguessableToken>& ports = ephemeral->second;
    ports.erase(token);
  }

  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  api::Session* session =
      api::Session::FromBrowserContext(web_contents->GetBrowserContext());

  if (session) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    auto details = gin_helper::Dictionary::CreateEmpty(isolate);
    details.Set("port", it->second);
    details.SetGetter("frame", render_frame_host);
    details.Set("origin", origin.Serialize());
    session->Emit("serial-port-revoked", details);
  }
}

// static
bool SerialChooserContext::CanStorePersistentEntry(
    const device::mojom::SerialPortInfo& port) {
  // If there is no display name then the path name will be used instead. The
  // path name is not guaranteed to be stable. For example, on Linux the name
  // "ttyUSB0" is reused for any USB serial device. A name like that would be
  // confusing to show in settings when the device is disconnected.
  if (!port.display_name || port.display_name->empty())
    return false;

  const bool has_bluetooth = port.bluetooth_service_class_id &&
                             port.bluetooth_service_class_id->IsValid() &&
                             !port.path.empty();
  if (has_bluetooth) {
    return true;
  }

#if BUILDFLAG(IS_WIN)
  return !port.device_instance_id.empty();
#else
  const bool has_usb = port.has_vendor_id && port.has_product_id &&
                       port.serial_number && !port.serial_number->empty();
  if (!has_usb) {
    return false;
  }

#if BUILDFLAG(IS_MAC)
  // The combination of the standard USB vendor ID, product ID and serial
  // number properties should be enough to uniquely identify a device
  // however recent versions of macOS include built-in drivers for common
  // types of USB-to-serial adapters while their manufacturers still
  // recommend installing their custom drivers. When both are loaded two
  // IOSerialBSDClient instances are found for each device. Including the
  // USB driver name allows us to distinguish between the two.
  if (!port.usb_driver_name || port.usb_driver_name->empty())
    return false;
#endif  // BUILDFLAG(IS_MAC)

  return true;
#endif  // BUILDFLAG(IS_WIN)
}

const device::mojom::SerialPortInfo* SerialChooserContext::GetPortInfo(
    const base::UnguessableToken& token) {
  DCHECK(is_initialized_);
  auto it = port_info_.find(token);
  return it == port_info_.end() ? nullptr : it->second.get();
}

device::mojom::SerialPortManager* SerialChooserContext::GetPortManager() {
  EnsurePortManagerConnection();
  return port_manager_.get();
}

void SerialChooserContext::AddPortObserver(PortObserver* observer) {
  port_observer_list_.AddObserver(observer);
}

void SerialChooserContext::RemovePortObserver(PortObserver* observer) {
  port_observer_list_.RemoveObserver(observer);
}

base::WeakPtr<SerialChooserContext> SerialChooserContext::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void SerialChooserContext::OnPortAdded(device::mojom::SerialPortInfoPtr port) {
  if (!base::Contains(port_info_, port->token))
    port_info_.insert({port->token, port->Clone()});

  for (auto& map_entry : ephemeral_ports_) {
    std::set<base::UnguessableToken>& ports = map_entry.second;
    ports.erase(port->token);
  }

  for (auto& observer : port_observer_list_)
    observer.OnPortAdded(*port);
}

void SerialChooserContext::OnPortRemoved(
    device::mojom::SerialPortInfoPtr port) {
  for (auto& observer : port_observer_list_)
    observer.OnPortRemoved(*port);

  port_info_.erase(port->token);
}

void SerialChooserContext::EnsurePortManagerConnection() {
  if (port_manager_)
    return;

  mojo::PendingRemote<device::mojom::SerialPortManager> manager;
  content::GetDeviceService().BindSerialPortManager(
      manager.InitWithNewPipeAndPassReceiver());
  SetUpPortManagerConnection(std::move(manager));
}

void SerialChooserContext::SetUpPortManagerConnection(
    mojo::PendingRemote<device::mojom::SerialPortManager> manager) {
  port_manager_.Bind(std::move(manager));
  port_manager_.set_disconnect_handler(
      base::BindOnce(&SerialChooserContext::OnPortManagerConnectionError,
                     base::Unretained(this)));

  port_manager_->SetClient(client_receiver_.BindNewPipeAndPassRemote());
  port_manager_->GetDevices(base::BindOnce(&SerialChooserContext::OnGetDevices,
                                           weak_factory_.GetWeakPtr()));
}

void SerialChooserContext::OnGetDevices(
    std::vector<device::mojom::SerialPortInfoPtr> ports) {
  for (auto& port : ports)
    port_info_.insert({port->token, std::move(port)});
  is_initialized_ = true;
}

void SerialChooserContext::OnPortManagerConnectionError() {
  port_manager_.reset();
  client_receiver_.reset();

  port_info_.clear();
  ephemeral_ports_.clear();
}
}  // namespace electron
