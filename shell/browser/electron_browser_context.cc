// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_browser_context.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/barrier_closure.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/predictors/preconnect_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/prefs/value_map_pref_store.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"  // nogncheck
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cors_origin_pattern_setter.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/shared_cors_origin_access_list.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents_media_capture_id.h"
#include "gin/arguments.h"
#include "media/audio/audio_device_description.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "shell/browser/cookie_change_notifier.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/browser/electron_browser_main_parts.h"
#include "shell/browser/electron_download_manager_delegate.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/browser/file_system_access/file_system_access_permission_context_factory.h"
#include "shell/browser/net/resolve_proxy_helper.h"
#include "shell/browser/protocol_registry.h"
#include "shell/browser/serial/serial_chooser_context.h"
#include "shell/browser/special_storage_policy.h"
#include "shell/browser/ui/inspectable_web_contents.h"
#include "shell/browser/ui/webui/accessibility_ui.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/browser/web_view_manager.h"
#include "shell/browser/zoom_level_delegate.h"
#include "shell/common/application_info.h"
#include "shell/common/electron_constants.h"
#include "shell/common/electron_paths.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/options_switches.h"
#include "shell/common/thread_restrictions.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/mojom/media/capture_handle_config.mojom.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "extensions/browser/browser_context_keyed_service_factories.h"
#include "extensions/browser/extension_pref_store.h"
#include "extensions/browser/extension_pref_value_map_factory.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/extension_api.h"
#include "shell/browser/extensions/electron_browser_context_keyed_service_factories.h"
#include "shell/browser/extensions/electron_extension_system.h"
#include "shell/browser/extensions/electron_extension_system_factory.h"
#include "shell/browser/extensions/electron_extensions_browser_client.h"
#include "shell/common/extensions/electron_extensions_client.h"
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS) || \
    BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/user_prefs/user_prefs.h"
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
#include "base/i18n/rtl.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/spellcheck/browser/pref_names.h"
#include "components/spellcheck/common/spellcheck_common.h"
#endif

using content::BrowserThread;

namespace electron {

namespace {

// Copied from chrome/browser/media/webrtc/desktop_capture_devices_util.cc.
media::mojom::CaptureHandlePtr CreateCaptureHandle(
    content::WebContents* capturer,
    const url::Origin& capturer_origin,
    const content::DesktopMediaID& captured_id) {
  if (capturer_origin.opaque()) {
    return nullptr;
  }

  content::RenderFrameHost* const captured_rfh =
      content::RenderFrameHost::FromID(
          captured_id.web_contents_id.render_process_id,
          captured_id.web_contents_id.main_render_frame_id);
  if (!captured_rfh || !captured_rfh->IsActive()) {
    return nullptr;
  }

  content::WebContents* const captured =
      content::WebContents::FromRenderFrameHost(captured_rfh);
  if (!captured) {
    return nullptr;
  }

  const auto& captured_config = captured->GetCaptureHandleConfig();
  if (!captured_config.all_origins_permitted &&
      std::ranges::none_of(
          captured_config.permitted_origins,
          [capturer_origin](const url::Origin& permitted_origin) {
            return capturer_origin.IsSameOriginWith(permitted_origin);
          })) {
    return nullptr;
  }

  // Observing CaptureHandle when either the capturing or the captured party
  // is incognito is disallowed, except for self-capture.
  if (capturer->GetPrimaryMainFrame() != captured->GetPrimaryMainFrame()) {
    if (capturer->GetBrowserContext()->IsOffTheRecord() ||
        captured->GetBrowserContext()->IsOffTheRecord()) {
      return nullptr;
    }
  }

  if (!captured_config.expose_origin &&
      captured_config.capture_handle.empty()) {
    return nullptr;
  }

  auto result = media::mojom::CaptureHandle::New();
  if (captured_config.expose_origin) {
    result->origin = captured->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  }
  result->capture_handle = captured_config.capture_handle;

  return result;
}

// Copied from chrome/browser/media/webrtc/desktop_capture_devices_util.cc.
std::optional<int> GetZoomLevel(content::WebContents* capturer,
                                const url::Origin& capturer_origin,
                                const content::DesktopMediaID& captured_id) {
  content::RenderFrameHost* const captured_rfh =
      content::RenderFrameHost::FromID(
          captured_id.web_contents_id.render_process_id,
          captured_id.web_contents_id.main_render_frame_id);
  if (!captured_rfh || !captured_rfh->IsActive()) {
    return std::nullopt;
  }

  content::WebContents* const captured_wc =
      content::WebContents::FromRenderFrameHost(captured_rfh);
  if (!captured_wc) {
    return std::nullopt;
  }

  double zoom_level = blink::ZoomLevelToZoomFactor(
      content::HostZoomMap::GetZoomLevel(captured_wc));
  return std::round(100 * zoom_level);
}

// Copied from chrome/browser/media/webrtc/desktop_capture_devices_util.cc.
media::mojom::DisplayMediaInformationPtr
DesktopMediaIDToDisplayMediaInformation(
    content::WebContents* capturer,
    const url::Origin& capturer_origin,
    const content::DesktopMediaID& media_id) {
  media::mojom::DisplayCaptureSurfaceType display_surface =
      media::mojom::DisplayCaptureSurfaceType::MONITOR;
  bool logical_surface = true;
  media::mojom::CursorCaptureType cursor =
      media::mojom::CursorCaptureType::NEVER;
#if defined(USE_AURA)
  const bool uses_aura =
      media_id.window_id != content::DesktopMediaID::kNullId ? true : false;
#else
  const bool uses_aura = false;
#endif  // defined(USE_AURA)

  media::mojom::CaptureHandlePtr capture_handle;
  int zoom_level = 100;
  switch (media_id.type) {
    case content::DesktopMediaID::TYPE_SCREEN:
      display_surface = media::mojom::DisplayCaptureSurfaceType::MONITOR;
      cursor = uses_aura ? media::mojom::CursorCaptureType::MOTION
                         : media::mojom::CursorCaptureType::ALWAYS;
      break;
    case content::DesktopMediaID::TYPE_WINDOW:
      display_surface = media::mojom::DisplayCaptureSurfaceType::WINDOW;
      cursor = uses_aura ? media::mojom::CursorCaptureType::MOTION
                         : media::mojom::CursorCaptureType::ALWAYS;
      break;
    case content::DesktopMediaID::TYPE_WEB_CONTENTS:
      display_surface = media::mojom::DisplayCaptureSurfaceType::BROWSER;
      cursor = media::mojom::CursorCaptureType::MOTION;
      capture_handle = CreateCaptureHandle(capturer, capturer_origin, media_id);
      zoom_level =
          GetZoomLevel(capturer, capturer_origin, media_id).value_or(100);
      break;
    case content::DesktopMediaID::TYPE_NONE:
      break;
  }

  return media::mojom::DisplayMediaInformation::New(
      display_surface, logical_surface, cursor, std::move(capture_handle),
      zoom_level);
}

// Convert string to lower case and escape it.
std::string MakePartitionName(const std::string& input) {
  return base::EscapePath(base::ToLowerASCII(input));
}

[[nodiscard]] content::DesktopMediaID GetAudioDesktopMediaId(
    const std::vector<std::string>& audio_device_ids) {
  // content::MediaStreamRequest provides a vector of ids
  // to allow user preference to influence which stream is
  // returned. This is a WIP upstream, so for now just use
  // the first device in the list.
  // Xref: https://chromium-review.googlesource.com/c/chromium/src/+/5132210
  if (!audio_device_ids.empty())
    return content::DesktopMediaID::Parse(audio_device_ids.front());
  return {};
}

bool DoesDeviceMatch(const base::Value& device,
                     const base::Value& device_to_compare,
                     const blink::PermissionType permission_type) {
  if (permission_type ==
          static_cast<blink::PermissionType>(
              WebContentsPermissionHelper::PermissionType::HID) ||
      permission_type ==
          static_cast<blink::PermissionType>(
              WebContentsPermissionHelper::PermissionType::USB)) {
    if (device.GetDict().FindInt(kDeviceVendorIdKey) !=
            device_to_compare.GetDict().FindInt(kDeviceVendorIdKey) ||
        device.GetDict().FindInt(kDeviceProductIdKey) !=
            device_to_compare.GetDict().FindInt(kDeviceProductIdKey)) {
      return false;
    }

    const auto* serial_number =
        device_to_compare.GetDict().FindString(kDeviceSerialNumberKey);
    const auto* device_serial_number =
        device.GetDict().FindString(kDeviceSerialNumberKey);

    if (serial_number && device_serial_number &&
        *device_serial_number == *serial_number)
      return true;
  } else if (permission_type ==
             static_cast<blink::PermissionType>(
                 WebContentsPermissionHelper::PermissionType::SERIAL)) {
#if BUILDFLAG(IS_WIN)
    const auto* instance_id = device.GetDict().FindString(kDeviceInstanceIdKey);
    const auto* port_instance_id =
        device_to_compare.GetDict().FindString(kDeviceInstanceIdKey);
    if (instance_id && port_instance_id && *instance_id == *port_instance_id)
      return true;
#else
    const auto* serial_number = device.GetDict().FindString(kSerialNumberKey);
    const auto* port_serial_number =
        device_to_compare.GetDict().FindString(kSerialNumberKey);
    if (device.GetDict().FindInt(kVendorIdKey) !=
            device_to_compare.GetDict().FindInt(kVendorIdKey) ||
        device.GetDict().FindInt(kProductIdKey) !=
            device_to_compare.GetDict().FindInt(kProductIdKey) ||
        (serial_number && port_serial_number &&
         *port_serial_number != *serial_number)) {
      return false;
    }

#if BUILDFLAG(IS_MAC)
    const auto* usb_driver_key = device.GetDict().FindString(kUsbDriverKey);
    const auto* port_usb_driver_key =
        device_to_compare.GetDict().FindString(kUsbDriverKey);
    if (usb_driver_key && port_usb_driver_key &&
        *usb_driver_key != *port_usb_driver_key) {
      return false;
    }
#endif  // BUILDFLAG(IS_MAC)
    return true;
#endif  // BUILDFLAG(IS_WIN)
  }
  return false;
}

}  // namespace

// static
ElectronBrowserContext::BrowserContextMap&
ElectronBrowserContext::browser_context_map() {
  static base::NoDestructor<ElectronBrowserContext::BrowserContextMap>
      browser_context_map;
  return *browser_context_map;
}

ElectronBrowserContext::ElectronBrowserContext(
    const PartitionOrPath partition_location,
    bool in_memory,
    base::Value::Dict options)
    : in_memory_pref_store_(new ValueMapPrefStore),
      storage_policy_(base::MakeRefCounted<SpecialStoragePolicy>()),
      protocol_registry_(base::WrapUnique(new ProtocolRegistry)),
      in_memory_(in_memory),
      ssl_config_(network::mojom::SSLConfig::New()) {
  // Read options.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  use_cache_ = !command_line->HasSwitch(switches::kDisableHttpCache);
  if (auto use_cache_opt = options.FindBool("cache")) {
    use_cache_ = use_cache_opt.value();
  }

  base::StringToInt(command_line->GetSwitchValueASCII(switches::kDiskCacheSize),
                    &max_cache_size_);

  if (auto* path_value = std::get_if<std::reference_wrapper<const std::string>>(
          &partition_location)) {
    base::PathService::Get(DIR_SESSION_DATA, &path_);
    const std::string& partition_loc = path_value->get();
    if (!in_memory && !partition_loc.empty()) {
      path_ = path_.Append(FILE_PATH_LITERAL("Partitions"))
                  .Append(base::FilePath::FromUTF8Unsafe(
                      MakePartitionName(partition_loc)));
    }
  } else if (auto* filepath_partition =
                 std::get_if<std::reference_wrapper<const base::FilePath>>(
                     &partition_location)) {
    const base::FilePath& partition_path = filepath_partition->get();
    path_ = std::move(partition_path);
  }

  BrowserContextDependencyManager::GetInstance()->MarkBrowserContextLive(this);

  // Initialize Pref Registry.
  InitPrefs();

  cookie_change_notifier_ = std::make_unique<CookieChangeNotifier>(this);

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  if (!in_memory_) {
    BrowserContextDependencyManager::GetInstance()
        ->CreateBrowserContextServices(this);

    extension_system_ = static_cast<extensions::ElectronExtensionSystem*>(
        extensions::ExtensionSystem::Get(this));
    extension_system_->InitForRegularProfile(true /* extensions_enabled */);
    extension_system_->FinishInitialization();
  }
#endif
}

ElectronBrowserContext::~ElectronBrowserContext() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  NotifyWillBeDestroyed();

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // the DestroyBrowserContextServices() call below frees this.
  extension_system_ = nullptr;
#endif

  // Notify any keyed services of browser context destruction.
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      this);
  ShutdownStoragePartitions();
}

void ElectronBrowserContext::InitPrefs() {
  auto prefs_path = GetPath().Append(FILE_PATH_LITERAL("Preferences"));
  ScopedAllowBlockingForElectron allow_blocking;
  PrefServiceFactory prefs_factory;
  scoped_refptr<JsonPrefStore> pref_store =
      base::MakeRefCounted<JsonPrefStore>(prefs_path);
  pref_store->ReadPrefs();  // Synchronous.
  prefs_factory.set_user_prefs(pref_store);
  prefs_factory.set_command_line_prefs(in_memory_pref_store());

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  if (!in_memory_) {
    auto* ext_pref_store = new ExtensionPrefStore(
        ExtensionPrefValueMapFactory::GetForBrowserContext(this),
        IsOffTheRecord());
    prefs_factory.set_extension_prefs(ext_pref_store);
  }
#endif

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS) || \
    BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
#else
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
#endif

  registry->RegisterFilePathPref(prefs::kSelectFileLastDirectory,
                                 base::FilePath());
  base::FilePath download_dir;
  base::PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &download_dir);
  registry->RegisterFilePathPref(prefs::kDownloadDefaultDirectory,
                                 download_dir);
  registry->RegisterDictionaryPref(prefs::kDevToolsFileSystemPaths);
  InspectableWebContents::RegisterPrefs(registry.get());
  MediaDeviceIDSalt::RegisterPrefs(registry.get());
  ZoomLevelDelegate::RegisterPrefs(registry.get());
  PrefProxyConfigTrackerImpl::RegisterPrefs(registry.get());
  ElectronAccessibilityUIMessageHandler::RegisterPrefs(registry.get());
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  if (!in_memory_)
    extensions::ExtensionPrefs::RegisterProfilePrefs(registry.get());
  extensions::PermissionsManager::RegisterProfilePrefs(registry.get());
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  BrowserContextDependencyManager::GetInstance()
      ->RegisterProfilePrefsForServices(registry.get());

  language::LanguagePrefs::RegisterProfilePrefs(registry.get());
#endif

  prefs_ = prefs_factory.Create(registry.get());
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS) || \
    BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  user_prefs::UserPrefs::Set(this, prefs_.get());
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  // No configured dictionaries, the default will be en-US
  if (prefs()->GetList(spellcheck::prefs::kSpellCheckDictionaries).empty()) {
    std::string default_code = spellcheck::GetCorrespondingSpellCheckLanguage(
        base::i18n::GetConfiguredLocale());
    if (!default_code.empty()) {
      base::Value::List language_codes;
      language_codes.Append(default_code);
      prefs()->Set(spellcheck::prefs::kSpellCheckDictionaries,
                   base::Value(std::move(language_codes)));
    }
  }
#endif
}

void ElectronBrowserContext::SetUserAgent(const std::string& user_agent) {
  user_agent_ = user_agent;
}

base::FilePath ElectronBrowserContext::GetPath() {
  return path_;
}

bool ElectronBrowserContext::IsOffTheRecord() {
  return in_memory_;
}

std::string ElectronBrowserContext::GetMediaDeviceIDSalt() {
  if (!media_device_id_salt_)
    media_device_id_salt_ = std::make_unique<MediaDeviceIDSalt>(prefs_.get());
  return media_device_id_salt_->GetSalt();
}

std::unique_ptr<content::ZoomLevelDelegate>
ElectronBrowserContext::CreateZoomLevelDelegate(
    const base::FilePath& partition_path) {
  if (!IsOffTheRecord()) {
    return std::make_unique<ZoomLevelDelegate>(prefs(), partition_path);
  }
  return std::unique_ptr<content::ZoomLevelDelegate>();
}

content::DownloadManagerDelegate*
ElectronBrowserContext::GetDownloadManagerDelegate() {
  if (!download_manager_delegate_) {
    download_manager_delegate_ =
        std::make_unique<ElectronDownloadManagerDelegate>(GetDownloadManager());
  }
  return download_manager_delegate_.get();
}

content::BrowserPluginGuestManager* ElectronBrowserContext::GetGuestManager() {
  if (!guest_manager_)
    guest_manager_ = std::make_unique<WebViewManager>();
  return guest_manager_.get();
}

content::PlatformNotificationService*
ElectronBrowserContext::GetPlatformNotificationService() {
  return ElectronBrowserClient::Get()->GetPlatformNotificationService();
}

content::PermissionControllerDelegate*
ElectronBrowserContext::GetPermissionControllerDelegate() {
  if (!permission_manager_)
    permission_manager_ = std::make_unique<ElectronPermissionManager>();
  return permission_manager_.get();
}

storage::SpecialStoragePolicy*
ElectronBrowserContext::GetSpecialStoragePolicy() {
  return storage_policy_.get();
}

std::string ElectronBrowserContext::GetUserAgent() const {
  return user_agent_.value_or(ElectronBrowserClient::Get()->GetUserAgent());
}

predictors::PreconnectManager* ElectronBrowserContext::GetPreconnectManager() {
  if (!preconnect_manager_) {
    preconnect_manager_ =
        std::make_unique<predictors::PreconnectManager>(nullptr, this);
  }
  return preconnect_manager_.get();
}

scoped_refptr<network::SharedURLLoaderFactory>
ElectronBrowserContext::GetURLLoaderFactory() {
  if (url_loader_factory_)
    return url_loader_factory_;

  network::URLLoaderFactoryBuilder factory_builder;

  // Consult the embedder.
  mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>
      header_client;
  static_cast<content::ContentBrowserClient*>(ElectronBrowserClient::Get())
      ->WillCreateURLLoaderFactory(
          this, nullptr, -1,
          content::ContentBrowserClient::URLLoaderFactoryType::kNavigation,
          url::Origin(), net::IsolationInfo(), std::nullopt,
          ukm::kInvalidSourceIdObj, factory_builder, &header_client, nullptr,
          nullptr, nullptr, nullptr);

  network::mojom::URLLoaderFactoryParamsPtr params =
      network::mojom::URLLoaderFactoryParams::New();
  params->header_client = std::move(header_client);
  params->process_id = network::mojom::kBrowserProcessId;
  params->is_trusted = true;
  params->is_orb_enabled = false;
  // The tests of net module would fail if this setting is true, it seems that
  // the non-NetworkService implementation always has web security enabled.
  params->disable_web_security = false;

  auto* storage_partition = GetDefaultStoragePartition();
  url_loader_factory_ =
      std::move(factory_builder)
          .Finish(storage_partition->GetNetworkContext(), std::move(params));
  return url_loader_factory_;
}

content::PushMessagingService*
ElectronBrowserContext::GetPushMessagingService() {
  return nullptr;
}

content::SSLHostStateDelegate*
ElectronBrowserContext::GetSSLHostStateDelegate() {
  return nullptr;
}

content::BackgroundFetchDelegate*
ElectronBrowserContext::GetBackgroundFetchDelegate() {
  return nullptr;
}

content::BackgroundSyncController*
ElectronBrowserContext::GetBackgroundSyncController() {
  return nullptr;
}

content::BrowsingDataRemoverDelegate*
ElectronBrowserContext::GetBrowsingDataRemoverDelegate() {
  return nullptr;
}

content::ClientHintsControllerDelegate*
ElectronBrowserContext::GetClientHintsControllerDelegate() {
  return nullptr;
}

content::StorageNotificationService*
ElectronBrowserContext::GetStorageNotificationService() {
  return nullptr;
}

content::ReduceAcceptLanguageControllerDelegate*
ElectronBrowserContext::GetReduceAcceptLanguageControllerDelegate() {
  // Needs implementation
  // Refs https://chromium-review.googlesource.com/c/chromium/src/+/3687391
  return nullptr;
}

content::FileSystemAccessPermissionContext*
ElectronBrowserContext::GetFileSystemAccessPermissionContext() {
  return FileSystemAccessPermissionContextFactory::GetForBrowserContext(this);
}

ResolveProxyHelper* ElectronBrowserContext::GetResolveProxyHelper() {
  if (!resolve_proxy_helper_) {
    resolve_proxy_helper_ = base::MakeRefCounted<ResolveProxyHelper>(this);
  }
  return resolve_proxy_helper_.get();
}

network::mojom::SSLConfigPtr ElectronBrowserContext::GetSSLConfig() {
  return ssl_config_.Clone();
}

void ElectronBrowserContext::SetSSLConfig(network::mojom::SSLConfigPtr config) {
  ssl_config_ = std::move(config);
  if (ssl_config_client_) {
    ssl_config_client_->OnSSLConfigUpdated(ssl_config_.Clone());
  }
}

void ElectronBrowserContext::SetSSLConfigClient(
    mojo::Remote<network::mojom::SSLConfigClient> client) {
  ssl_config_client_ = std::move(client);
}

void ElectronBrowserContext::SetDisplayMediaRequestHandler(
    DisplayMediaRequestHandler handler) {
  display_media_request_handler_ = handler;
}

void ElectronBrowserContext::DisplayMediaDeviceChosen(
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback,
    gin::Arguments* args) {
  blink::mojom::StreamDevicesSetPtr stream_devices_set =
      blink::mojom::StreamDevicesSet::New();
  v8::Local<v8::Value> result;
  if (!args->GetNext(&result) || result->IsNullOrUndefined()) {
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::CAPTURE_FAILURE, nullptr);
    return;
  }
  gin_helper::Dictionary result_dict;
  if (!gin::ConvertFromV8(args->isolate(), result, &result_dict)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowTypeError(
            "Display Media Request streams callback must be called with null "
            "or a valid object");
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::CAPTURE_FAILURE, nullptr);
    return;
  }
  stream_devices_set->stream_devices.emplace_back(
      blink::mojom::StreamDevices::New());
  blink::mojom::StreamDevices& devices = *stream_devices_set->stream_devices[0];
  bool video_requested =
      request.video_type != blink::mojom::MediaStreamType::NO_SERVICE;
  bool audio_requested =
      request.audio_type != blink::mojom::MediaStreamType::NO_SERVICE;
  bool has_video = false;
  if (video_requested && result_dict.Has("video")) {
    gin_helper::Dictionary video_dict;
    std::string id;
    std::string name;
    content::RenderFrameHost* rfh;
    if (result_dict.Get("video", &video_dict) && video_dict.Get("id", &id) &&
        video_dict.Get("name", &name)) {
      blink::MediaStreamDevice video_device(request.video_type, id, name);
      video_device.display_media_info = DesktopMediaIDToDisplayMediaInformation(
          nullptr, url::Origin::Create(request.security_origin),
          content::DesktopMediaID::Parse(video_device.id));
      devices.video_device = video_device;
    } else if (result_dict.Get("video", &rfh)) {
      auto* web_contents = content::WebContents::FromRenderFrameHost(rfh);
      blink::MediaStreamDevice video_device(
          request.video_type,
          content::WebContentsMediaCaptureId(rfh->GetProcess()->GetID(),
                                             rfh->GetRoutingID())
              .ToString(),
          base::UTF16ToUTF8(web_contents->GetTitle()));
      video_device.display_media_info = DesktopMediaIDToDisplayMediaInformation(
          web_contents, url::Origin::Create(request.security_origin),
          content::DesktopMediaID::Parse(video_device.id));
      devices.video_device = video_device;
    } else {
      gin_helper::ErrorThrower(args->isolate())
          .ThrowTypeError(
              "video must be a WebFrameMain or DesktopCapturerSource");
      std::move(callback).Run(
          blink::mojom::StreamDevicesSet(),
          blink::mojom::MediaStreamRequestResult::CAPTURE_FAILURE, nullptr);
      return;
    }
    has_video = true;
  }
  if (audio_requested && result_dict.Has("audio")) {
    gin_helper::Dictionary audio_dict;
    std::string id;
    std::string name;
    content::RenderFrameHost* rfh;
    // NB. this is not permitted by the documentation, but is left here as an
    // "escape hatch" for providing an arbitrary name/id if needed in the
    // future.
    if (result_dict.Get("audio", &audio_dict) && audio_dict.Get("id", &id) &&
        audio_dict.Get("name", &name)) {
      blink::MediaStreamDevice audio_device(request.audio_type, id, name);
      audio_device.display_media_info = DesktopMediaIDToDisplayMediaInformation(
          nullptr, url::Origin::Create(request.security_origin),
          GetAudioDesktopMediaId(request.requested_audio_device_ids));
      devices.audio_device = audio_device;
    } else if (result_dict.Get("audio", &rfh)) {
      bool enable_local_echo = false;
      result_dict.Get("enableLocalEcho", &enable_local_echo);
      bool disable_local_echo = !enable_local_echo;
      auto* web_contents = content::WebContents::FromRenderFrameHost(rfh);
      blink::MediaStreamDevice audio_device(
          request.audio_type,
          content::WebContentsMediaCaptureId(rfh->GetProcess()->GetID(),
                                             rfh->GetRoutingID(),
                                             disable_local_echo)
              .ToString(),
          "Tab audio");
      audio_device.display_media_info = DesktopMediaIDToDisplayMediaInformation(
          web_contents, url::Origin::Create(request.security_origin),
          GetAudioDesktopMediaId(request.requested_audio_device_ids));
      devices.audio_device = audio_device;
    } else if (result_dict.Get("audio", &id)) {
      blink::MediaStreamDevice audio_device(request.audio_type, id,
                                            "System audio");
      audio_device.display_media_info = DesktopMediaIDToDisplayMediaInformation(
          nullptr, url::Origin::Create(request.security_origin),
          GetAudioDesktopMediaId(request.requested_audio_device_ids));
      devices.audio_device = audio_device;
    } else {
      gin_helper::ErrorThrower(args->isolate())
          .ThrowTypeError(
              "audio must be a WebFrameMain, \"loopback\" or "
              "\"loopbackWithMute\"");
      std::move(callback).Run(
          blink::mojom::StreamDevicesSet(),
          blink::mojom::MediaStreamRequestResult::CAPTURE_FAILURE, nullptr);
      return;
    }
  }

  if ((video_requested && !has_video)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowTypeError(
            "Video was requested, but no video stream was provided");
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::CAPTURE_FAILURE, nullptr);
    return;
  }

  std::move(callback).Run(*stream_devices_set,
                          blink::mojom::MediaStreamRequestResult::OK, nullptr);
}

bool ElectronBrowserContext::ChooseDisplayMediaDevice(
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  if (!display_media_request_handler_)
    return false;
  DisplayMediaResponseCallbackJs callbackJs =
      base::BindOnce(&DisplayMediaDeviceChosen, request, std::move(callback));
  display_media_request_handler_.Run(request, std::move(callbackJs));
  return true;
}

void ElectronBrowserContext::GrantDevicePermission(
    const url::Origin& origin,
    const base::Value& device,
    blink::PermissionType permission_type) {
  granted_devices_[permission_type][origin].push_back(
      std::make_unique<base::Value>(device.Clone()));
}

void ElectronBrowserContext::RevokeDevicePermission(
    const url::Origin& origin,
    const base::Value& device,
    const blink::PermissionType permission_type) {
  const auto& current_devices_it = granted_devices_.find(permission_type);
  if (current_devices_it == granted_devices_.end())
    return;

  const auto& origin_devices_it = current_devices_it->second.find(origin);
  if (origin_devices_it == current_devices_it->second.end())
    return;

  std::erase_if(origin_devices_it->second,
                [&device, &permission_type](auto const& val) {
                  return DoesDeviceMatch(device, *val, permission_type);
                });
}

bool ElectronBrowserContext::CheckDevicePermission(
    const url::Origin& origin,
    const base::Value& device,
    blink::PermissionType permission_type) {
  const auto& current_devices_it = granted_devices_.find(permission_type);
  if (current_devices_it == granted_devices_.end())
    return false;

  const auto& origin_devices_it = current_devices_it->second.find(origin);
  if (origin_devices_it == current_devices_it->second.end())
    return false;

  for (const auto& device_to_compare : origin_devices_it->second) {
    if (DoesDeviceMatch(device, *device_to_compare, permission_type))
      return true;
  }

  return false;
}

// static
ElectronBrowserContext* ElectronBrowserContext::From(
    const std::string& partition,
    bool in_memory,
    base::Value::Dict options) {
  PartitionKey key(partition, in_memory);
  ElectronBrowserContext* browser_context = browser_context_map()[key].get();
  if (browser_context) {
    return browser_context;
  }

  auto* new_context = new ElectronBrowserContext(std::cref(partition),
                                                 in_memory, std::move(options));
  browser_context_map()[key] =
      std::unique_ptr<ElectronBrowserContext>(new_context);
  return new_context;
}

ElectronBrowserContext* ElectronBrowserContext::FromPath(
    const base::FilePath& path,
    base::Value::Dict options) {
  PartitionKey key(path);

  ElectronBrowserContext* browser_context = browser_context_map()[key].get();
  if (browser_context) {
    return browser_context;
  }

  auto* new_context =
      new ElectronBrowserContext(std::cref(path), false, std::move(options));
  browser_context_map()[key] =
      std::unique_ptr<ElectronBrowserContext>(new_context);
  return new_context;
}

}  // namespace electron
