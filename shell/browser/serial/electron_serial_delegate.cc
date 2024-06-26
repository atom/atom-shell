// Copyright (c) 2020 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/serial/electron_serial_delegate.h"

#include <utility>

#include "base/feature_list.h"
#include "content/public/browser/web_contents.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/serial/serial_chooser_context.h"
#include "shell/browser/serial/serial_chooser_context_factory.h"
#include "shell/browser/serial/serial_chooser_controller.h"
#include "shell/browser/web_contents_permission_helper.h"

namespace electron {

SerialChooserContext* GetChooserContext(content::RenderFrameHost* frame) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  auto* browser_context = web_contents->GetBrowserContext();
  return SerialChooserContextFactory::GetForBrowserContext(browser_context);
}

ElectronSerialDelegate::ElectronSerialDelegate() = default;

ElectronSerialDelegate::~ElectronSerialDelegate() = default;

std::unique_ptr<content::SerialChooser> ElectronSerialDelegate::RunChooser(
    content::RenderFrameHost* frame,
    std::vector<blink::mojom::SerialPortFilterPtr> filters,
    std::vector<device::BluetoothUUID> allowed_bluetooth_service_class_ids,
    content::SerialChooser::Callback callback) {
  SerialChooserController* controller = ControllerForFrame(frame);
  if (controller) {
    DeleteControllerForFrame(frame);
  }
  AddControllerForFrame(frame, std::move(filters),
                        std::move(allowed_bluetooth_service_class_ids),
                        std::move(callback));

  // Return a nullptr because the return value isn't used for anything, eg
  // there is no mechanism to cancel navigator.serial.requestPort(). The return
  // value is simply used in Chromium to cleanup the chooser UI once the serial
  // service is destroyed.
  return nullptr;
}

bool ElectronSerialDelegate::CanRequestPortPermission(
    content::RenderFrameHost* frame) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  return permission_helper->CheckSerialAccessPermission(
      web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin());
}

bool ElectronSerialDelegate::HasPortPermission(
    content::RenderFrameHost* frame,
    const device::mojom::SerialPortInfo& port) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  return GetChooserContext(frame)->HasPortPermission(
      web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin(), port,
      frame);
}

void ElectronSerialDelegate::RevokePortPermissionWebInitiated(
    content::RenderFrameHost* frame,
    const base::UnguessableToken& token) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  return GetChooserContext(frame)->RevokePortPermissionWebInitiated(
      web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin(), token,
      frame);
}

const device::mojom::SerialPortInfo* ElectronSerialDelegate::GetPortInfo(
    content::RenderFrameHost* frame,
    const base::UnguessableToken& token) {
  return GetChooserContext(frame)->GetPortInfo(token);
}

device::mojom::SerialPortManager* ElectronSerialDelegate::GetPortManager(
    content::RenderFrameHost* frame) {
  return GetChooserContext(frame)->GetPortManager();
}

void ElectronSerialDelegate::AddObserver(
    content::RenderFrameHost* frame,
    content::SerialDelegate::Observer* observer) {
  observer_list_.AddObserver(observer);
  auto* chooser_context = GetChooserContext(frame);
  if (!port_observation_.IsObserving())
    port_observation_.Observe(chooser_context);
}

void ElectronSerialDelegate::RemoveObserver(
    content::RenderFrameHost* frame,
    content::SerialDelegate::Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

SerialChooserController* ElectronSerialDelegate::ControllerForFrame(
    content::RenderFrameHost* render_frame_host) {
  auto mapping = controller_map_.find(render_frame_host);
  return mapping == controller_map_.end() ? nullptr : mapping->second.get();
}

SerialChooserController* ElectronSerialDelegate::AddControllerForFrame(
    content::RenderFrameHost* render_frame_host,
    std::vector<blink::mojom::SerialPortFilterPtr> filters,
    std::vector<device::BluetoothUUID> allowed_bluetooth_service_class_ids,
    content::SerialChooser::Callback callback) {
  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto controller = std::make_unique<SerialChooserController>(
      render_frame_host, std::move(filters),
      std::move(allowed_bluetooth_service_class_ids), std::move(callback),
      web_contents, weak_factory_.GetWeakPtr());
  controller_map_.insert(
      std::make_pair(render_frame_host, std::move(controller)));
  return ControllerForFrame(render_frame_host);
}

void ElectronSerialDelegate::DeleteControllerForFrame(
    content::RenderFrameHost* render_frame_host) {
  controller_map_.erase(render_frame_host);
}

// SerialChooserContext::PortObserver:
void ElectronSerialDelegate::OnPortAdded(
    const device::mojom::SerialPortInfo& port) {
  for (auto& observer : observer_list_)
    observer.OnPortAdded(port);
}

void ElectronSerialDelegate::OnPortRemoved(
    const device::mojom::SerialPortInfo& port) {
  for (auto& observer : observer_list_)
    observer.OnPortRemoved(port);
}

void ElectronSerialDelegate::OnPortManagerConnectionError() {
  port_observation_.Reset();
  for (auto& observer : observer_list_)
    observer.OnPortManagerConnectionError();
}

void ElectronSerialDelegate::OnSerialChooserContextShutdown() {
  port_observation_.Reset();
}

}  // namespace electron
