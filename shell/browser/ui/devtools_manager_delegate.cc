// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "shell/browser/ui/devtools_manager_delegate.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/devtools_socket_factory.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/user_agent.h"
#include "electron/grit/electron_resources.h"
#include "net/base/net_errors.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "shell/browser/browser.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/common/electron_paths.h"
#include "third_party/inspector_protocol/crdtp/dispatch.h"
#include "ui/base/resource/resource_bundle.h"

namespace electron {

namespace {

class TCPServerSocketFactory : public content::DevToolsSocketFactory {
 public:
  TCPServerSocketFactory(const std::string& address, int port)
      : address_(address), port_(port) {}

  // disable copy
  TCPServerSocketFactory(const TCPServerSocketFactory&) = delete;
  TCPServerSocketFactory& operator=(const TCPServerSocketFactory&) = delete;

 private:
  // content::ServerSocketFactory.
  std::unique_ptr<net::ServerSocket> CreateForHttpServer() override {
    auto socket =
        std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
    if (socket->ListenWithAddressAndPort(address_, port_, 10) != net::OK)
      return std::unique_ptr<net::ServerSocket>();

    return socket;
  }
  std::unique_ptr<net::ServerSocket> CreateForTethering(
      std::string* name) override {
    return std::unique_ptr<net::ServerSocket>();
  }

  std::string address_;
  uint16_t port_;
};

std::unique_ptr<content::DevToolsSocketFactory> CreateSocketFactory() {
  auto& command_line = *base::CommandLine::ForCurrentProcess();
  // See if the user specified a port on the command line (useful for
  // automation). If not, use an ephemeral port by specifying 0.
  int port = 0;
  if (command_line.HasSwitch(switches::kRemoteDebuggingPort)) {
    int temp_port;
    std::string port_str =
        command_line.GetSwitchValueASCII(switches::kRemoteDebuggingPort);
    if (base::StringToInt(port_str, &temp_port) && temp_port >= 0 &&
        temp_port < 65535) {
      port = temp_port;
    } else {
      DLOG(WARNING) << "Invalid http debugger port number " << temp_port;
    }
  }
  return std::make_unique<TCPServerSocketFactory>("127.0.0.1", port);
}

const char kBrowserCloseMethod[] = "Browser.close";

}  // namespace

// DevToolsManagerDelegate ---------------------------------------------------

// static
void DevToolsManagerDelegate::StartHttpHandler() {
  base::FilePath session_data;
  base::PathService::Get(DIR_SESSION_DATA, &session_data);
  content::DevToolsAgentHost::StartRemoteDebuggingServer(
      CreateSocketFactory(), session_data, base::FilePath());
}

DevToolsManagerDelegate::DevToolsManagerDelegate() = default;

DevToolsManagerDelegate::~DevToolsManagerDelegate() = default;

void DevToolsManagerDelegate::Inspect(content::DevToolsAgentHost* agent_host) {}

void DevToolsManagerDelegate::HandleCommand(
    content::DevToolsAgentHostClientChannel* channel,
    base::span<const uint8_t> message,
    NotHandledCallback callback) {
  crdtp::Dispatchable dispatchable(crdtp::SpanFrom(message));
  DCHECK(dispatchable.ok());
  if (crdtp::SpanEquals(crdtp::SpanFrom(kBrowserCloseMethod),
                        dispatchable.Method())) {
    // In theory, we should respond over the protocol saying that the
    // Browser.close was handled. But doing so requires instantiating the
    // protocol UberDispatcher and generating proper protocol handlers.
    // Since we only have one method and it is supposed to close Electron,
    // we don't need to add this complexity. Should we decide to support
    // methods like Browser.setWindowBounds, we'll need to do it though.
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce([]() { Browser::Get()->Quit(); }));
    return;
  }
  std::move(callback).Run(message);
}

scoped_refptr<content::DevToolsAgentHost>
DevToolsManagerDelegate::CreateNewTarget(const GURL& url,
                                         TargetType target_type) {
  return nullptr;
}

std::string DevToolsManagerDelegate::GetDiscoveryPageHTML() {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
      IDR_CONTENT_SHELL_DEVTOOLS_DISCOVERY_PAGE);
}

bool DevToolsManagerDelegate::HasBundledFrontendResources() {
  return true;
}

content::BrowserContext* DevToolsManagerDelegate::GetDefaultBrowserContext() {
  return ElectronBrowserContext::From("", false);
}

}  // namespace electron
