// Copyright (c) 2020 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_SERIAL_ELECTRON_SERIAL_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_SERIAL_ELECTRON_SERIAL_DELEGATE_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/serial_delegate.h"
#include "shell/browser/serial/serial_chooser_context.h"
#include "shell/browser/serial/serial_chooser_controller.h"

namespace electron {

class SerialChooserController;

class ElectronSerialDelegate : public content::SerialDelegate,
                               private SerialChooserContext::PortObserver {
 public:
  ElectronSerialDelegate();
  ~ElectronSerialDelegate() override;

  // disable copy
  ElectronSerialDelegate(const ElectronSerialDelegate&) = delete;
  ElectronSerialDelegate& operator=(const ElectronSerialDelegate&) = delete;

  // content::SerialDelegate:
  std::unique_ptr<content::SerialChooser> RunChooser(
      content::RenderFrameHost* frame,
      std::vector<blink::mojom::SerialPortFilterPtr> filters,
      std::vector<device::BluetoothUUID> allowed_bluetooth_service_class_ids,
      content::SerialChooser::Callback callback) override;
  bool CanRequestPortPermission(content::RenderFrameHost* frame) override;
  bool HasPortPermission(content::RenderFrameHost* frame,
                         const device::mojom::SerialPortInfo& port) override;
  void RevokePortPermissionWebInitiated(
      content::RenderFrameHost* frame,
      const base::UnguessableToken& token) override;
  const device::mojom::SerialPortInfo* GetPortInfo(
      content::RenderFrameHost* frame,
      const base::UnguessableToken& token) override;
  device::mojom::SerialPortManager* GetPortManager(
      content::RenderFrameHost* frame) override;
  void AddObserver(content::RenderFrameHost* frame,
                   content::SerialDelegate::Observer* observer) override;
  void RemoveObserver(content::RenderFrameHost* frame,
                      content::SerialDelegate::Observer* observer) override;

  void DeleteControllerForFrame(content::RenderFrameHost* render_frame_host);

  // SerialChooserContext::PortObserver:
  void OnPortAdded(const device::mojom::SerialPortInfo& port) override;
  void OnPortRemoved(const device::mojom::SerialPortInfo& port) override;
  void OnPortConnectedStateChanged(
      const device::mojom::SerialPortInfo& port) override {}
  void OnPortManagerConnectionError() override;
  void OnPermissionRevoked(const url::Origin& origin) override {}
  void OnSerialChooserContextShutdown() override;

 private:
  SerialChooserController* ControllerForFrame(
      content::RenderFrameHost* render_frame_host);
  SerialChooserController* AddControllerForFrame(
      content::RenderFrameHost* render_frame_host,
      std::vector<blink::mojom::SerialPortFilterPtr> filters,
      std::vector<device::BluetoothUUID> allowed_bluetooth_service_class_ids,
      content::SerialChooser::Callback callback);

  base::ScopedObservation<SerialChooserContext,
                          SerialChooserContext::PortObserver>
      port_observation_{this};
  base::ObserverList<content::SerialDelegate::Observer> observer_list_;

  std::unordered_map<content::RenderFrameHost*,
                     std::unique_ptr<SerialChooserController>>
      controller_map_;

  base::WeakPtrFactory<ElectronSerialDelegate> weak_factory_{this};
};

}  // namespace electron

namespace base {

template <>
struct ScopedObservationTraits<electron::SerialChooserContext,
                               electron::SerialChooserContext::PortObserver> {
  static void AddObserver(
      electron::SerialChooserContext* source,
      electron::SerialChooserContext::PortObserver* observer) {
    source->AddPortObserver(observer);
  }
  static void RemoveObserver(
      electron::SerialChooserContext* source,
      electron::SerialChooserContext::PortObserver* observer) {
    source->RemovePortObserver(observer);
  }
};

}  // namespace base

#endif  // ELECTRON_SHELL_BROWSER_SERIAL_ELECTRON_SERIAL_DELEGATE_H_
