// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/usb/electron_usb_delegate.h"

#include <string_view>
#include <utility>

#include "base/containers/contains.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "electron/buildflags/buildflags.h"
#include "services/device/public/mojom/usb_enumeration_options.mojom.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/browser/usb/usb_chooser_context.h"
#include "shell/browser/usb/usb_chooser_context_factory.h"
#include "shell/browser/usb/usb_chooser_controller.h"
#include "shell/browser/web_contents_permission_helper.h"

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "base/containers/fixed_flat_set.h"
#include "chrome/common/chrome_features.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "services/device/public/mojom/usb_device.mojom.h"
#endif

namespace {

electron::UsbChooserContext* GetChooserContext(
    content::BrowserContext* browser_context) {
  return electron::UsbChooserContextFactory::GetForBrowserContext(
      browser_context);
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
// These extensions can claim the smart card USB class and automatically gain
// permissions for devices that have an interface with this class.
constexpr auto kSmartCardPrivilegedExtensionIds =
    base::MakeFixedFlatSet<std::string_view>({
        // Smart Card Connector Extension and its Beta version, see
        // crbug.com/1233881.
        "khpfeaanjngmcnplbdlpegiifgpfgdco",
        "mockcojkppdndnhgonljagclgpkjbkek",
    });

bool DeviceHasInterfaceWithClass(
    const device::mojom::UsbDeviceInfo& device_info,
    uint8_t interface_class) {
  for (const auto& configuration : device_info.configurations) {
    for (const auto& interface : configuration->interfaces) {
      for (const auto& alternate : interface->alternates) {
        if (alternate->class_code == interface_class)
          return true;
      }
    }
  }
  return false;
}
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

bool IsDevicePermissionAutoGranted(
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device_info) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // Note: The `DeviceHasInterfaceWithClass()` call is made after checking the
  // origin, since that method call is expensive.
  if (origin.scheme() == extensions::kExtensionScheme &&
      base::Contains(kSmartCardPrivilegedExtensionIds, origin.host()) &&
      DeviceHasInterfaceWithClass(device_info,
                                  device::mojom::kUsbSmartCardClass)) {
    return true;
  }
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

  return false;
}

}  // namespace

namespace electron {

// Manages the UsbDelegate observers for a single browser context.
class ElectronUsbDelegate::ContextObservation
    : private UsbChooserContext::DeviceObserver {
 public:
  ContextObservation(ElectronUsbDelegate* parent,
                     content::BrowserContext* browser_context)
      : parent_(parent), browser_context_(browser_context) {
    auto* chooser_context = GetChooserContext(browser_context_);
    if (chooser_context)
      device_observation_.Observe(chooser_context);
  }
  ContextObservation(ContextObservation&) = delete;
  ContextObservation& operator=(ContextObservation&) = delete;
  ~ContextObservation() override = default;

  // UsbChooserContext::DeviceObserver:
  void OnDeviceAdded(const device::mojom::UsbDeviceInfo& device_info) override {
    for (auto& observer : observer_list_)
      observer.OnDeviceAdded(device_info);
  }

  void OnDeviceRemoved(
      const device::mojom::UsbDeviceInfo& device_info) override {
    for (auto& observer : observer_list_)
      observer.OnDeviceRemoved(device_info);
  }

  void OnDeviceManagerConnectionError() override {
    for (auto& observer : observer_list_)
      observer.OnDeviceManagerConnectionError();
  }

  void OnBrowserContextShutdown() override {
    parent_->observations_.erase(browser_context_);
    // Return since `this` is now deleted.
  }

  void AddObserver(content::UsbDelegate::Observer* observer) {
    observer_list_.AddObserver(observer);
  }

  void RemoveObserver(content::UsbDelegate::Observer* observer) {
    observer_list_.RemoveObserver(observer);
  }

 private:
  // Safe because `parent_` owns `this`.
  const raw_ptr<ElectronUsbDelegate> parent_;

  // Safe because `this` is destroyed when the context is lost.
  const raw_ptr<content::BrowserContext> browser_context_;

  base::ScopedObservation<UsbChooserContext, UsbChooserContext::DeviceObserver>
      device_observation_{this};
  base::ObserverList<content::UsbDelegate::Observer> observer_list_;
};

ElectronUsbDelegate::ElectronUsbDelegate() = default;

ElectronUsbDelegate::~ElectronUsbDelegate() = default;

void ElectronUsbDelegate::AdjustProtectedInterfaceClasses(
    content::BrowserContext* browser_context,
    const url::Origin& origin,
    content::RenderFrameHost* frame,
    std::vector<uint8_t>& classes) {
  auto* permission_manager = static_cast<ElectronPermissionManager*>(
      browser_context->GetPermissionControllerDelegate());
  classes = permission_manager->CheckProtectedUSBClasses(classes);
}

std::unique_ptr<content::UsbChooser> ElectronUsbDelegate::RunChooser(
    content::RenderFrameHost& frame,
    blink::mojom::WebUsbRequestDeviceOptionsPtr options,
    blink::mojom::WebUsbService::GetPermissionCallback callback) {
  UsbChooserController* controller = ControllerForFrame(&frame);
  if (controller) {
    DeleteControllerForFrame(&frame);
  }
  AddControllerForFrame(&frame, std::move(options), std::move(callback));
  // Return a nullptr because the return value isn't used for anything. The
  // return value is simply used in Chromium to cleanup the chooser UI once the
  // usb service is destroyed.
  return nullptr;
}

bool ElectronUsbDelegate::CanRequestDevicePermission(
    content::BrowserContext* browser_context,
    const url::Origin& origin) {
  if (!browser_context)
    return false;

  base::Value::Dict details;
  details.Set("securityOrigin", origin.GetURL().spec());
  auto* permission_manager = static_cast<ElectronPermissionManager*>(
      browser_context->GetPermissionControllerDelegate());
  return permission_manager->CheckPermissionWithDetails(
      static_cast<blink::PermissionType>(
          WebContentsPermissionHelper::PermissionType::USB),
      nullptr, origin.GetURL(), std::move(details));
}

void ElectronUsbDelegate::RevokeDevicePermissionWebInitiated(
    content::BrowserContext* browser_context,
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device) {
  auto* chooser_context = GetChooserContext(browser_context);
  if (chooser_context) {
    chooser_context->RevokeDevicePermissionWebInitiated(origin, device);
  }
}

const device::mojom::UsbDeviceInfo* ElectronUsbDelegate::GetDeviceInfo(
    content::BrowserContext* browser_context,
    const std::string& guid) {
  auto* chooser_context = GetChooserContext(browser_context);
  if (!chooser_context)
    return nullptr;
  return chooser_context->GetDeviceInfo(guid);
}

bool ElectronUsbDelegate::HasDevicePermission(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device_info) {
  if (IsDevicePermissionAutoGranted(origin, device_info))
    return true;

  auto* chooser_context = GetChooserContext(browser_context);
  if (!chooser_context)
    return false;

  return GetChooserContext(browser_context)
      ->HasDevicePermission(origin, device_info);
}

void ElectronUsbDelegate::GetDevices(
    content::BrowserContext* browser_context,
    blink::mojom::WebUsbService::GetDevicesCallback callback) {
  auto* chooser_context = GetChooserContext(browser_context);
  if (!chooser_context) {
    std::move(callback).Run(std::vector<device::mojom::UsbDeviceInfoPtr>());
    return;
  }
  chooser_context->GetDevices(std::move(callback));
}

void ElectronUsbDelegate::GetDevice(
    content::BrowserContext* browser_context,
    const std::string& guid,
    base::span<const uint8_t> blocked_interface_classes,
    mojo::PendingReceiver<device::mojom::UsbDevice> device_receiver,
    mojo::PendingRemote<device::mojom::UsbDeviceClient> device_client) {
  auto* chooser_context = GetChooserContext(browser_context);
  if (chooser_context) {
    chooser_context->GetDevice(guid, blocked_interface_classes,
                               std::move(device_receiver),
                               std::move(device_client));
  }
}

void ElectronUsbDelegate::AddObserver(content::BrowserContext* browser_context,
                                      Observer* observer) {
  if (!browser_context)
    return;

  GetContextObserver(browser_context)->AddObserver(observer);
}

void ElectronUsbDelegate::RemoveObserver(
    content::BrowserContext* browser_context,
    Observer* observer) {
  if (!browser_context)
    return;

  GetContextObserver(browser_context)->RemoveObserver(observer);
}

ElectronUsbDelegate::ContextObservation*
ElectronUsbDelegate::GetContextObserver(
    content::BrowserContext* browser_context) {
  CHECK(browser_context);
  if (!base::Contains(observations_, browser_context)) {
    observations_.emplace(browser_context, std::make_unique<ContextObservation>(
                                               this, browser_context));
  }
  return observations_[browser_context].get();
}

bool ElectronUsbDelegate::IsServiceWorkerAllowedForOrigin(
    const url::Origin& origin) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // WebUSB is only available on extension service workers for now.
  if (base::FeatureList::IsEnabled(
          features::kEnableWebUsbOnExtensionServiceWorker) &&
      origin.scheme() == extensions::kExtensionScheme) {
    return true;
  }
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  return false;
}

UsbChooserController* ElectronUsbDelegate::ControllerForFrame(
    content::RenderFrameHost* render_frame_host) {
  auto mapping = controller_map_.find(render_frame_host);
  return mapping == controller_map_.end() ? nullptr : mapping->second.get();
}

UsbChooserController* ElectronUsbDelegate::AddControllerForFrame(
    content::RenderFrameHost* render_frame_host,
    blink::mojom::WebUsbRequestDeviceOptionsPtr options,
    blink::mojom::WebUsbService::GetPermissionCallback callback) {
  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto controller = std::make_unique<UsbChooserController>(
      render_frame_host, std::move(options), std::move(callback), web_contents,
      weak_factory_.GetWeakPtr());
  controller_map_.insert(
      std::make_pair(render_frame_host, std::move(controller)));
  return ControllerForFrame(render_frame_host);
}

void ElectronUsbDelegate::DeleteControllerForFrame(
    content::RenderFrameHost* render_frame_host) {
  controller_map_.erase(render_frame_host);
}

bool ElectronUsbDelegate::PageMayUseUsb(content::Page& page) {
  content::RenderFrameHost& main_rfh = page.GetMainDocument();
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // WebViewGuests have no mechanism to show permission prompts and their
  // embedder can't grant USB access through its permissionrequest API. Also
  // since webviews use a separate StoragePartition, they must not gain access
  // through permissions granted in non-webview contexts.
  if (extensions::WebViewGuest::FromRenderFrameHost(&main_rfh)) {
    return false;
  }
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

  // USB permissions are scoped to a BrowserContext instead of a
  // StoragePartition, so we need to be careful about usage across
  // StoragePartitions. Until this is scoped correctly, we'll try to avoid
  // inappropriate sharing by restricting access to the API. We can't be as
  // strict as we'd like, as cases like extensions and Isolated Web Apps still
  // need USB access in non-default partitions, so we'll just guard against
  // HTTP(S) as that presents a clear risk for inappropriate sharing.
  // TODO(crbug.com/1469672): USB permissions should be explicitly scoped to
  // StoragePartitions.
  if (main_rfh.GetStoragePartition() !=
      main_rfh.GetBrowserContext()->GetDefaultStoragePartition()) {
    return !main_rfh.GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
  }

  return true;
}

}  // namespace electron
