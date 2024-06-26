// Copyright (c) 2021 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_HID_HID_CHOOSER_CONTROLLER_H_
#define ELECTRON_SHELL_BROWSER_HID_HID_CHOOSER_CONTROLLER_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/hid_chooser.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "services/device/public/mojom/hid.mojom-forward.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/hid/electron_hid_delegate.h"
#include "shell/browser/hid/hid_chooser_context.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"
#include "third_party/blink/public/mojom/hid/hid.mojom.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace electron {

class ElectronHidDelegate;

class HidChooserContext;

// HidChooserController provides data for the WebHID API permission prompt.
class HidChooserController
    : private content::WebContentsObserver,
      private electron::HidChooserContext::DeviceObserver {
 public:
  // Construct a chooser controller for Human Interface Devices (HID).
  // |render_frame_host| is used to initialize the chooser strings and to access
  // the requesting and embedding origins. |callback| is called when the chooser
  // is closed, either by selecting an item or by dismissing the chooser dialog.
  // The callback is called with the selected device, or nullptr if no device is
  // selected.
  HidChooserController(
      content::RenderFrameHost* render_frame_host,
      std::vector<blink::mojom::HidDeviceFilterPtr> filters,
      std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
      content::HidChooser::Callback callback,
      content::WebContents* web_contents,
      base::WeakPtr<ElectronHidDelegate> hid_delegate);
  HidChooserController(HidChooserController&) = delete;
  HidChooserController& operator=(HidChooserController&) = delete;
  ~HidChooserController() override;

  // static
  static const std::string& PhysicalDeviceIdFromDeviceInfo(
      const device::mojom::HidDeviceInfo& device);

  // HidChooserContext::DeviceObserver:
  void OnDeviceAdded(const device::mojom::HidDeviceInfo& device_info) override;
  void OnDeviceRemoved(
      const device::mojom::HidDeviceInfo& device_info) override;
  void OnDeviceChanged(
      const device::mojom::HidDeviceInfo& device_info) override;
  void OnHidManagerConnectionError() override;
  void OnHidChooserContextShutdown() override;

  // content::WebContentsObserver:
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;

 private:
  api::Session* GetSession();
  void OnGotDevices(std::vector<device::mojom::HidDeviceInfoPtr> devices);
  bool DisplayDevice(const device::mojom::HidDeviceInfo& device) const;
  bool FilterMatchesAny(const device::mojom::HidDeviceInfo& device) const;
  bool IsExcluded(const device::mojom::HidDeviceInfo& device) const;
  void AddMessageToConsole(blink::mojom::ConsoleMessageLevel level,
                           const std::string& message) const;

  // Add |device_info| to |device_map_|. The device is added to the chooser item
  // representing the physical device. If the chooser item does not yet exist, a
  // new item is appended. Returns true if an item was appended.
  bool AddDeviceInfo(const device::mojom::HidDeviceInfo& device_info);

  // Remove |device_info| from |device_map_|. The device info is removed from
  // the chooser item representing the physical device. If this would cause the
  // item to be empty, the chooser item is removed. Does nothing if the device
  // is not in the chooser item. Returns true if an item was removed.
  bool RemoveDeviceInfo(const device::mojom::HidDeviceInfo& device_info);

  // Update the information for the device described by |device_info| in the
  // |device_map_|.
  void UpdateDeviceInfo(const device::mojom::HidDeviceInfo& device_info);

  void RunCallback(std::vector<device::mojom::HidDeviceInfoPtr> devices);
  void OnDeviceChosen(gin::Arguments* args);

  std::vector<blink::mojom::HidDeviceFilterPtr> filters_;
  std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters_;
  content::HidChooser::Callback callback_;
  content::WeakDocumentPtr initiator_document_;
  const url::Origin origin_;

  // The lifetime of the chooser context is tied to the browser context used to
  // create it, and may be destroyed while the chooser is still active.
  base::WeakPtr<HidChooserContext> chooser_context_;

  // Information about connected devices and their HID interfaces. A single
  // physical device may expose multiple HID interfaces. Keys are physical
  // device IDs, values are collections of HidDeviceInfo objects representing
  // the HID interfaces hosted by the physical device.
  std::map<std::string, std::vector<device::mojom::HidDeviceInfoPtr>>
      device_map_;

  // An ordered list of physical device IDs that determines the order of items
  // in the chooser.
  std::vector<std::string> items_;

  base::ScopedObservation<HidChooserContext, HidChooserContext::DeviceObserver>
      observation_{this};

  base::WeakPtr<ElectronHidDelegate> hid_delegate_;

  content::GlobalRenderFrameHostId render_frame_host_id_;

  base::WeakPtrFactory<HidChooserController> weak_factory_{this};
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_HID_HID_CHOOSER_CONTROLLER_H_
