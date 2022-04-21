// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_browser_main_parts.h"

#include <memory>
#include <string>
#include <utility>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "components/os_crypt/key_storage_config_linux.h"
#include "components/os_crypt/os_crypt.h"
#include "content/browser/browser_main_loop.h"  // nogncheck
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/result_codes.h"
#include "electron/buildflags/buildflags.h"
#include "electron/fuses.h"
#include "media/base/localized_strings.h"
#include "services/network/public/cpp/features.h"
#include "services/tracing/public/cpp/stack_sampling/tracing_sampler_profiler.h"
#include "shell/app/electron_main_delegate.h"
#include "shell/browser/api/electron_api_app.h"
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
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/idle/idle.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_switches.h"

#if defined(USE_AURA)
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/wm/core/wm_state.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "base/environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/dbus/dbus_bluez_manager_wrapper_linux.h"
#include "electron/electron_gtk_stubs.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/base/ime/linux/linux_input_method_context_factory.h"
#include "ui/gfx/color_utils.h"
#include "ui/gtk/gtk_compat.h"      // nogncheck
#include "ui/gtk/gtk_ui_factory.h"  // nogncheck
#include "ui/gtk/gtk_util.h"        // nogncheck
#include "ui/ozone/public/ozone_platform.h"
#include "ui/views/linux_ui/linux_ui.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/display/win/dpi.h"
#include "ui/gfx/system_fonts_win.h"
#include "ui/strings/grit/app_locale_settings.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "services/device/public/cpp/geolocation/geolocation_manager.h"
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

namespace electron {

namespace {

template <typename T>
void Erase(T* container, typename T::iterator iter) {
  container->erase(iter);
}

#if BUILDFLAG(IS_WIN)
// gfx::Font callbacks
void AdjustUIFont(gfx::win::FontAdjustment* font_adjustment) {
  l10n_util::NeedOverrideDefaultUIFont(&font_adjustment->font_family_override,
                                       &font_adjustment->font_scale);
  font_adjustment->font_scale *= display::win::GetAccessibilityFontScale();
}

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

#if BUILDFLAG(IS_LINUX)
// GTK does not provide a way to check if current theme is dark, so we compare
// the text and background luminosity to get a result.
// This trick comes from FireFox.
void UpdateDarkThemeSetting() {
  float bg = color_utils::GetRelativeLuminance(gtk::GetBgColor("GtkLabel"));
  float fg = color_utils::GetRelativeLuminance(gtk::GetFgColor("GtkLabel"));
  bool is_dark = fg > bg;
  // Pass it to NativeUi theme, which is used by the nativeTheme module and most
  // places in Electron.
  ui::NativeTheme::GetInstanceForNativeUi()->set_use_dark_colors(is_dark);
  // Pass it to Web Theme, to make "prefers-color-scheme" media query work.
  ui::NativeTheme::GetInstanceForWeb()->set_use_dark_colors(is_dark);
}
#endif

}  // namespace

#if BUILDFLAG(IS_LINUX)
class DarkThemeObserver : public ui::NativeThemeObserver {
 public:
  DarkThemeObserver() = default;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override {
    UpdateDarkThemeSetting();
  }
};
#endif

// static
ElectronBrowserMainParts* ElectronBrowserMainParts::self_ = nullptr;

ElectronBrowserMainParts::ElectronBrowserMainParts(
    const content::MainFunctionParams& params)
    : fake_browser_process_(std::make_unique<BrowserProcessImpl>()),
      browser_(std::make_unique<Browser>()),
      node_bindings_(
          NodeBindings::Create(NodeBindings::BrowserEnvironment::kBrowser)),
      electron_bindings_(
          std::make_unique<ElectronBindings>(node_bindings_->uv_loop())) {
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
  field_trial_list_ = std::make_unique<base::FieldTrialList>(nullptr);
#if BUILDFLAG(IS_POSIX)
  HandleSIGCHLD();
#endif
#if BUILDFLAG(IS_LINUX)
  ui::OzonePlatform::PreEarlyInitialization();
#endif

  return GetExitCode();
}

void ElectronBrowserMainParts::PostEarlyInitialization() {
  // A workaround was previously needed because there was no ThreadTaskRunner
  // set.  If this check is failing we may need to re-add that workaround
  DCHECK(base::ThreadTaskRunnerHandle::IsSet());

  // The ProxyResolverV8 has setup a complete V8 environment, in order to
  // avoid conflicts we only initialize our V8 environment after that.
  js_env_ = std::make_unique<JavascriptEnvironment>(node_bindings_->uv_loop());

  v8::HandleScope scope(js_env_->isolate());

  node_bindings_->Initialize();
  // Create the global environment.
  node::Environment* env = node_bindings_->CreateEnvironment(
      js_env_->context(), js_env_->platform());
  node_env_ = std::make_unique<NodeEnvironment>(env);

  env->set_trace_sync_io(env->options()->trace_sync_io);

  // We do not want to crash the main process on unhandled rejections.
  env->options()->unhandled_rejections = "warn";

  // Add Electron extended APIs.
  electron_bindings_->BindTo(js_env_->isolate(), env->process_object());

  // Load everything.
  node_bindings_->LoadEnvironment(env);

  // Wrap the uv loop with global env.
  node_bindings_->set_uv_env(env);

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
#if defined(USE_AURA)
  screen_ = views::CreateDesktopScreen();
  display::Screen::SetScreenInstance(screen_.get());
#if BUILDFLAG(IS_LINUX)
  views::LinuxUI::instance()->UpdateDeviceScaleFactor();
#endif
#endif

  if (!views::LayoutProvider::Get())
    layout_provider_ = std::make_unique<views::LayoutProvider>();

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
  absl::optional<std::string> lc_all;
  if (!locale.empty()) {
    std::string str;
    if (env->GetVar("LC_ALL", &str))
      lc_all.emplace(std::move(str));
    env->SetVar("LC_ALL", locale.c_str());
  }
#endif

  // Load resources bundle according to locale.
  std::string loaded_locale = LoadResourceBundle(locale);

  // Initialize the app locale.
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
#endif

  fake_browser_process_->PreCreateThreads();

  // Notify observers.
  Browser::Get()->PreCreateThreads();

  return 0;
}

void ElectronBrowserMainParts::PostCreateThreads() {
  base::PostTask(
      FROM_HERE, {content::BrowserThread::IO},
      base::BindOnce(&tracing::TracingSamplerProfiler::CreateOnChildThread));
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

#if BUILDFLAG(IS_LINUX)
// static
absl::optional<std::string>& ElectronBrowserMainParts::GetGDKBackend() {
  static absl::optional<std::string> gdk_backend;
  return gdk_backend;
}
#endif

void ElectronBrowserMainParts::ToolkitInitialized() {
#if BUILDFLAG(IS_LINUX)
  // This is set by Chromium here:
  // https://chromium-review.googlesource.com/c/chromium/src/+/2586184
  // and can detrimentally affect external app behaviors, so we want to
  // check if the user has set it so we can use it later.
  std::string backend;
  if (base::Environment::Create()->GetVar("GDK_BACKEND", &backend))
    GetGDKBackend() = backend;

  auto linux_ui = BuildGtkUi();
  linux_ui->Initialize();
  DCHECK(ui::LinuxInputMethodContextFactory::instance());

  // Try loading gtk symbols used by Electron.
  electron::InitializeElectron_gtk(gtk::GetLibGtk());
  if (!electron::IsElectron_gtkInitialized()) {
    electron::UninitializeElectron_gtk();
  }

  // Chromium does not respect GTK dark theme setting, but they may change
  // in future and this code might be no longer needed. Check the Chromium
  // issue to keep updated:
  // https://bugs.chromium.org/p/chromium/issues/detail?id=998903
  UpdateDarkThemeSetting();
  // Update the native theme when GTK theme changes. The GetNativeTheme
  // here returns a NativeThemeGtk, which monitors GTK settings.
  dark_theme_observer_ = std::make_unique<DarkThemeObserver>();
  linux_ui->GetNativeTheme(nullptr)->AddObserver(dark_theme_observer_.get());
  views::LinuxUI::SetInstance(std::move(linux_ui));

  // Cursor theme changes are tracked by LinuxUI (via a CursorThemeManager
  // implementation). Start observing them once it's initialized.
  ui::CursorFactory::GetInstance()->ObserveThemeChanges();
#endif

#if defined(USE_AURA)
  wm_state_ = std::make_unique<wm::WMState>();
#endif

#if BUILDFLAG(IS_WIN)
  gfx::win::SetAdjustFontCallback(&AdjustUIFont);
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
      base::PostTask(FROM_HERE, {content::BrowserThread::UI},
                     base::BindOnce([]() { Browser::Get()->Quit(); }));
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
  Browser::Get()->DidFinishLaunching(base::DictionaryValue());
#endif

  // Notify observers that main thread message loop was initialized.
  Browser::Get()->PreMainMessageLoopRun();

  return GetExitCode();
}

void ElectronBrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  js_env_->OnMessageLoopCreated();
  exit_code_ = content::RESULT_CODE_NORMAL_EXIT;
  Browser::Get()->SetMainMessageLoopQuitClosure(
      run_loop->QuitWhenIdleClosure());
}

void ElectronBrowserMainParts::PostCreateMainMessageLoop() {
#if BUILDFLAG(IS_LINUX)
  auto shutdown_cb =
      base::BindOnce(base::RunLoop::QuitCurrentWhenIdleClosureDeprecated());
  ui::OzonePlatform::GetInstance()->PostCreateMainMessageLoop(
      std::move(shutdown_cb));
  bluez::DBusBluezManagerWrapperLinux::Initialize();

  // Set up crypt config. This needs to be done before anything starts the
  // network service, as the raw encryption key needs to be shared with the
  // network service for encrypted cookie storage.
  std::string app_name = electron::Browser::Get()->GetName();
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  std::unique_ptr<os_crypt::Config> config =
      std::make_unique<os_crypt::Config>();
  // Forward to os_crypt the flag to use a specific password store.
  config->store = command_line.GetSwitchValueASCII(::switches::kPasswordStore);
  config->product_name = app_name;
  config->application_name = app_name;
  config->main_thread_runner = base::ThreadTaskRunnerHandle::Get();
  // c.f.
  // https://source.chromium.org/chromium/chromium/src/+/master:chrome/common/chrome_switches.cc;l=689;drc=9d82515060b9b75fa941986f5db7390299669ef1
  config->should_use_preference =
      command_line.HasSwitch(::switches::kEnableEncryptionSelection);
  base::PathService::Get(chrome::DIR_USER_DATA, &config->user_data_path);
  OSCrypt::SetConfig(std::move(config));
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

  // Destroy node platform after all destructors_ are executed, as they may
  // invoke Node/V8 APIs inside them.
  node_env_->env()->set_trace_sync_io(false);
  js_env_->OnMessageLoopDestroying();
  node::Stop(node_env_->env());
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

#if BUILDFLAG(IS_MAC)
device::GeolocationManager* ElectronBrowserMainParts::GetGeolocationManager() {
  return geolocation_manager_.get();
}
#endif

IconManager* ElectronBrowserMainParts::GetIconManager() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!icon_manager_.get())
    icon_manager_ = std::make_unique<IconManager>();
  return icon_manager_.get();
}

}  // namespace electron
