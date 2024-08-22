// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_browser_main_parts.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/i18n/rtl.h"
#include "base/metrics/field_trial.h"
#include "base/nix/xdg_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/ui/color/chrome_color_mixers.h"
#include "chrome/common/chrome_switches.h"
#include "components/os_crypt/sync/key_storage_config_linux.h"
#include "components/os_crypt/sync/key_storage_util_linux.h"
#include "components/os_crypt/sync/os_crypt.h"
#include "components/password_manager/core/browser/password_manager_switches.h"  // nogncheck
#include "content/browser/browser_main_loop.h"  // nogncheck
#include "content/public/browser/browser_child_process_host_delegate.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/first_party_sets_handler.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/process_type.h"
#include "content/public/common/result_codes.h"
#include "electron/buildflags/buildflags.h"
#include "media/base/localized_strings.h"
#include "services/network/public/cpp/features.h"
#include "services/tracing/public/cpp/stack_sampling/tracing_sampler_profiler.h"
#include "shell/app/electron_main_delegate.h"
#include "shell/browser/api/electron_api_utility_process.h"
#include "shell/browser/browser.h"
#include "shell/browser/browser_process_impl.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/electron_web_ui_controller_factory.h"
#include "shell/browser/feature_list.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/media/media_capture_devices_dispatcher.h"
#include "shell/browser/ui/devtools_manager_delegate.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/application_info.h"
#include "shell/common/electron_paths.h"
#include "shell/common/gin_helper/trackable_object.h"
#include "shell/common/logging.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"
#include "ui/base/idle/idle.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_switches.h"
#include "ui/color/color_provider_manager.h"
#include "ui/display/screen.h"
#include "url/url_util.h"

#if defined(USE_AURA)
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/wm/core/wm_state.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "base/environment.h"
#include "chrome/browser/ui/views/dark_mode_manager_linux.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"
#include "device/bluetooth/dbus/dbus_bluez_manager_wrapper_linux.h"
#include "electron/electron_gtk_stubs.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/base/ime/linux/linux_input_method_context_factory.h"
#include "ui/gtk/gtk_compat.h"  // nogncheck
#include "ui/gtk/gtk_util.h"    // nogncheck
#include "ui/linux/linux_ui.h"
#include "ui/linux/linux_ui_factory.h"
#include "ui/linux/linux_ui_getter.h"
#include "ui/ozone/public/ozone_platform.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/win/chrome_select_file_dialog_factory.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/gfx/system_fonts_win.h"
#include "ui/strings/grit/app_locale_settings.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "components/os_crypt/sync/keychain_password_mac.h"
#include "shell/browser/ui/cocoa/views_delegate_mac.h"
#else
#include "shell/browser/ui/views/electron_views_delegate.h"
#endif

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "extensions/browser/browser_context_keyed_service_factories.h"
#include "extensions/common/extension_api.h"
#include "shell/browser/extensions/electron_browser_context_keyed_service_factories.h"
#include "shell/browser/extensions/electron_extensions_browser_client.h"
#include "shell/common/extensions/electron_extensions_client.h"
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
#include "chrome/browser/spellchecker/spellcheck_factory.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
#include "content/public/browser/plugin_service.h"
#include "shell/common/plugin_info.h"
#endif  // BUILDFLAG(ENABLE_PLUGINS)

namespace electron {

namespace {

#if BUILDFLAG(IS_LINUX)
class LinuxUiGetterImpl : public ui::LinuxUiGetter {
 public:
  LinuxUiGetterImpl() = default;
  ~LinuxUiGetterImpl() override = default;

  // ui::LinuxUiGetter
  ui::LinuxUiTheme* GetForWindow(aura::Window* window) override {
    return GetForProfile(nullptr);
  }
  ui::LinuxUiTheme* GetForProfile(Profile* profile) override {
    return ui::GetLinuxUiTheme(ui::SystemTheme::kGtk);
  }
};
#endif

#if BUILDFLAG(IS_WIN)
int GetMinimumFontSize() {
  int min_font_size;
  base::StringToInt(l10n_util::GetStringUTF16(IDS_MINIMUM_UI_FONT_SIZE),
                    &min_font_size);
  return min_font_size;
}
#endif

std::u16string MediaStringProvider(media::MessageId id) {
  switch (id) {
    case media::DEFAULT_AUDIO_DEVICE_NAME:
      return u"Default";
#if BUILDFLAG(IS_WIN)
    case media::COMMUNICATIONS_AUDIO_DEVICE_NAME:
      return u"Communications";
#endif
    default:
      return std::u16string();
  }
}

}  // namespace

// static
ElectronBrowserMainParts* ElectronBrowserMainParts::self_ = nullptr;

ElectronBrowserMainParts::ElectronBrowserMainParts()
    : fake_browser_process_(std::make_unique<BrowserProcessImpl>()),
      node_bindings_{
          NodeBindings::Create(NodeBindings::BrowserEnvironment::kBrowser)},
      electron_bindings_{
          std::make_unique<ElectronBindings>(node_bindings_->uv_loop())},
      browser_{std::make_unique<Browser>()} {
  DCHECK(!self_) << "Cannot have two ElectronBrowserMainParts";
  self_ = this;
}

ElectronBrowserMainParts::~ElectronBrowserMainParts() = default;

// static
ElectronBrowserMainParts* ElectronBrowserMainParts::Get() {
  DCHECK(self_);
  return self_;
}

bool ElectronBrowserMainParts::SetExitCode(int code) {
  if (!exit_code_)
    return false;

  content::BrowserMainLoop::GetInstance()->SetResultCode(code);
  *exit_code_ = code;
  return true;
}

int ElectronBrowserMainParts::GetExitCode() const {
  return exit_code_.value_or(content::RESULT_CODE_NORMAL_EXIT);
}

int ElectronBrowserMainParts::PreEarlyInitialization() {
  field_trial_list_ = std::make_unique<base::FieldTrialList>();
#if BUILDFLAG(IS_POSIX)
  HandleSIGCHLD();
#endif
#if BUILDFLAG(IS_LINUX)
  DetectOzonePlatform();
  ui::OzonePlatform::PreEarlyInitialization();
#endif
#if BUILDFLAG(IS_MAC)
  screen_ = std::make_unique<display::ScopedNativeScreen>();
#endif

  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(AddChromeColorMixers));

  return GetExitCode();
}

void ElectronBrowserMainParts::PostEarlyInitialization() {
  // A workaround was previously needed because there was no ThreadTaskRunner
  // set.  If this check is failing we may need to re-add that workaround
  DCHECK(base::SingleThreadTaskRunner::HasCurrentDefault());

  // The ProxyResolverV8 has setup a complete V8 environment, in order to
  // avoid conflicts we only initialize our V8 environment after that.
  js_env_ = std::make_unique<JavascriptEnvironment>(node_bindings_->uv_loop());

  v8::HandleScope scope(js_env_->isolate());

  node_bindings_->Initialize(js_env_->isolate()->GetCurrentContext());
  // Create the global environment.
  node_env_ = node_bindings_->CreateEnvironment(
      js_env_->isolate()->GetCurrentContext(), js_env_->platform());

  node_env_->set_trace_sync_io(node_env_->options()->trace_sync_io);

  // We do not want to crash the main process on unhandled rejections.
  node_env_->options()->unhandled_rejections = "warn-with-error-code";

  // Add Electron extended APIs.
  electron_bindings_->BindTo(js_env_->isolate(), node_env_->process_object());

  // Create explicit microtasks runner.
  js_env_->CreateMicrotasksRunner();

  // Wrap the uv loop with global env.
  node_bindings_->set_uv_env(node_env_.get());

  // Load everything.
  node_bindings_->LoadEnvironment(node_env_.get());

  // Wait for app
  node_bindings_->JoinAppCode();

  // We already initialized the feature list in PreEarlyInitialization(), but
  // the user JS script would not have had a chance to alter the command-line
  // switches at that point. Lets reinitialize it here to pick up the
  // command-line changes.
  base::FeatureList::ClearInstanceForTesting();
  InitializeFeatureList();

  // Initialize field trials.
  InitializeFieldTrials();

  // Reinitialize logging now that the app has had a chance to set the app name
  // and/or user data directory.
  logging::InitElectronLogging(*base::CommandLine::ForCurrentProcess(),
                               /* is_preinit = */ false);

  // Initialize after user script environment creation.
  fake_browser_process_->PostEarlyInitialization();
}

int ElectronBrowserMainParts::PreCreateThreads() {
  if (!views::LayoutProvider::Get()) {
    layout_provider_ = std::make_unique<views::LayoutProvider>();
  }

  // Fetch the system locale for Electron.
#if BUILDFLAG(IS_MAC)
  fake_browser_process_->SetSystemLocale(GetCurrentSystemLocale());
#else
  fake_browser_process_->SetSystemLocale(base::i18n::GetConfiguredLocale());
#endif

  auto* command_line = base::CommandLine::ForCurrentProcess();
  std::string locale = command_line->GetSwitchValueASCII(::switches::kLang);

#if BUILDFLAG(IS_MAC)
  // The browser process only wants to support the language Cocoa will use,
  // so force the app locale to be overridden with that value. This must
  // happen before the ResourceBundle is loaded
  if (locale.empty())
    l10n_util::OverrideLocaleWithCocoaLocale();
#elif BUILDFLAG(IS_LINUX)
  // l10n_util::GetApplicationLocaleInternal uses g_get_language_names(),
  // which keys off of getenv("LC_ALL").
  // We must set this env first to make ui::ResourceBundle accept the custom
  // locale.
  auto env = base::Environment::Create();
  std::optional<std::string> lc_all;
  if (!locale.empty()) {
    std::string str;
    if (env->GetVar("LC_ALL", &str))
      lc_all.emplace(std::move(str));
    env->SetVar("LC_ALL", locale);
  }
#endif

  // Load resources bundle according to locale.
  std::string loaded_locale = LoadResourceBundle(locale);

#if defined(USE_AURA)
  // NB: must be called _after_ locale resource bundle is loaded,
  // because ui lib makes use of it in X11
  if (!display::Screen::GetScreen()) {
    screen_ = views::CreateDesktopScreen();
  }
#endif

  // Initialize the app locale for Electron and Chromium.
  std::string app_locale = l10n_util::GetApplicationLocale(loaded_locale);
  ElectronBrowserClient::SetApplicationLocale(app_locale);
  fake_browser_process_->SetApplicationLocale(app_locale);

#if BUILDFLAG(IS_LINUX)
  // Reset to the original LC_ALL since we should not be changing it.
  if (!locale.empty()) {
    if (lc_all)
      env->SetVar("LC_ALL", *lc_all);
    else
      env->UnSetVar("LC_ALL");
  }
#endif

  // Force MediaCaptureDevicesDispatcher to be created on UI thread.
  MediaCaptureDevicesDispatcher::GetInstance();

  // Force MediaCaptureDevicesDispatcher to be created on UI thread.
  MediaCaptureDevicesDispatcher::GetInstance();

#if BUILDFLAG(IS_MAC)
  ui::InitIdleMonitor();
  Browser::Get()->ApplyForcedRTL();
#endif

  fake_browser_process_->PreCreateThreads();

  // Notify observers.
  Browser::Get()->PreCreateThreads();

  return 0;
}

void ElectronBrowserMainParts::PostCreateThreads() {
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&tracing::TracingSamplerProfiler::CreateOnChildThread));
#if BUILDFLAG(ENABLE_PLUGINS)
  // PluginService can only be used on the UI thread
  // and ContentClient::AddPlugins gets called for both browser and render
  // process where the latter will not have UI thread which leads to DCHECK.
  // Separate the WebPluginInfo registration for these processes.
  std::vector<content::WebPluginInfo> plugins;
  auto* plugin_service = content::PluginService::GetInstance();
  plugin_service->RefreshPlugins();
  GetInternalPlugins(&plugins);
  for (const auto& plugin : plugins)
    plugin_service->RegisterInternalPlugin(plugin, true);
#endif
}

void ElectronBrowserMainParts::PostDestroyThreads() {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_browser_client_.reset();
  extensions::ExtensionsBrowserClient::Set(nullptr);
#endif
#if BUILDFLAG(IS_LINUX)
  device::BluetoothAdapterFactory::Shutdown();
  bluez::DBusBluezManagerWrapperLinux::Shutdown();
#endif
  fake_browser_process_->PostDestroyThreads();
}

void ElectronBrowserMainParts::ToolkitInitialized() {
#if BUILDFLAG(IS_LINUX)
  auto* linux_ui = ui::GetDefaultLinuxUi();
  CHECK(linux_ui);
  linux_ui_getter_ = std::make_unique<LinuxUiGetterImpl>();

  electron::InitializeElectron_gdk_pixbuf(gtk::GetLibGdkPixbuf());
  CHECK(electron::IsElectron_gdk_pixbufInitialized())
      << "Failed to initialize libgdk_pixbuf-2.0.so.0";

  // source theme changes from system settings, including settings portal:
  // https://flatpak.github.io/xdg-desktop-portal/#gdbus-org.freedesktop.portal.Settings
  dark_mode_manager_ = std::make_unique<ui::DarkModeManagerLinux>();

  ui::LinuxUi::SetInstance(linux_ui);

  // Cursor theme changes are tracked by LinuxUI (via a CursorThemeManager
  // implementation). Start observing them once it's initialized.
  ui::CursorFactory::GetInstance()->ObserveThemeChanges();
#endif

#if defined(USE_AURA)
  wm_state_ = std::make_unique<wm::WMState>();
#endif

#if BUILDFLAG(IS_WIN)
  gfx::win::SetAdjustFontCallback(&l10n_util::AdjustUiFont);
  gfx::win::SetGetMinimumFontSizeCallback(&GetMinimumFontSize);
#endif

#if BUILDFLAG(IS_MAC)
  views_delegate_ = std::make_unique<ViewsDelegateMac>();
#else
  views_delegate_ = std::make_unique<ViewsDelegate>();
#endif
}

int ElectronBrowserMainParts::PreMainMessageLoopRun() {
  // Run user's main script before most things get initialized, so we can have
  // a chance to setup everything.
  node_bindings_->PrepareEmbedThread();
  node_bindings_->StartPolling();

  // url::Add*Scheme are not threadsafe, this helps prevent data races.
  url::LockSchemeRegistries();

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions_client_ = std::make_unique<ElectronExtensionsClient>();
  extensions::ExtensionsClient::Set(extensions_client_.get());

  // BrowserContextKeyedAPIServiceFactories require an ExtensionsBrowserClient.
  extensions_browser_client_ =
      std::make_unique<ElectronExtensionsBrowserClient>();
  extensions::ExtensionsBrowserClient::Set(extensions_browser_client_.get());

  extensions::EnsureBrowserContextKeyedServiceFactoriesBuilt();
  extensions::electron::EnsureBrowserContextKeyedServiceFactoriesBuilt();
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  SpellcheckServiceFactory::GetInstance();
#endif

  content::WebUIControllerFactory::RegisterFactory(
      ElectronWebUIControllerFactory::GetInstance());

  auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kRemoteDebuggingPipe)) {
    // --remote-debugging-pipe
    auto on_disconnect = base::BindOnce([]() {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE, base::BindOnce([]() { Browser::Get()->Quit(); }));
    });
    content::DevToolsAgentHost::StartRemoteDebuggingPipeHandler(
        std::move(on_disconnect));
  } else if (command_line->HasSwitch(switches::kRemoteDebuggingPort)) {
    // --remote-debugging-port
    DevToolsManagerDelegate::StartHttpHandler();
  }

#if !BUILDFLAG(IS_MAC)
  // The corresponding call in macOS is in ElectronApplicationDelegate.
  Browser::Get()->WillFinishLaunching();
  Browser::Get()->DidFinishLaunching(base::Value::Dict());
#endif

  // Notify observers that main thread message loop was initialized.
  Browser::Get()->PreMainMessageLoopRun();

  fake_browser_process_->PreMainMessageLoopRun();

#if BUILDFLAG(IS_WIN)
  ui::SelectFileDialog::SetFactory(
      std::make_unique<ChromeSelectFileDialogFactory>());
#endif

  return GetExitCode();
}

void ElectronBrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  exit_code_ = content::RESULT_CODE_NORMAL_EXIT;
  Browser::Get()->SetMainMessageLoopQuitClosure(
      run_loop->QuitWhenIdleClosure());
}

void ElectronBrowserMainParts::PostCreateMainMessageLoop() {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
  std::string app_name = electron::Browser::Get()->GetName();
#endif
#if BUILDFLAG(IS_LINUX)
  auto shutdown_cb =
      base::BindOnce([] { LOG(FATAL) << "Failed to shutdown."; });
  ui::OzonePlatform::GetInstance()->PostCreateMainMessageLoop(
      std::move(shutdown_cb),
      content::GetUIThreadTaskRunner({content::BrowserTaskType::kUserInput}));

  if (!bluez::BluezDBusManager::IsInitialized())
    bluez::DBusBluezManagerWrapperLinux::Initialize();

  // Set up crypt config. This needs to be done before anything starts the
  // network service, as the raw encryption key needs to be shared with the
  // network service for encrypted cookie storage.
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  std::unique_ptr<os_crypt::Config> config =
      std::make_unique<os_crypt::Config>();
  // Forward to os_crypt the flag to use a specific password store.
  config->store =
      command_line.GetSwitchValueASCII(password_manager::kPasswordStore);
  config->product_name = app_name;
  config->application_name = app_name;
  // c.f.
  // https://source.chromium.org/chromium/chromium/src/+/main:chrome/common/chrome_switches.cc;l=689;drc=9d82515060b9b75fa941986f5db7390299669ef1
  config->should_use_preference =
      command_line.HasSwitch(password_manager::kEnableEncryptionSelection);
  base::PathService::Get(DIR_SESSION_DATA, &config->user_data_path);

  bool use_backend = !config->should_use_preference ||
                     os_crypt::GetBackendUse(config->user_data_path);
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  base::nix::DesktopEnvironment desktop_env =
      base::nix::GetDesktopEnvironment(env.get());
  os_crypt::SelectedLinuxBackend selected_backend =
      os_crypt::SelectBackend(config->store, use_backend, desktop_env);
  fake_browser_process_->SetLinuxStorageBackend(selected_backend);
  OSCrypt::SetConfig(std::move(config));
#endif
#if BUILDFLAG(IS_MAC)
  KeychainPassword::GetServiceName() = app_name + " Safe Storage";
  KeychainPassword::GetAccountName() = app_name;
#endif
#if BUILDFLAG(IS_POSIX)
  // Exit in response to SIGINT, SIGTERM, etc.
  InstallShutdownSignalHandlers(
      base::BindOnce(&Browser::Quit, base::Unretained(Browser::Get())),
      content::GetUIThreadTaskRunner({}));
#endif
}

void ElectronBrowserMainParts::PostMainMessageLoopRun() {
#if BUILDFLAG(IS_MAC)
  FreeAppDelegate();
#endif

  // Shutdown the DownloadManager before destroying Node to prevent
  // DownloadItem callbacks from crashing.
  for (auto& iter : ElectronBrowserContext::browser_context_map()) {
    auto* download_manager = iter.second.get()->GetDownloadManager();
    if (download_manager) {
      download_manager->Shutdown();
    }
  }

  // Shutdown utility process created with Electron API before
  // stopping Node.js so that exit events can be emitted. We don't let
  // content layer perform this action since it destroys
  // child process only after this step (PostMainMessageLoopRun) via
  // BrowserProcessIOThread::ProcessHostCleanUp() which is too late for our
  // use case.
  // https://source.chromium.org/chromium/chromium/src/+/main:content/browser/browser_main_loop.cc;l=1086-1108
  //
  // The following logic is based on
  // https://source.chromium.org/chromium/chromium/src/+/main:content/browser/browser_process_io_thread.cc;l=127-159
  //
  // Although content::BrowserChildProcessHostIterator is only to be called from
  // IO thread, it is safe to call from PostMainMessageLoopRun because thread
  // restrictions have been lifted.
  // https://source.chromium.org/chromium/chromium/src/+/main:content/browser/browser_main_loop.cc;l=1062-1078
  for (content::BrowserChildProcessHostIterator it(
           content::PROCESS_TYPE_UTILITY);
       !it.Done(); ++it) {
    if (it.GetDelegate()->GetServiceName() == node::mojom::NodeService::Name_) {
      auto& process = it.GetData().GetProcess();
      if (!process.IsValid())
        continue;
      auto utility_process_wrapper =
          api::UtilityProcessWrapper::FromProcessId(process.Pid());
      if (utility_process_wrapper)
        utility_process_wrapper->Shutdown(0 /* exit_code */);
    }
  }

  // Destroy node platform after all destructors_ are executed, as they may
  // invoke Node/V8 APIs inside them.
  node_env_->set_trace_sync_io(false);
  js_env_->DestroyMicrotasksRunner();
  node::Stop(node_env_.get(), node::StopFlags::kDoNotTerminateIsolate);
  node_bindings_->set_uv_env(nullptr);
  node_env_.reset();

  auto default_context_key = ElectronBrowserContext::PartitionKey("", false);
  std::unique_ptr<ElectronBrowserContext> default_context = std::move(
      ElectronBrowserContext::browser_context_map()[default_context_key]);
  ElectronBrowserContext::browser_context_map().clear();
  default_context.reset();

  fake_browser_process_->PostMainMessageLoopRun();
  content::DevToolsAgentHost::StopRemoteDebuggingPipeHandler();

#if BUILDFLAG(IS_LINUX)
  ui::OzonePlatform::GetInstance()->PostMainMessageLoopRun();
#endif
}

#if !BUILDFLAG(IS_MAC)
void ElectronBrowserMainParts::PreCreateMainMessageLoop() {
  PreCreateMainMessageLoopCommon();
}
#endif

void ElectronBrowserMainParts::PreCreateMainMessageLoopCommon() {
#if BUILDFLAG(IS_MAC)
  InitializeMainNib();
  RegisterURLHandler();
#endif
  media::SetLocalizedStringProvider(MediaStringProvider);

#if BUILDFLAG(IS_WIN)
  auto* local_state = g_browser_process->local_state();
  DCHECK(local_state);

  bool os_crypt_init = OSCrypt::Init(local_state);
  DCHECK(os_crypt_init);
#endif
}

device::mojom::GeolocationControl*
ElectronBrowserMainParts::GetGeolocationControl() {
  if (!geolocation_control_) {
    content::GetDeviceService().BindGeolocationControl(
        geolocation_control_.BindNewPipeAndPassReceiver());
  }
  return geolocation_control_.get();
}

IconManager* ElectronBrowserMainParts::GetIconManager() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!icon_manager_.get())
    icon_manager_ = std::make_unique<IconManager>();
  return icon_manager_.get();
}

}  // namespace electron
