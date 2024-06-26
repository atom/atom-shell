// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/usb/usb_chooser_controller.h"

#include <stddef.h>
#include <utility>

#include "base/functional/bind.h"
#include "base/ranges/algorithm.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "gin/data_object_builder.h"
#include "services/device/public/cpp/usb/usb_utils.h"
#include "services/device/public/mojom/usb_enumeration_options.mojom.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/usb/usb_chooser_context_factory.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/content_converter.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_converters/usb_device_info_converter.h"
#include "shell/common/node_includes.h"
#include "shell/common/process_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

using content::RenderFrameHost;
using content::WebContents;

namespace electron {

UsbChooserController::UsbChooserController(
    RenderFrameHost* render_frame_host,
    blink::mojom::WebUsbRequestDeviceOptionsPtr options,
    blink::mojom::WebUsbService::GetPermissionCallback callback,
    content::WebContents* web_contents,
    base::WeakPtr<ElectronUsbDelegate> usb_delegate)
    : WebContentsObserver(web_contents),
      options_(std::move(options)),
      callback_(std::move(callback)),
      origin_(render_frame_host->GetMainFrame()->GetLastCommittedOrigin()),
      usb_delegate_(usb_delegate),
      render_frame_host_id_(render_frame_host->GetGlobalId()) {
  chooser_context_ = UsbChooserContextFactory::GetForBrowserContext(
                         web_contents->GetBrowserContext())
                         ->AsWeakPtr();
  DCHECK(chooser_context_);
  chooser_context_->GetDevices(base::BindOnce(
      &UsbChooserController::GotUsbDeviceList, weak_factory_.GetWeakPtr()));
}

UsbChooserController::~UsbChooserController() {
  RunCallback(/*device_info=*/nullptr);
}

api::Session* UsbChooserController::GetSession() {
  if (!web_contents()) {
    return nullptr;
  }
  return api::Session::FromBrowserContext(web_contents()->GetBrowserContext());
}

void UsbChooserController::OnDeviceAdded(
    const device::mojom::UsbDeviceInfo& device_info) {
  if (DisplayDevice(device_info)) {
    api::Session* session = GetSession();
    if (session) {
      session->Emit("usb-device-added", device_info.Clone(), web_contents());
    }
  }
}

void UsbChooserController::OnDeviceRemoved(
    const device::mojom::UsbDeviceInfo& device_info) {
  api::Session* session = GetSession();
  if (session) {
    session->Emit("usb-device-removed", device_info.Clone(), web_contents());
  }
}

void UsbChooserController::OnDeviceChosen(gin::Arguments* args) {
  std::string device_id;
  if (!args->GetNext(&device_id) || device_id.empty()) {
    RunCallback(/*device_info=*/nullptr);
  } else {
    auto* device_info = chooser_context_->GetDeviceInfo(device_id);
    if (device_info) {
      RunCallback(device_info->Clone());
    } else {
      v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
      node::Environment* env = node::Environment::GetCurrent(isolate);
      EmitWarning(env, "The device id " + device_id + " was not found.",
                  "UnknownUsbDeviceId");
      RunCallback(/*device_info=*/nullptr);
    }
  }
}

void UsbChooserController::OnBrowserContextShutdown() {
  observation_.Reset();
}

// Get a list of devices that can be shown in the chooser bubble UI for
// user to grant permission.
void UsbChooserController::GotUsbDeviceList(
    std::vector<::device::mojom::UsbDeviceInfoPtr> devices) {
  // Listen to UsbChooserContext for OnDeviceAdded/Removed events after the
  // enumeration.
  if (chooser_context_)
    observation_.Observe(chooser_context_.get());

  bool prevent_default = false;
  api::Session* session = GetSession();
  if (session) {
    auto* rfh = content::RenderFrameHost::FromID(render_frame_host_id_);
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);

    // "select-usb-device" should respect |filters|.
    std::erase_if(devices, [this](const auto& device_info) {
      return !DisplayDevice(*device_info);
    });

    v8::Local<v8::Object> details = gin::DataObjectBuilder(isolate)
                                        .Set("deviceList", devices)
                                        .Set("frame", rfh)
                                        .Build();

    prevent_default =
        session->Emit("select-usb-device", details,
                      base::BindRepeating(&UsbChooserController::OnDeviceChosen,
                                          weak_factory_.GetWeakPtr()));
  }
  if (!prevent_default) {
    RunCallback(/*device_info=*/nullptr);
  }
}

bool UsbChooserController::DisplayDevice(
    const device::mojom::UsbDeviceInfo& device_info) const {
  if (!device::UsbDeviceFilterMatchesAny(options_->filters, device_info)) {
    return false;
  }

  if (base::ranges::any_of(
          options_->exclusion_filters, [&device_info](const auto& filter) {
            return device::UsbDeviceFilterMatches(*filter, device_info);
          })) {
    return false;
  }

  return true;
}

void UsbChooserController::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  if (usb_delegate_) {
    usb_delegate_->DeleteControllerForFrame(render_frame_host);
  }
}

void UsbChooserController::RunCallback(
    device::mojom::UsbDeviceInfoPtr device_info) {
  if (callback_) {
    if (!chooser_context_ || !device_info) {
      std::move(callback_).Run(nullptr);
    } else {
      chooser_context_->GrantDevicePermission(origin_, *device_info);
      std::move(callback_).Run(std::move(device_info));
    }
  }
}

}  // namespace electron
