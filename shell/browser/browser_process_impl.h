// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This interface is for managing the global services of the application. Each
// service is lazily created when requested the first time. The service getters
// will return nullptr if the service is not available, so callers must check
// for this condition.

#ifndef ELECTRON_SHELL_BROWSER_BROWSER_PROCESS_IMPL_H_
#define ELECTRON_SHELL_BROWSER_BROWSER_PROCESS_IMPL_H_

#include <memory>
#include <string>

#include "chrome/browser/browser_process.h"
#include "components/embedder_support/origin_trials/origin_trials_settings_storage.h"
#include "components/prefs/value_map_pref_store.h"
#include "printing/buildflags/buildflags.h"
#include "services/network/public/cpp/network_quality_tracker.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "shell/browser/net/system_network_context_manager.h"

#if BUILDFLAG(IS_LINUX)
#include "components/os_crypt/sync/key_storage_util_linux.h"
#endif

class PrefService;

namespace printing {
class PrintJobManager;
}

namespace electron {
class ResolveProxyHelper;
}

// Empty definition for std::unique_ptr, rather than a forward declaration
class BackgroundModeManager {};

// NOT THREAD SAFE, call only from the main thread.
// These functions shouldn't return nullptr unless otherwise noted.
class BrowserProcessImpl : public BrowserProcess {
 public:
  BrowserProcessImpl();
  ~BrowserProcessImpl() override;

  // disable copy
  BrowserProcessImpl(const BrowserProcessImpl&) = delete;
  BrowserProcessImpl& operator=(const BrowserProcessImpl&) = delete;

  static void ApplyProxyModeFromCommandLine(ValueMapPrefStore* pref_store);

  void PostEarlyInitialization();
  void PreCreateThreads();
  void PreMainMessageLoopRun();
  void PostDestroyThreads() {}
  void PostMainMessageLoopRun();
  void SetSystemLocale(const std::string& locale);
  const std::string& GetSystemLocale() const;
  electron::ResolveProxyHelper* GetResolveProxyHelper();

#if BUILDFLAG(IS_LINUX)
  void SetLinuxStorageBackend(os_crypt::SelectedLinuxBackend selected_backend);
  [[nodiscard]] const std::string& linux_storage_backend() const {
    return selected_linux_storage_backend_;
  }
#endif

  // BrowserProcess
  BuildState* GetBuildState() override;
  GlobalFeatures* GetFeatures() override;
  void EndSession() override {}
  void FlushLocalStateAndReply(base::OnceClosure reply) override {}
  bool IsShuttingDown() override;

  metrics_services_manager::MetricsServicesManager* GetMetricsServicesManager()
      override;
  metrics::MetricsService* metrics_service() override;
  ProfileManager* profile_manager() override;
  PrefService* local_state() override;
  signin::ActivePrimaryAccountsMetricsRecorder*
  active_primary_accounts_metrics_recorder() override;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory()
      override;
  variations::VariationsService* variations_service() override;
  BrowserProcessPlatformPart* platform_part() override;
  NotificationUIManager* notification_ui_manager() override;
  NotificationPlatformBridge* notification_platform_bridge() override;
  SystemNetworkContextManager* system_network_context_manager() override;
  network::NetworkQualityTracker* network_quality_tracker() override;
  embedder_support::OriginTrialsSettingsStorage*
  GetOriginTrialsSettingsStorage() override;
  policy::ChromeBrowserPolicyConnector* browser_policy_connector() override;
  policy::PolicyService* policy_service() override;
  IconManager* icon_manager() override;
  GpuModeManager* gpu_mode_manager() override;
  printing::PrintPreviewDialogController* print_preview_dialog_controller()
      override;
  printing::BackgroundPrintingManager* background_printing_manager() override;
  IntranetRedirectDetector* intranet_redirect_detector() override;
  DownloadStatusUpdater* download_status_updater() override;
  DownloadRequestLimiter* download_request_limiter() override;
  BackgroundModeManager* background_mode_manager() override;
  StatusTray* status_tray() override;
  safe_browsing::SafeBrowsingService* safe_browsing_service() override;
  subresource_filter::RulesetService* subresource_filter_ruleset_service()
      override;
  component_updater::ComponentUpdateService* component_updater() override;
  MediaFileSystemRegistry* media_file_system_registry() override;
  WebRtcLogUploader* webrtc_log_uploader() override;
  network_time::NetworkTimeTracker* network_time_tracker() override;
  gcm::GCMDriver* gcm_driver() override;
  resource_coordinator::ResourceCoordinatorParts* resource_coordinator_parts()
      override;
  resource_coordinator::TabManager* GetTabManager() override;
  SerialPolicyAllowedPorts* serial_policy_allowed_ports() override;
  HidSystemTrayIcon* hid_system_tray_icon() override;
  UsbSystemTrayIcon* usb_system_tray_icon() override;
  os_crypt_async::OSCryptAsync* os_crypt_async() override;
  void set_additional_os_crypt_async_provider_for_test(
      size_t precedence,
      std::unique_ptr<os_crypt_async::KeyProvider> provider) override;
  void CreateDevToolsProtocolHandler() override {}
  void CreateDevToolsAutoOpener() override {}
  void set_background_mode_manager_for_test(
      std::unique_ptr<BackgroundModeManager> manager) override {}
#if (BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX))
  void StartAutoupdateTimer() override {}
#endif
  void SetApplicationLocale(const std::string& locale) override;
  const std::string& GetApplicationLocale() override;
  printing::PrintJobManager* print_job_manager() override;
  StartupData* startup_data() override;
  subresource_filter::RulesetService*
  fingerprinting_protection_ruleset_service() override;

  ValueMapPrefStore* in_memory_pref_store() const {
    return in_memory_pref_store_.get();
  }

 private:
  void CreateNetworkQualityObserver();
  void CreateOSCryptAsync();
  network::NetworkQualityTracker* GetNetworkQualityTracker();

#if BUILDFLAG(ENABLE_PRINTING)
  std::unique_ptr<printing::PrintJobManager> print_job_manager_;
#endif
  std::unique_ptr<PrefService> local_state_;
  std::string locale_;
  std::string system_locale_;
#if BUILDFLAG(IS_LINUX)
  std::string selected_linux_storage_backend_;
#endif
  embedder_support::OriginTrialsSettingsStorage origin_trials_settings_storage_;

  scoped_refptr<ValueMapPrefStore> in_memory_pref_store_;
  scoped_refptr<electron::ResolveProxyHelper> resolve_proxy_helper_;
  std::unique_ptr<network::NetworkQualityTracker> network_quality_tracker_;
  std::unique_ptr<
      network::NetworkQualityTracker::RTTAndThroughputEstimatesObserver>
      network_quality_observer_;

  std::unique_ptr<os_crypt_async::OSCryptAsync> os_crypt_async_;
};

#endif  // ELECTRON_SHELL_BROWSER_BROWSER_PROCESS_IMPL_H_
