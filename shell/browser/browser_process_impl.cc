// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/browser_process_impl.h"

#include <memory>

#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_switches.h"
#include "components/os_crypt/async/browser/key_provider.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/sync/os_crypt.h"
#include "components/prefs/in_memory_pref_store.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/overlay_user_pref_store.h"
#include "components/prefs/pref_registry.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/network_quality_observer_factory.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/common/content_switches.h"
#include "extensions/common/constants.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "services/device/public/cpp/geolocation/geolocation_system_permission_manager.h"
#include "services/network/public/cpp/network_switches.h"
#include "shell/browser/net/resolve_proxy_helper.h"
#include "shell/common/electron_paths.h"
#include "shell/common/thread_restrictions.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "chrome/browser/printing/print_job_manager.h"
#endif

BrowserProcessImpl::BrowserProcessImpl() {
  g_browser_process = this;
}

BrowserProcessImpl::~BrowserProcessImpl() {
  g_browser_process = nullptr;
}

// static
void BrowserProcessImpl::ApplyProxyModeFromCommandLine(
    ValueMapPrefStore* pref_store) {
  if (!pref_store)
    return;

  auto* command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch(switches::kNoProxyServer)) {
    pref_store->SetValue(proxy_config::prefs::kProxy,
                         base::Value(ProxyConfigDictionary::CreateDirect()),
                         WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  } else if (command_line->HasSwitch(switches::kProxyPacUrl)) {
    std::string pac_script_url =
        command_line->GetSwitchValueASCII(switches::kProxyPacUrl);
    pref_store->SetValue(proxy_config::prefs::kProxy,
                         base::Value(ProxyConfigDictionary::CreatePacScript(
                             pac_script_url, false /* pac_mandatory */)),
                         WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  } else if (command_line->HasSwitch(switches::kProxyAutoDetect)) {
    pref_store->SetValue(proxy_config::prefs::kProxy,
                         base::Value(ProxyConfigDictionary::CreateAutoDetect()),
                         WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  } else if (command_line->HasSwitch(switches::kProxyServer)) {
    std::string proxy_server =
        command_line->GetSwitchValueASCII(switches::kProxyServer);
    std::string bypass_list =
        command_line->GetSwitchValueASCII(switches::kProxyBypassList);
    pref_store->SetValue(proxy_config::prefs::kProxy,
                         base::Value(ProxyConfigDictionary::CreateFixedServers(
                             proxy_server, bypass_list)),
                         WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  }
}

BuildState* BrowserProcessImpl::GetBuildState() {
  NOTIMPLEMENTED();
  return nullptr;
}

GlobalFeatures* BrowserProcessImpl::GetFeatures() {
  NOTIMPLEMENTED();
  return nullptr;
}

void BrowserProcessImpl::PostEarlyInitialization() {
  PrefServiceFactory prefs_factory;
  auto pref_registry = base::MakeRefCounted<PrefRegistrySimple>();
  PrefProxyConfigTrackerImpl::RegisterPrefs(pref_registry.get());
#if BUILDFLAG(IS_WIN)
  OSCrypt::RegisterLocalPrefs(pref_registry.get());
#endif

  in_memory_pref_store_ = base::MakeRefCounted<ValueMapPrefStore>();
  ApplyProxyModeFromCommandLine(in_memory_pref_store());
  prefs_factory.set_command_line_prefs(in_memory_pref_store());

  // Only use a persistent prefs store when cookie encryption is enabled as that
  // is the only key that needs it
  base::FilePath prefs_path;
  CHECK(base::PathService::Get(electron::DIR_SESSION_DATA, &prefs_path));
  prefs_path = prefs_path.Append(FILE_PATH_LITERAL("Local State"));
  electron::ScopedAllowBlockingForElectron allow_blocking;
  scoped_refptr<JsonPrefStore> user_pref_store =
      base::MakeRefCounted<JsonPrefStore>(prefs_path);
  user_pref_store->ReadPrefs();
  prefs_factory.set_user_prefs(user_pref_store);
  local_state_ = prefs_factory.Create(std::move(pref_registry));
}

void BrowserProcessImpl::PreCreateThreads() {
  // chrome-extension:// URLs are safe to request anywhere, but may only
  // commit (including in iframes) in extension processes.
  content::ChildProcessSecurityPolicy::GetInstance()
      ->RegisterWebSafeIsolatedScheme(extensions::kExtensionScheme, true);
  // Must be created before the IOThread.
  // Once IOThread class is no longer needed,
  // this can be created on first use.
  if (!SystemNetworkContextManager::GetInstance())
    SystemNetworkContextManager::CreateInstance(local_state_.get());
}

void BrowserProcessImpl::PreMainMessageLoopRun() {
  CreateNetworkQualityObserver();
  CreateOSCryptAsync();
}

void BrowserProcessImpl::PostMainMessageLoopRun() {
  if (local_state_)
    local_state_->CommitPendingWrite();

  // This expects to be destroyed before the task scheduler is torn down.
  SystemNetworkContextManager::DeleteInstance();
}

bool BrowserProcessImpl::IsShuttingDown() {
  return false;
}

metrics_services_manager::MetricsServicesManager*
BrowserProcessImpl::GetMetricsServicesManager() {
  return nullptr;
}

metrics::MetricsService* BrowserProcessImpl::metrics_service() {
  return nullptr;
}

ProfileManager* BrowserProcessImpl::profile_manager() {
  return nullptr;
}

PrefService* BrowserProcessImpl::local_state() {
  DCHECK(local_state_.get());
  return local_state_.get();
}

signin::ActivePrimaryAccountsMetricsRecorder*
BrowserProcessImpl::active_primary_accounts_metrics_recorder() {
  return nullptr;
}

scoped_refptr<network::SharedURLLoaderFactory>
BrowserProcessImpl::shared_url_loader_factory() {
  return system_network_context_manager()->GetSharedURLLoaderFactory();
}

variations::VariationsService* BrowserProcessImpl::variations_service() {
  return nullptr;
}

BrowserProcessPlatformPart* BrowserProcessImpl::platform_part() {
  return nullptr;
}

NotificationUIManager* BrowserProcessImpl::notification_ui_manager() {
  return nullptr;
}

NotificationPlatformBridge* BrowserProcessImpl::notification_platform_bridge() {
  return nullptr;
}

SystemNetworkContextManager*
BrowserProcessImpl::system_network_context_manager() {
  DCHECK(SystemNetworkContextManager::GetInstance());
  return SystemNetworkContextManager::GetInstance();
}

network::NetworkQualityTracker* BrowserProcessImpl::network_quality_tracker() {
  return nullptr;
}

embedder_support::OriginTrialsSettingsStorage*
BrowserProcessImpl::GetOriginTrialsSettingsStorage() {
  return &origin_trials_settings_storage_;
}

policy::ChromeBrowserPolicyConnector*
BrowserProcessImpl::browser_policy_connector() {
  return nullptr;
}

policy::PolicyService* BrowserProcessImpl::policy_service() {
  return nullptr;
}

IconManager* BrowserProcessImpl::icon_manager() {
  return nullptr;
}

GpuModeManager* BrowserProcessImpl::gpu_mode_manager() {
  return nullptr;
}

printing::PrintPreviewDialogController*
BrowserProcessImpl::print_preview_dialog_controller() {
  return nullptr;
}

printing::BackgroundPrintingManager*
BrowserProcessImpl::background_printing_manager() {
  return nullptr;
}

IntranetRedirectDetector* BrowserProcessImpl::intranet_redirect_detector() {
  return nullptr;
}

DownloadStatusUpdater* BrowserProcessImpl::download_status_updater() {
  return nullptr;
}

DownloadRequestLimiter* BrowserProcessImpl::download_request_limiter() {
  return nullptr;
}

BackgroundModeManager* BrowserProcessImpl::background_mode_manager() {
  return nullptr;
}

StatusTray* BrowserProcessImpl::status_tray() {
  return nullptr;
}

safe_browsing::SafeBrowsingService*
BrowserProcessImpl::safe_browsing_service() {
  return nullptr;
}

subresource_filter::RulesetService*
BrowserProcessImpl::subresource_filter_ruleset_service() {
  return nullptr;
}

component_updater::ComponentUpdateService*
BrowserProcessImpl::component_updater() {
  return nullptr;
}

MediaFileSystemRegistry* BrowserProcessImpl::media_file_system_registry() {
  return nullptr;
}

WebRtcLogUploader* BrowserProcessImpl::webrtc_log_uploader() {
  return nullptr;
}

network_time::NetworkTimeTracker* BrowserProcessImpl::network_time_tracker() {
  return nullptr;
}

gcm::GCMDriver* BrowserProcessImpl::gcm_driver() {
  return nullptr;
}

resource_coordinator::ResourceCoordinatorParts*
BrowserProcessImpl::resource_coordinator_parts() {
  return nullptr;
}

resource_coordinator::TabManager* BrowserProcessImpl::GetTabManager() {
  return nullptr;
}

SerialPolicyAllowedPorts* BrowserProcessImpl::serial_policy_allowed_ports() {
  return nullptr;
}

HidSystemTrayIcon* BrowserProcessImpl::hid_system_tray_icon() {
  return nullptr;
}

UsbSystemTrayIcon* BrowserProcessImpl::usb_system_tray_icon() {
  return nullptr;
}

subresource_filter::RulesetService*
BrowserProcessImpl::fingerprinting_protection_ruleset_service() {
  return nullptr;
}

os_crypt_async::OSCryptAsync* BrowserProcessImpl::os_crypt_async() {
  return os_crypt_async_.get();
}

void BrowserProcessImpl::set_additional_os_crypt_async_provider_for_test(
    size_t precedence,
    std::unique_ptr<os_crypt_async::KeyProvider> provider) {}

void BrowserProcessImpl::SetSystemLocale(const std::string& locale) {
  system_locale_ = locale;
}

const std::string& BrowserProcessImpl::GetSystemLocale() const {
  return system_locale_;
}

electron::ResolveProxyHelper* BrowserProcessImpl::GetResolveProxyHelper() {
  if (!resolve_proxy_helper_) {
    resolve_proxy_helper_ = base::MakeRefCounted<electron::ResolveProxyHelper>(
        nullptr /* browser_context */);
  }
  return resolve_proxy_helper_.get();
}

#if BUILDFLAG(IS_LINUX)
void BrowserProcessImpl::SetLinuxStorageBackend(
    os_crypt::SelectedLinuxBackend selected_backend) {
  switch (selected_backend) {
    case os_crypt::SelectedLinuxBackend::BASIC_TEXT:
      selected_linux_storage_backend_ = "basic_text";
      break;
    case os_crypt::SelectedLinuxBackend::GNOME_LIBSECRET:
      selected_linux_storage_backend_ = "gnome_libsecret";
      break;
    case os_crypt::SelectedLinuxBackend::KWALLET:
      selected_linux_storage_backend_ = "kwallet";
      break;
    case os_crypt::SelectedLinuxBackend::KWALLET5:
      selected_linux_storage_backend_ = "kwallet5";
      break;
    case os_crypt::SelectedLinuxBackend::KWALLET6:
      selected_linux_storage_backend_ = "kwallet6";
      break;
    case os_crypt::SelectedLinuxBackend::DEFER:
      NOTREACHED();
  }
}
#endif  // BUILDFLAG(IS_LINUX)

void BrowserProcessImpl::SetApplicationLocale(const std::string& locale) {
  locale_ = locale;
}

const std::string& BrowserProcessImpl::GetApplicationLocale() {
  return locale_;
}

printing::PrintJobManager* BrowserProcessImpl::print_job_manager() {
#if BUILDFLAG(ENABLE_PRINTING)
  if (!print_job_manager_)
    print_job_manager_ = std::make_unique<printing::PrintJobManager>();
  return print_job_manager_.get();
#else
  return nullptr;
#endif
}

StartupData* BrowserProcessImpl::startup_data() {
  return nullptr;
}

network::NetworkQualityTracker* BrowserProcessImpl::GetNetworkQualityTracker() {
  if (!network_quality_tracker_) {
    network_quality_tracker_ = std::make_unique<network::NetworkQualityTracker>(
        base::BindRepeating(&content::GetNetworkService));
  }
  return network_quality_tracker_.get();
}

void BrowserProcessImpl::CreateNetworkQualityObserver() {
  DCHECK(!network_quality_observer_);
  network_quality_observer_ =
      content::CreateNetworkQualityObserver(GetNetworkQualityTracker());
  DCHECK(network_quality_observer_);
}

void BrowserProcessImpl::CreateOSCryptAsync() {
  // source: https://chromium-review.googlesource.com/c/chromium/src/+/4455776

  // For now, initialize OSCryptAsync with no providers. This delegates all
  // encryption operations to OSCrypt.
  // TODO(crbug.com/1373092): Add providers behind features, as support for them
  // is added.
  os_crypt_async_ = std::make_unique<os_crypt_async::OSCryptAsync>(
      std::vector<
          std::pair<size_t, std::unique_ptr<os_crypt_async::KeyProvider>>>());

  // Trigger async initialization of OSCrypt key providers.
  std::ignore = os_crypt_async_->GetInstance(base::DoNothing());
}
