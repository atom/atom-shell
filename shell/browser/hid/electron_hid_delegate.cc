// Copyright (c) 2021 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/hid/electron_hid_delegate.h"

#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/scoped_observation.h"
#include "chrome/common/chrome_features.h"
#include "content/public/browser/web_contents.h"
#include "electron/buildflags/buildflags.h"
#include "services/device/public/cpp/hid/hid_switches.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/browser/hid/hid_chooser_context.h"
#include "shell/browser/hid/hid_chooser_context_factory.h"
#include "shell/browser/hid/hid_chooser_controller.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "extensions/common/constants.h"
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

namespace {

electron::HidChooserContext* GetChooserContext(
    content::BrowserContext* browser_context) {
  if (!browser_context)
    return nullptr;
  return electron::HidChooserContextFactory::GetForBrowserContext(
      browser_context);
}

}  // namespace

namespace electron {

// Manages the HidDelegate observers for a single browser context.
class ElectronHidDelegate::ContextObservation
    : private HidChooserContext::DeviceObserver {
 public:
  ContextObservation(ElectronHidDelegate* parent,
                     content::BrowserContext* browser_context)
      : parent_(parent), browser_context_(browser_context) {
    if (auto* chooser_context = GetChooserContext(browser_context_))
      device_observation_.Observe(chooser_context);
  }

  ContextObservation(ContextObservation&) = delete;
  ContextObservation& operator=(ContextObservation&) = delete;
  ~ContextObservation() override = default;

  // HidChooserContext::DeviceObserver:
  void OnDeviceAdded(const device::mojom::HidDeviceInfo& device_info) override {
    for (auto& observer : observer_list_)
      observer.OnDeviceAdded(device_info);
  }

  void OnDeviceRemoved(
      const device::mojom::HidDeviceInfo& device_info) override {
    for (auto& observer : observer_list_)
      observer.OnDeviceRemoved(device_info);
  }

  void OnDeviceChanged(
      const device::mojom::HidDeviceInfo& device_info) override {
    for (auto& observer : observer_list_)
      observer.OnDeviceChanged(device_info);
  }

  void OnHidManagerConnectionError() override {
    for (auto& observer : observer_list_)
      observer.OnHidManagerConnectionError();
  }

  void OnHidChooserContextShutdown() override {
    parent_->observations_.erase(browser_context_);
    // Return since `this` is now deleted.
  }

  void AddObserver(content::HidDelegate::Observer* observer) {
    observer_list_.AddObserver(observer);
  }

  void RemoveObserver(content::HidDelegate::Observer* observer) {
    observer_list_.RemoveObserver(observer);
  }

 private:
  // Safe because `parent_` owns `this`.
  const raw_ptr<ElectronHidDelegate> parent_;

  // Safe because `this` is destroyed when the context is lost.
  const raw_ptr<content::BrowserContext> browser_context_;

  base::ScopedObservation<HidChooserContext, HidChooserContext::DeviceObserver>
      device_observation_{this};

  base::ObserverList<content::HidDelegate::Observer> observer_list_;
};

ElectronHidDelegate::ElectronHidDelegate() = default;

ElectronHidDelegate::~ElectronHidDelegate() = default;

std::unique_ptr<content::HidChooser> ElectronHidDelegate::RunChooser(
    content::RenderFrameHost* render_frame_host,
    std::vector<blink::mojom::HidDeviceFilterPtr> filters,
    std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
    content::HidChooser::Callback callback) {
  DCHECK(render_frame_host);
  auto* browser_context = render_frame_host->GetBrowserContext();

  // Start observing HidChooserContext for permission and device events.
  GetContextObserver(browser_context);
  DCHECK(base::Contains(observations_, browser_context));

  HidChooserController* controller = ControllerForFrame(render_frame_host);
  if (controller) {
    DeleteControllerForFrame(render_frame_host);
  }
  AddControllerForFrame(render_frame_host, std::move(filters),
                        std::move(exclusion_filters), std::move(callback));

  // Return a nullptr because the return value isn't used for anything, eg
  // there is no mechanism to cancel navigator.hid.requestDevice(). The return
  // value is simply used in Chromium to cleanup the chooser UI once the serial
  // service is destroyed.
  return nullptr;
}

bool ElectronHidDelegate::CanRequestDevicePermission(
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
          WebContentsPermissionHelper::PermissionType::HID),
      nullptr, origin.GetURL(), std::move(details));
}

bool ElectronHidDelegate::HasDevicePermission(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* render_frame_host,
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  return browser_context && GetChooserContext(browser_context)
                                ->HasDevicePermission(origin, device);
}

void ElectronHidDelegate::RevokeDevicePermission(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* render_frame_host,
    const url::Origin& origin,
    const device::mojom::HidDeviceInfo& device) {
  if (browser_context) {
    GetChooserContext(browser_context)->RevokeDevicePermission(origin, device);
  }
}

device::mojom::HidManager* ElectronHidDelegate::GetHidManager(
    content::BrowserContext* browser_context) {
  if (!browser_context)
    return nullptr;
  return GetChooserContext(browser_context)->GetHidManager();
}

void ElectronHidDelegate::AddObserver(content::BrowserContext* browser_context,
                                      Observer* observer) {
  if (!browser_context)
    return;
  GetContextObserver(browser_context)->AddObserver(observer);
}

void ElectronHidDelegate::RemoveObserver(
    content::BrowserContext* browser_context,
    content::HidDelegate::Observer* observer) {
  if (!browser_context)
    return;
  DCHECK(base::Contains(observations_, browser_context));
  GetContextObserver(browser_context)->RemoveObserver(observer);
}

const device::mojom::HidDeviceInfo* ElectronHidDelegate::GetDeviceInfo(
    content::BrowserContext* browser_context,
    const std::string& guid) {
  auto* chooser_context = GetChooserContext(browser_context);
  if (!chooser_context)
    return nullptr;
  return chooser_context->GetDeviceInfo(guid);
}

bool ElectronHidDelegate::IsFidoAllowedForOrigin(
    content::BrowserContext* browser_context,
    const url::Origin& origin) {
  auto* chooser_context = GetChooserContext(browser_context);
  return chooser_context && chooser_context->IsFidoAllowedForOrigin(origin);
}

bool ElectronHidDelegate::IsServiceWorkerAllowedForOrigin(
    const url::Origin& origin) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // WebHID is only available on extension service workers with feature flag
  // enabled for now.
  if (base::FeatureList::IsEnabled(
          features::kEnableWebHidOnExtensionServiceWorker) &&
      origin.scheme() == extensions::kExtensionScheme)
    return true;
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  return false;
}

ElectronHidDelegate::ContextObservation*
ElectronHidDelegate::GetContextObserver(
    content::BrowserContext* browser_context) {
  if (!base::Contains(observations_, browser_context)) {
    observations_.emplace(browser_context, std::make_unique<ContextObservation>(
                                               this, browser_context));
  }
  return observations_[browser_context].get();
}

HidChooserController* ElectronHidDelegate::ControllerForFrame(
    content::RenderFrameHost* render_frame_host) {
  auto mapping = controller_map_.find(render_frame_host);
  return mapping == controller_map_.end() ? nullptr : mapping->second.get();
}

HidChooserController* ElectronHidDelegate::AddControllerForFrame(
    content::RenderFrameHost* render_frame_host,
    std::vector<blink::mojom::HidDeviceFilterPtr> filters,
    std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
    content::HidChooser::Callback callback) {
  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto controller = std::make_unique<HidChooserController>(
      render_frame_host, std::move(filters), std::move(exclusion_filters),
      std::move(callback), web_contents, weak_factory_.GetWeakPtr());
  controller_map_.insert(
      std::make_pair(render_frame_host, std::move(controller)));
  return ControllerForFrame(render_frame_host);
}

void ElectronHidDelegate::DeleteControllerForFrame(
    content::RenderFrameHost* render_frame_host) {
  controller_map_.erase(render_frame_host);
}

}  // namespace electron
