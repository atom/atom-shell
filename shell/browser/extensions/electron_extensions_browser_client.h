// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSIONS_BROWSER_CLIENT_H_
#define ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSIONS_BROWSER_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "build/build_config.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/kiosk/kiosk_delegate.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "url/origin.h"

class PrefService;

namespace extensions {
class ExtensionsAPIClient;
class KioskDelegate;
class ProcessManagerDelegate;
class ElectronProcessManagerDelegate;
class ProcessMap;
class ElectronComponentExtensionResourceManager;
}  // namespace extensions

namespace electron {

// An ExtensionsBrowserClient that supports a single content::BrowserContext
// with no related incognito context.
class ElectronExtensionsBrowserClient
    : public extensions::ExtensionsBrowserClient {
 public:
  ElectronExtensionsBrowserClient();
  ~ElectronExtensionsBrowserClient() override;

  // disable copy
  ElectronExtensionsBrowserClient(const ElectronExtensionsBrowserClient&) =
      delete;
  ElectronExtensionsBrowserClient& operator=(
      const ElectronExtensionsBrowserClient&) = delete;

  // ExtensionsBrowserClient overrides:
  bool IsShuttingDown() override;
  bool AreExtensionsDisabled(const base::CommandLine& command_line,
                             content::BrowserContext* context) override;
  bool IsValidContext(void* context) override;
  bool IsSameContext(content::BrowserContext* first,
                     content::BrowserContext* second) override;
  bool HasOffTheRecordContext(content::BrowserContext* context) override;
  content::BrowserContext* GetOffTheRecordContext(
      content::BrowserContext* context) override;
  content::BrowserContext* GetOriginalContext(
      content::BrowserContext* context) override;
  content::BrowserContext* GetContextRedirectedToOriginal(
      content::BrowserContext* context,
      bool force_guest_profile) override;
  content::BrowserContext* GetContextOwnInstance(
      content::BrowserContext* context,
      bool force_guest_profile) override;
  content::BrowserContext* GetContextForOriginalOnly(
      content::BrowserContext* context,
      bool force_guest_profile) override;
  bool AreExtensionsDisabledForContext(
      content::BrowserContext* context) override;
  bool IsGuestSession(content::BrowserContext* context) const override;
  bool IsExtensionIncognitoEnabled(
      const std::string& extension_id,
      content::BrowserContext* context) const override;
  bool CanExtensionCrossIncognito(
      const extensions::Extension* extension,
      content::BrowserContext* context) const override;
  base::FilePath GetBundleResourcePath(
      const network::ResourceRequest& request,
      const base::FilePath& extension_resources_path,
      int* resource_id) const override;
  void LoadResourceFromResourceBundle(
      const network::ResourceRequest& request,
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      const base::FilePath& resource_relative_path,
      int resource_id,
      scoped_refptr<net::HttpResponseHeaders> headers,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client) override;
  bool AllowCrossRendererResourceLoad(
      const network::ResourceRequest& request,
      network::mojom::RequestDestination destination,
      ui::PageTransition page_transition,
      int child_id,
      bool is_incognito,
      const extensions::Extension* extension,
      const extensions::ExtensionSet& extensions,
      const extensions::ProcessMap& process_map) override;
  PrefService* GetPrefServiceForContext(
      content::BrowserContext* context) override;
  void GetEarlyExtensionPrefsObservers(
      content::BrowserContext* context,
      std::vector<extensions::EarlyExtensionPrefsObserver*>* observers)
      const override;
  extensions::ProcessManagerDelegate* GetProcessManagerDelegate()
      const override;
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
  GetControlledFrameEmbedderURLLoader(
      const url::Origin& app_origin,
      int frame_tree_node_id,
      content::BrowserContext* browser_context) override;
  std::unique_ptr<extensions::ExtensionHostDelegate>
  CreateExtensionHostDelegate() override;
  bool DidVersionUpdate(content::BrowserContext* context) override;
  void PermitExternalProtocolHandler() override;
  bool IsInDemoMode() override;
  bool IsScreensaverInDemoMode(const std::string& app_id) override;
  bool IsRunningInForcedAppMode() override;
  bool IsAppModeForcedForApp(
      const extensions::ExtensionId& extension_id) override;
  bool IsLoggedInAsPublicAccount() override;
  extensions::ExtensionSystemProvider* GetExtensionSystemFactory() override;
  std::unique_ptr<extensions::RuntimeAPIDelegate> CreateRuntimeAPIDelegate(
      content::BrowserContext* context) const override;
  const extensions::ComponentExtensionResourceManager*
  GetComponentExtensionResourceManager() override;
  void BroadcastEventToRenderers(
      extensions::events::HistogramValue histogram_value,
      const std::string& event_name,
      base::Value::List args,
      bool dispatch_to_off_the_record_profiles) override;
  extensions::ExtensionCache* GetExtensionCache() override;
  bool IsBackgroundUpdateAllowed() override;
  bool IsMinBrowserVersionSupported(const std::string& min_version) override;
  extensions::ExtensionWebContentsObserver* GetExtensionWebContentsObserver(
      content::WebContents* web_contents) override;
  extensions::KioskDelegate* GetKioskDelegate() override;
  bool IsLockScreenContext(content::BrowserContext* context) override;
  std::string GetApplicationLocale() override;
  std::string GetUserAgent() const override;
  void RegisterBrowserInterfaceBindersForFrame(
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map,
      content::RenderFrameHost* render_frame_host,
      const extensions::Extension* extension) const override;

  // Sets the API client.
  void SetAPIClientForTest(extensions::ExtensionsAPIClient* api_client);

 private:
  // Support for extension APIs.
  std::unique_ptr<extensions::ExtensionsAPIClient> api_client_;

  // Support for ProcessManager.
  std::unique_ptr<extensions::ElectronProcessManagerDelegate>
      process_manager_delegate_;

  // The extension cache used for download and installation.
  std::unique_ptr<extensions::ExtensionCache> extension_cache_;

  std::unique_ptr<extensions::KioskDelegate> kiosk_delegate_;

  std::unique_ptr<extensions::ElectronComponentExtensionResourceManager>
      resource_manager_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_EXTENSIONS_ELECTRON_EXTENSIONS_BROWSER_CLIENT_H_
