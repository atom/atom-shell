// Copyright (c) 2021 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/hid/hid_chooser_controller.h"

#include <utility>

#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/functional/bind.h"
#include "base/ranges/algorithm.h"
#include "gin/data_object_builder.h"
#include "services/device/public/cpp/hid/hid_blocklist.h"
#include "services/device/public/cpp/hid/hid_switches.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/hid/hid_chooser_context.h"
#include "shell/browser/hid/hid_chooser_context_factory.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/content_converter.h"
#include "shell/common/gin_converters/hid_device_info_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"
#include "shell/common/process_util.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

bool FilterMatch(const blink::mojom::HidDeviceFilterPtr& filter,
                 const device::mojom::HidDeviceInfo& device) {
  if (filter->device_ids) {
    if (filter->device_ids->is_vendor()) {
      if (filter->device_ids->get_vendor() != device.vendor_id)
        return false;
    } else if (filter->device_ids->is_vendor_and_product()) {
      const auto& vendor_and_product =
          filter->device_ids->get_vendor_and_product();
      if (vendor_and_product->vendor != device.vendor_id)
        return false;
      if (vendor_and_product->product != device.product_id)
        return false;
    }
  }

  if (filter->usage) {
    if (filter->usage->is_page()) {
      const uint16_t usage_page = filter->usage->get_page();
      auto find_it =
          std::find_if(device.collections.begin(), device.collections.end(),
                       [=](const device::mojom::HidCollectionInfoPtr& c) {
                         return usage_page == c->usage->usage_page;
                       });
      if (find_it == device.collections.end())
        return false;
    } else if (filter->usage->is_usage_and_page()) {
      const auto& usage_and_page = filter->usage->get_usage_and_page();
      auto find_it = std::find_if(
          device.collections.begin(), device.collections.end(),
          [&usage_and_page](const device::mojom::HidCollectionInfoPtr& c) {
            return usage_and_page->usage_page == c->usage->usage_page &&
                   usage_and_page->usage == c->usage->usage;
          });
      if (find_it == device.collections.end())
        return false;
    }
  }
  return true;
}

}  // namespace

namespace electron {

HidChooserController::HidChooserController(
    content::RenderFrameHost* render_frame_host,
    std::vector<blink::mojom::HidDeviceFilterPtr> filters,
    std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
    content::HidChooser::Callback callback,
    content::WebContents* web_contents,
    base::WeakPtr<ElectronHidDelegate> hid_delegate)
    : WebContentsObserver(web_contents),
      filters_(std::move(filters)),
      exclusion_filters_(std::move(exclusion_filters)),
      callback_(std::move(callback)),
      initiator_document_(render_frame_host->GetWeakDocumentPtr()),
      origin_(content::WebContents::FromRenderFrameHost(render_frame_host)
                  ->GetPrimaryMainFrame()
                  ->GetLastCommittedOrigin()),
      hid_delegate_(hid_delegate),
      render_frame_host_id_(render_frame_host->GetGlobalId()) {
  // The use above of GetMainFrame is safe as content::HidService instances are
  // not created for fenced frames.
  DCHECK(!render_frame_host->IsNestedWithinFencedFrame());

  chooser_context_ = HidChooserContextFactory::GetForBrowserContext(
                         web_contents->GetBrowserContext())
                         ->AsWeakPtr();
  DCHECK(chooser_context_);

  chooser_context_->GetHidManager()->GetDevices(base::BindOnce(
      &HidChooserController::OnGotDevices, weak_factory_.GetWeakPtr()));
}

HidChooserController::~HidChooserController() {
  if (callback_)
    std::move(callback_).Run(std::vector<device::mojom::HidDeviceInfoPtr>());
}

// static
const std::string& HidChooserController::PhysicalDeviceIdFromDeviceInfo(
    const device::mojom::HidDeviceInfo& device) {
  // A single physical device may expose multiple HID interfaces, each
  // represented by a HidDeviceInfo object. When a device exposes multiple
  // HID interfaces, the HidDeviceInfo objects will share a common
  // |physical_device_id|. Group these devices so that a single chooser item
  // is shown for each physical device. If a device's physical device ID is
  // empty, use its GUID instead.
  return device.physical_device_id.empty() ? device.guid
                                           : device.physical_device_id;
}

api::Session* HidChooserController::GetSession() {
  if (!web_contents()) {
    return nullptr;
  }
  return api::Session::FromBrowserContext(web_contents()->GetBrowserContext());
}

void HidChooserController::OnDeviceAdded(
    const device::mojom::HidDeviceInfo& device) {
  if (!DisplayDevice(device))
    return;

  if (AddDeviceInfo(device)) {
    api::Session* session = GetSession();
    if (session) {
      auto* rfh = content::RenderFrameHost::FromID(render_frame_host_id_);
      v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
      v8::HandleScope scope(isolate);
      v8::Local<v8::Object> details = gin::DataObjectBuilder(isolate)
                                          .Set("device", device.Clone())
                                          .Set("frame", rfh)
                                          .Build();
      session->Emit("hid-device-added", details);
    }
  }
}

void HidChooserController::OnDeviceRemoved(
    const device::mojom::HidDeviceInfo& device) {
  if (!base::Contains(items_, PhysicalDeviceIdFromDeviceInfo(device)))
    return;

  api::Session* session = GetSession();
  if (session) {
    auto* rfh = content::RenderFrameHost::FromID(render_frame_host_id_);
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Object> details = gin::DataObjectBuilder(isolate)
                                        .Set("device", device.Clone())
                                        .Set("frame", rfh)
                                        .Build();
    session->Emit("hid-device-removed", details);
  }
  RemoveDeviceInfo(device);
}

void HidChooserController::OnDeviceChanged(
    const device::mojom::HidDeviceInfo& device) {
  bool has_chooser_item =
      base::Contains(items_, PhysicalDeviceIdFromDeviceInfo(device));
  if (!DisplayDevice(device)) {
    if (has_chooser_item)
      OnDeviceRemoved(device);
    return;
  }

  if (!has_chooser_item) {
    OnDeviceAdded(device);
    return;
  }

  // Update the item to replace the old device info with |device|.
  UpdateDeviceInfo(device);
}

void HidChooserController::OnDeviceChosen(gin::Arguments* args) {
  std::string device_id;
  if (!args->GetNext(&device_id) || device_id.empty()) {
    RunCallback({});
  } else {
    auto find_it = device_map_.find(device_id);
    if (find_it != device_map_.end()) {
      auto& device_infos = find_it->second;
      std::vector<device::mojom::HidDeviceInfoPtr> devices;
      devices.reserve(device_infos.size());
      for (auto& device : device_infos) {
        chooser_context_->GrantDevicePermission(origin_, *device);
        devices.push_back(device->Clone());
      }
      RunCallback(std::move(devices));
    } else {
      v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
      node::Environment* env = node::Environment::GetCurrent(isolate);
      EmitWarning(env, "The device id " + device_id + " was not found.",
                  "UnknownHIDDeviceId");
      RunCallback({});
    }
  }
}

void HidChooserController::OnHidManagerConnectionError() {
  observation_.Reset();
}

void HidChooserController::OnHidChooserContextShutdown() {
  observation_.Reset();
}

void HidChooserController::OnGotDevices(
    std::vector<device::mojom::HidDeviceInfoPtr> devices) {
  std::vector<device::mojom::HidDeviceInfoPtr> devicesToDisplay;
  devicesToDisplay.reserve(devices.size());

  for (auto& device : devices) {
    if (DisplayDevice(*device)) {
      if (AddDeviceInfo(*device))
        devicesToDisplay.push_back(device->Clone());
    }
  }

  // Listen to HidChooserContext for OnDeviceAdded/Removed events after the
  // enumeration.
  if (chooser_context_)
    observation_.Observe(chooser_context_.get());

  bool prevent_default = false;
  api::Session* session = GetSession();
  if (session) {
    auto* rfh = content::RenderFrameHost::FromID(render_frame_host_id_);
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Object> details = gin::DataObjectBuilder(isolate)
                                        .Set("deviceList", devicesToDisplay)
                                        .Set("frame", rfh)
                                        .Build();
    prevent_default =
        session->Emit("select-hid-device", details,
                      base::BindRepeating(&HidChooserController::OnDeviceChosen,
                                          weak_factory_.GetWeakPtr()));
  }
  if (!prevent_default) {
    RunCallback({});
  }
}

bool HidChooserController::DisplayDevice(
    const device::mojom::HidDeviceInfo& device) const {
  // Check if `device` has a top-level collection with a FIDO usage. FIDO
  // devices may be displayed if the origin is privileged or the blocklist is
  // disabled.
  const bool has_fido_collection =
      base::Contains(device.collections, device::mojom::kPageFido,
                     [](const auto& c) { return c->usage->usage_page; });

  if (has_fido_collection) {
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableHidBlocklist) ||
        (chooser_context_ &&
         chooser_context_->IsFidoAllowedForOrigin(origin_))) {
      return FilterMatchesAny(device) && !IsExcluded(device);
    }

    AddMessageToConsole(
        blink::mojom::ConsoleMessageLevel::kInfo,
        base::StringPrintf(
            "Chooser dialog is not displaying a FIDO HID device: vendorId=%d, "
            "productId=%d, name='%s', serial='%s'",
            device.vendor_id, device.product_id, device.product_name.c_str(),
            device.serial_number.c_str()));
    return false;
  }

  if (device.is_excluded_by_blocklist) {
    AddMessageToConsole(
        blink::mojom::ConsoleMessageLevel::kInfo,
        base::StringPrintf(
            "Chooser dialog is not displaying a device excluded by "
            "the HID blocklist: vendorId=%d, "
            "productId=%d, name='%s', serial='%s'",
            device.vendor_id, device.product_id, device.product_name.c_str(),
            device.serial_number.c_str()));
    return false;
  }

  return FilterMatchesAny(device) && !IsExcluded(device);
}

bool HidChooserController::FilterMatchesAny(
    const device::mojom::HidDeviceInfo& device) const {
  if (filters_.empty())
    return true;

  for (const auto& filter : filters_) {
    if (FilterMatch(filter, device))
      return true;
  }

  return false;
}

bool HidChooserController::IsExcluded(
    const device::mojom::HidDeviceInfo& device) const {
  for (const auto& exclusion_filter : exclusion_filters_) {
    if (FilterMatch(exclusion_filter, device))
      return true;
  }

  return false;
}

void HidChooserController::AddMessageToConsole(
    blink::mojom::ConsoleMessageLevel level,
    const std::string& message) const {
  if (content::RenderFrameHost* rfh =
          initiator_document_.AsRenderFrameHostIfValid()) {
    rfh->AddMessageToConsole(level, message);
  }
}

bool HidChooserController::AddDeviceInfo(
    const device::mojom::HidDeviceInfo& device) {
  const auto& id = PhysicalDeviceIdFromDeviceInfo(device);
  auto [iter, is_new_physical_device] = device_map_.try_emplace(id);
  iter->second.emplace_back(device.Clone());

  // append new devices to the chooser list
  if (is_new_physical_device)
    items_.emplace_back(id);

  return is_new_physical_device;
}

bool HidChooserController::RemoveDeviceInfo(
    const device::mojom::HidDeviceInfo& device) {
  const auto& id = PhysicalDeviceIdFromDeviceInfo(device);
  auto find_it = device_map_.find(id);
  DCHECK(find_it != device_map_.end());
  auto& device_infos = find_it->second;
  std::erase_if(device_infos,
                [&device](const device::mojom::HidDeviceInfoPtr& d) {
                  return d->guid == device.guid;
                });
  if (!device_infos.empty())
    return false;
  // A device was disconnected. Remove it from the chooser list.
  device_map_.erase(find_it);
  std::erase(items_, id);
  return true;
}

void HidChooserController::UpdateDeviceInfo(
    const device::mojom::HidDeviceInfo& device) {
  const auto& id = PhysicalDeviceIdFromDeviceInfo(device);
  auto physical_device_it = device_map_.find(id);
  DCHECK(physical_device_it != device_map_.end());
  auto& device_infos = physical_device_it->second;
  auto device_it = base::ranges::find(device_infos, device.guid,
                                      &device::mojom::HidDeviceInfo::guid);
  DCHECK(device_it != device_infos.end());
  *device_it = device.Clone();
}

void HidChooserController::RunCallback(
    std::vector<device::mojom::HidDeviceInfoPtr> devices) {
  if (callback_) {
    std::move(callback_).Run(std::move(devices));
  }
}

void HidChooserController::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  if (hid_delegate_) {
    hid_delegate_->DeleteControllerForFrame(render_frame_host);
  }
}

}  // namespace electron
