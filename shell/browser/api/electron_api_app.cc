// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_app.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/fixed_flat_map.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/callback_helpers.h"
#include "base/path_service.h"
#include "base/system/sys_info.h"
#include "base/values.h"
#include "base/win/windows_version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_paths.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "components/proxy_config/proxy_prefs.h"
#include "content/browser/gpu/compositor_util.h"        // nogncheck
#include "content/browser/gpu/gpu_data_manager_impl.h"  // nogncheck
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/client_certificate_delegate.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/content_switches.h"
#include "crypto/crypto_buildflags.h"
#include "media/audio/audio_manager.h"
#include "net/dns/public/dns_over_https_config.h"
#include "net/dns/public/dns_over_https_server_config.h"
#include "net/dns/public/util.h"
#include "net/ssl/client_cert_identity.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_private_key.h"
#include "sandbox/policy/switches.h"
#include "services/network/network_service.h"
#include "shell/app/command_line_args.h"
#include "shell/browser/api/electron_api_menu.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/api/electron_api_utility_process.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/api/gpuinfo_manager.h"
#include "shell/browser/browser_process_impl.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/electron_browser_main_parts.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/login_handler.h"
#include "shell/browser/relauncher.h"
#include "shell/common/application_info.h"
#include "shell/common/electron_command_line.h"
#include "shell/common/electron_paths.h"
#include "shell/common/gin_converters/base_converter.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_converters/net_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/language_util.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "shell/common/platform_util.h"
#include "shell/common/thread_restrictions.h"
#include "shell/common/v8_value_serializer.h"
#include "ui/gfx/image/image.h"

#if BUILDFLAG(IS_WIN)
#include "base/strings/utf_string_conversions.h"
#include "shell/browser/ui/win/jump_list.h"
#endif

#if BUILDFLAG(IS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include "shell/browser/ui/cocoa/electron_bundle_mover.h"
#endif

using electron::Browser;

namespace gin {

#if BUILDFLAG(IS_WIN)
template <>
struct Converter<electron::ProcessIntegrityLevel> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   electron::ProcessIntegrityLevel value) {
    switch (value) {
      case electron::ProcessIntegrityLevel::kUntrusted:
        return StringToV8(isolate, "untrusted");
      case electron::ProcessIntegrityLevel::kLow:
        return StringToV8(isolate, "low");
      case electron::ProcessIntegrityLevel::kMedium:
        return StringToV8(isolate, "medium");
      case electron::ProcessIntegrityLevel::kHigh:
        return StringToV8(isolate, "high");
      default:
        return StringToV8(isolate, "unknown");
    }
  }
};

template <>
struct Converter<Browser::UserTask> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     Browser::UserTask* out) {
    gin_helper::Dictionary dict;
    if (!ConvertFromV8(isolate, val, &dict))
      return false;
    if (!dict.Get("program", &(out->program)) ||
        !dict.Get("title", &(out->title)))
      return false;
    if (dict.Get("iconPath", &(out->icon_path)) &&
        !dict.Get("iconIndex", &(out->icon_index)))
      return false;
    dict.Get("arguments", &(out->arguments));
    dict.Get("description", &(out->description));
    dict.Get("workingDirectory", &(out->working_dir));
    return true;
  }
};

using electron::JumpListCategory;
using electron::JumpListItem;
using electron::JumpListResult;

template <>
struct Converter<JumpListItem::Type> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     JumpListItem::Type* out) {
    return FromV8WithLookup(isolate, val, Lookup, out);
  }

  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   JumpListItem::Type val) {
    for (const auto& [name, item_val] : Lookup)
      if (item_val == val)
        return gin::ConvertToV8(isolate, name);

    return gin::ConvertToV8(isolate, "");
  }

 private:
  static constexpr auto Lookup =
      base::MakeFixedFlatMap<std::string_view, JumpListItem::Type>({
          {"file", JumpListItem::Type::kFile},
          {"separator", JumpListItem::Type::kSeparator},
          {"task", JumpListItem::Type::kTask},
      });
};

template <>
struct Converter<JumpListItem> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     JumpListItem* out) {
    gin_helper::Dictionary dict;
    if (!ConvertFromV8(isolate, val, &dict))
      return false;

    if (!dict.Get("type", &(out->type)))
      return false;

    switch (out->type) {
      case JumpListItem::Type::kTask:
        if (!dict.Get("program", &(out->path)) ||
            !dict.Get("title", &(out->title)))
          return false;

        if (dict.Get("iconPath", &(out->icon_path)) &&
            !dict.Get("iconIndex", &(out->icon_index)))
          return false;

        dict.Get("args", &(out->arguments));
        dict.Get("description", &(out->description));
        dict.Get("workingDirectory", &(out->working_dir));
        return true;

      case JumpListItem::Type::kSeparator:
        return true;

      case JumpListItem::Type::kFile:
        return dict.Get("path", &(out->path));
    }

    assert(false);
    return false;
  }

  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const JumpListItem& val) {
    auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
    dict.Set("type", val.type);

    switch (val.type) {
      case JumpListItem::Type::kTask:
        dict.Set("program", val.path);
        dict.Set("args", val.arguments);
        dict.Set("title", val.title);
        dict.Set("iconPath", val.icon_path);
        dict.Set("iconIndex", val.icon_index);
        dict.Set("description", val.description);
        dict.Set("workingDirectory", val.working_dir);
        break;

      case JumpListItem::Type::kSeparator:
        break;

      case JumpListItem::Type::kFile:
        dict.Set("path", val.path);
        break;
    }
    return dict.GetHandle();
  }
};

template <>
struct Converter<JumpListCategory::Type> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     JumpListCategory::Type* out) {
    return FromV8WithLookup(isolate, val, Lookup, out);
  }

  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   JumpListCategory::Type val) {
    for (const auto& [name, type_val] : Lookup)
      if (type_val == val)
        return gin::ConvertToV8(isolate, name);

    return gin::ConvertToV8(isolate, "");
  }

 private:
  static constexpr auto Lookup =
      base::MakeFixedFlatMap<std::string_view, JumpListCategory::Type>({
          {"custom", JumpListCategory::Type::kCustom},
          {"frequent", JumpListCategory::Type::kFrequent},
          {"recent", JumpListCategory::Type::kRecent},
          {"tasks", JumpListCategory::Type::kTasks},
      });
};

template <>
struct Converter<JumpListCategory> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     JumpListCategory* out) {
    gin_helper::Dictionary dict;
    if (!ConvertFromV8(isolate, val, &dict))
      return false;

    if (dict.Get("name", &(out->name)) && out->name.empty())
      return false;

    if (!dict.Get("type", &(out->type))) {
      if (out->name.empty())
        out->type = JumpListCategory::Type::kTasks;
      else
        out->type = JumpListCategory::Type::kCustom;
    }

    if ((out->type == JumpListCategory::Type::kTasks) ||
        (out->type == JumpListCategory::Type::kCustom)) {
      if (!dict.Get("items", &(out->items)))
        return false;
    }

    return true;
  }
};

// static
template <>
struct Converter<JumpListResult> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate, JumpListResult val) {
    std::string result_code;
    switch (val) {
      case JumpListResult::kSuccess:
        result_code = "ok";
        break;

      case JumpListResult::kArgumentError:
        result_code = "argumentError";
        break;

      case JumpListResult::kGenericError:
        result_code = "error";
        break;

      case JumpListResult::kCustomCategorySeparatorError:
        result_code = "invalidSeparatorError";
        break;

      case JumpListResult::kMissingFileTypeRegistrationError:
        result_code = "fileTypeRegistrationError";
        break;

      case JumpListResult::kCustomCategoryAccessDeniedError:
        result_code = "customCategoryAccessDeniedError";
        break;
    }
    return ConvertToV8(isolate, result_code);
  }
};
#endif

template <>
struct Converter<content::CertificateRequestResultType> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     content::CertificateRequestResultType* out) {
    bool b;
    if (!ConvertFromV8(isolate, val, &b))
      return false;
    *out = b ? content::CERTIFICATE_REQUEST_RESULT_TYPE_CONTINUE
             : content::CERTIFICATE_REQUEST_RESULT_TYPE_DENY;
    return true;
  }
};

template <>
struct Converter<net::SecureDnsMode> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     net::SecureDnsMode* out) {
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, net::SecureDnsMode>({
            {"automatic", net::SecureDnsMode::kAutomatic},
            {"off", net::SecureDnsMode::kOff},
            {"secure", net::SecureDnsMode::kSecure},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};
}  // namespace gin

namespace electron::api {

gin::WrapperInfo App::kWrapperInfo = {gin::kEmbedderNativeGin};

namespace {

IconLoader::IconSize GetIconSizeByString(const std::string& size) {
  if (size == "small") {
    return IconLoader::IconSize::SMALL;
  } else if (size == "large") {
    return IconLoader::IconSize::LARGE;
  }
  return IconLoader::IconSize::NORMAL;
}

// Return the path constant from string.
int GetPathConstant(std::string_view name) {
  // clang-format off
  constexpr auto Lookup = base::MakeFixedFlatMap<std::string_view, int>({
      {"appData", DIR_APP_DATA},
#if BUILDFLAG(IS_POSIX)
      {"cache", base::DIR_CACHE},
#else
      {"cache", base::DIR_ROAMING_APP_DATA},
#endif
      {"crashDumps", DIR_CRASH_DUMPS},
      {"desktop", base::DIR_USER_DESKTOP},
      {"documents", chrome::DIR_USER_DOCUMENTS},
      {"downloads", chrome::DIR_DEFAULT_DOWNLOADS},
      {"exe", base::FILE_EXE},
      {"home", base::DIR_HOME},
      {"logs", DIR_APP_LOGS},
      {"module", base::FILE_MODULE},
      {"music", chrome::DIR_USER_MUSIC},
      {"pictures", chrome::DIR_USER_PICTURES},
#if BUILDFLAG(IS_WIN)
      {"recent", electron::DIR_RECENT},
#endif
      {"sessionData", DIR_SESSION_DATA},
      {"temp", base::DIR_TEMP},
      {"userCache", DIR_USER_CACHE},
      {"userData", chrome::DIR_USER_DATA},
      {"userDesktop", base::DIR_USER_DESKTOP},
      {"videos", chrome::DIR_USER_VIDEOS},
  });
  // clang-format on
  const auto* iter = Lookup.find(name);
  return iter != Lookup.end() ? iter->second : -1;
}

bool NotificationCallbackWrapper(
    const base::RepeatingCallback<
        void(base::CommandLine command_line,
             const base::FilePath& current_directory,
             const std::vector<uint8_t> additional_data)>& callback,
    base::CommandLine cmd,
    const base::FilePath& cwd,
    const std::vector<uint8_t> additional_data) {
  // Make sure the callback is called after app gets ready.
  if (Browser::Get()->is_ready()) {
    callback.Run(std::move(cmd), cwd, std::move(additional_data));
  } else {
    scoped_refptr<base::SingleThreadTaskRunner> task_runner(
        base::SingleThreadTaskRunner::GetCurrentDefault());

    // Make a copy of the span so that the data isn't lost.
    task_runner->PostTask(
        FROM_HERE, base::BindOnce(base::IgnoreResult(callback), std::move(cmd),
                                  cwd, std::move(additional_data)));
  }
  // ProcessSingleton needs to know whether current process is quitting.
  return !Browser::Get()->is_shutting_down();
}

void GotPrivateKey(std::shared_ptr<content::ClientCertificateDelegate> delegate,
                   scoped_refptr<net::X509Certificate> cert,
                   scoped_refptr<net::SSLPrivateKey> private_key) {
  delegate->ContinueWithCertificate(cert, private_key);
}

void OnClientCertificateSelected(
    v8::Isolate* isolate,
    std::shared_ptr<content::ClientCertificateDelegate> delegate,
    std::shared_ptr<net::ClientCertIdentityList> identities,
    gin::Arguments* args) {
  if (args->Length() == 2) {
    delegate->ContinueWithCertificate(nullptr, nullptr);
    return;
  }

  v8::Local<v8::Value> val;
  args->GetNext(&val);
  if (val->IsNull()) {
    delegate->ContinueWithCertificate(nullptr, nullptr);
    return;
  }

  gin_helper::Dictionary cert_data;
  if (!gin::ConvertFromV8(isolate, val, &cert_data)) {
    gin_helper::ErrorThrower(isolate).ThrowError(
        "Must pass valid certificate object.");
    return;
  }

  std::string data;
  if (!cert_data.Get("data", &data))
    return;

  auto certs = net::X509Certificate::CreateCertificateListFromBytes(
      base::as_bytes(base::make_span(data)), net::X509Certificate::FORMAT_AUTO);
  if (!certs.empty()) {
    scoped_refptr<net::X509Certificate> cert(certs[0].get());
    for (auto& identity : *identities) {
      scoped_refptr<net::X509Certificate> identity_cert =
          identity->certificate();
      // Since |cert| was recreated from |data|, it won't include any
      // intermediates. That's fine for checking equality, but once a match is
      // found then |identity_cert| should be used since it will include the
      // intermediates which would otherwise be lost.
      if (cert->EqualsExcludingChain(identity_cert.get())) {
        net::ClientCertIdentity::SelfOwningAcquirePrivateKey(
            std::move(identity), base::BindRepeating(&GotPrivateKey, delegate,
                                                     std::move(identity_cert)));
        break;
      }
    }
  }
}

#if BUILDFLAG(USE_NSS_CERTS)
int ImportIntoCertStore(CertificateManagerModel* model, base::Value options) {
  std::string file_data, cert_path;
  std::u16string password;
  net::ScopedCERTCertificateList imported_certs;
  int rv = -1;

  if (const base::Value::Dict* dict = options.GetIfDict(); dict != nullptr) {
    if (const std::string* str = dict->FindString("certificate"); str)
      cert_path = *str;

    if (const std::string* str = dict->FindString("password"); str)
      password = base::UTF8ToUTF16(*str);
  }

  if (!cert_path.empty()) {
    if (base::ReadFileToString(base::FilePath(cert_path), &file_data)) {
      auto module = model->cert_db()->GetPrivateSlot();
      rv = model->ImportFromPKCS12(module.get(), file_data, password, true,
                                   &imported_certs);
      if (imported_certs.size() > 1) {
        auto it = imported_certs.begin();
        ++it;  // skip first which would  be the client certificate.
        for (; it != imported_certs.end(); ++it)
          rv &= model->SetCertTrust(it->get(), net::CA_CERT,
                                    net::NSSCertDatabase::TRUSTED_SSL);
      }
    }
  }
  return rv;
}
#endif

void OnIconDataAvailable(gin_helper::Promise<gfx::Image> promise,
                         gfx::Image icon) {
  if (!icon.IsEmpty()) {
    promise.Resolve(icon);
  } else {
    promise.RejectWithErrorMessage("Failed to get file icon.");
  }
}

}  // namespace

App::App() {
  static_cast<ElectronBrowserClient*>(ElectronBrowserClient::Get())
      ->set_delegate(this);
  Browser::Get()->AddObserver(this);

  auto pid = content::ChildProcessHost::kInvalidUniqueID;
  auto process_metric = std::make_unique<electron::ProcessMetric>(
      content::PROCESS_TYPE_BROWSER, base::GetCurrentProcessHandle(),
      base::ProcessMetrics::CreateCurrentProcessMetrics());
  app_metrics_[pid] = std::move(process_metric);
}

App::~App() {
  static_cast<ElectronBrowserClient*>(ElectronBrowserClient::Get())
      ->set_delegate(nullptr);
  Browser::Get()->RemoveObserver(this);
  content::GpuDataManager::GetInstance()->RemoveObserver(this);
  content::BrowserChildProcessObserver::Remove(this);
}

void App::OnBeforeQuit(bool* prevent_default) {
  if (Emit("before-quit")) {
    *prevent_default = true;
  }
}

void App::OnWillQuit(bool* prevent_default) {
  if (Emit("will-quit")) {
    *prevent_default = true;
  }
}

void App::OnWindowAllClosed() {
  Emit("window-all-closed");
}

void App::OnQuit() {
  const int exitCode = ElectronBrowserMainParts::Get()->GetExitCode();
  Emit("quit", exitCode);

  if (process_singleton_) {
    process_singleton_->Cleanup();
    process_singleton_.reset();
  }
}

void App::OnOpenFile(bool* prevent_default, const std::string& file_path) {
  if (Emit("open-file", file_path)) {
    *prevent_default = true;
  }
}

void App::OnOpenURL(const std::string& url) {
  Emit("open-url", url);
}

void App::OnActivate(bool has_visible_windows) {
  Emit("activate", has_visible_windows);
}

void App::OnWillFinishLaunching() {
  Emit("will-finish-launching");
}

void App::OnFinishLaunching(base::Value::Dict launch_info) {
#if BUILDFLAG(IS_LINUX)
  // Set the application name for audio streams shown in external
  // applications. Only affects pulseaudio currently.
  media::AudioManager::SetGlobalAppName(Browser::Get()->GetName());
#endif
  Emit("ready", base::Value(std::move(launch_info)));
}

void App::OnPreMainMessageLoopRun() {
  content::BrowserChildProcessObserver::Add(this);
  if (process_singleton_ && watch_singleton_socket_on_ready_) {
    process_singleton_->StartWatching();
    watch_singleton_socket_on_ready_ = false;
  }
}

void App::OnPreCreateThreads() {
  DCHECK(!content::GpuDataManager::Initialized());
  content::GpuDataManager* manager = content::GpuDataManager::GetInstance();
  manager->AddObserver(this);

  if (disable_hw_acceleration_) {
    manager->DisableHardwareAcceleration();
  }

  if (disable_domain_blocking_for_3DAPIs_) {
    content::GpuDataManagerImpl::GetInstance()
        ->DisableDomainBlockingFor3DAPIsForTesting();
  }
}

void App::OnAccessibilitySupportChanged() {
  Emit("accessibility-support-changed", IsAccessibilitySupportEnabled());
}

#if BUILDFLAG(IS_MAC)
void App::OnWillContinueUserActivity(bool* prevent_default,
                                     const std::string& type) {
  if (Emit("will-continue-activity", type)) {
    *prevent_default = true;
  }
}

void App::OnDidFailToContinueUserActivity(const std::string& type,
                                          const std::string& error) {
  Emit("continue-activity-error", type, error);
}

void App::OnContinueUserActivity(bool* prevent_default,
                                 const std::string& type,
                                 base::Value::Dict user_info,
                                 base::Value::Dict details) {
  if (Emit("continue-activity", type, base::Value(std::move(user_info)),
           base::Value(std::move(details)))) {
    *prevent_default = true;
  }
}

void App::OnUserActivityWasContinued(const std::string& type,
                                     base::Value::Dict user_info) {
  Emit("activity-was-continued", type, base::Value(std::move(user_info)));
}

void App::OnUpdateUserActivityState(bool* prevent_default,
                                    const std::string& type,
                                    base::Value::Dict user_info) {
  if (Emit("update-activity-state", type, base::Value(std::move(user_info)))) {
    *prevent_default = true;
  }
}

void App::OnNewWindowForTab() {
  Emit("new-window-for-tab");
}

void App::OnDidBecomeActive() {
  Emit("did-become-active");
}

void App::OnDidResignActive() {
  Emit("did-resign-active");
}
#endif

bool App::CanCreateWindow(
    content::RenderFrameHost* opener,
    const GURL& opener_url,
    const GURL& opener_top_level_frame_url,
    const url::Origin& source_origin,
    content::mojom::WindowContainerType container_type,
    const GURL& target_url,
    const content::Referrer& referrer,
    const std::string& frame_name,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& features,
    const std::string& raw_features,
    const scoped_refptr<network::ResourceRequestBody>& body,
    bool user_gesture,
    bool opener_suppressed,
    bool* no_javascript_access) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(opener);
  if (web_contents) {
    WebContents* api_web_contents = WebContents::From(web_contents);
    // No need to emit any event if the WebContents is not available in JS.
    if (api_web_contents) {
      api_web_contents->OnCreateWindow(target_url, referrer, frame_name,
                                       disposition, raw_features, body);
    }
  }

  return false;
}

void App::AllowCertificateError(
    content::WebContents* web_contents,
    int cert_error,
    const net::SSLInfo& ssl_info,
    const GURL& request_url,
    bool is_main_frame_request,
    bool strict_enforcement,
    base::OnceCallback<void(content::CertificateRequestResultType)> callback) {
  auto adapted_callback = base::AdaptCallbackForRepeating(std::move(callback));
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  bool prevent_default = Emit(
      "certificate-error", WebContents::FromOrCreate(isolate, web_contents),
      request_url, net::ErrorToString(cert_error), ssl_info.cert,
      adapted_callback, is_main_frame_request);

  // Deny the certificate by default.
  if (!prevent_default)
    adapted_callback.Run(content::CERTIFICATE_REQUEST_RESULT_TYPE_DENY);
}

base::OnceClosure App::SelectClientCertificate(
    content::BrowserContext* browser_context,
    content::WebContents* web_contents,
    net::SSLCertRequestInfo* cert_request_info,
    net::ClientCertIdentityList identities,
    std::unique_ptr<content::ClientCertificateDelegate> delegate) {
  std::shared_ptr<content::ClientCertificateDelegate> shared_delegate(
      delegate.release());

  // Convert the ClientCertIdentityList to a CertificateList
  // to avoid changes in the API.
  auto client_certs = net::CertificateList();
  for (const std::unique_ptr<net::ClientCertIdentity>& identity : identities)
    client_certs.emplace_back(identity->certificate());

  auto shared_identities =
      std::make_shared<net::ClientCertIdentityList>(std::move(identities));

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  bool prevent_default =
      Emit("select-client-certificate",
           WebContents::FromOrCreate(isolate, web_contents),
           cert_request_info->host_and_port.ToString(), std::move(client_certs),
           base::BindOnce(&OnClientCertificateSelected, isolate,
                          shared_delegate, shared_identities));

  // Default to first certificate from the platform store.
  if (!prevent_default) {
    scoped_refptr<net::X509Certificate> cert =
        (*shared_identities)[0]->certificate();
    net::ClientCertIdentity::SelfOwningAcquirePrivateKey(
        std::move((*shared_identities)[0]),
        base::BindRepeating(&GotPrivateKey, shared_delegate, std::move(cert)));
  }
  return base::OnceClosure();
}

void App::OnGpuInfoUpdate() {
  Emit("gpu-info-update");
}

void App::BrowserChildProcessLaunchedAndConnected(
    const content::ChildProcessData& data) {
  ChildProcessLaunched(data.process_type, data.id, data.GetProcess().Handle(),
                       data.metrics_name, base::UTF16ToUTF8(data.name));
}

void App::BrowserChildProcessHostDisconnected(
    const content::ChildProcessData& data) {
  ChildProcessDisconnected(data.id);
}

void App::BrowserChildProcessCrashed(
    const content::ChildProcessData& data,
    const content::ChildProcessTerminationInfo& info) {
  ChildProcessDisconnected(data.id);
  BrowserChildProcessCrashedOrKilled(data, info);
}

void App::BrowserChildProcessKilled(
    const content::ChildProcessData& data,
    const content::ChildProcessTerminationInfo& info) {
  ChildProcessDisconnected(data.id);
  BrowserChildProcessCrashedOrKilled(data, info);
}

void App::BrowserChildProcessCrashedOrKilled(
    const content::ChildProcessData& data,
    const content::ChildProcessTerminationInfo& info) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto details = gin_helper::Dictionary::CreateEmpty(isolate);
  details.Set("type", content::GetProcessTypeNameInEnglish(data.process_type));
  details.Set("reason", info.status);
  details.Set("exitCode", info.exit_code);
  details.Set("serviceName", data.metrics_name);
  if (!data.name.empty()) {
    details.Set("name", data.name);
  }
  Emit("child-process-gone", details);
}

void App::RenderProcessReady(content::RenderProcessHost* host) {
  ChildProcessLaunched(content::PROCESS_TYPE_RENDERER, host->GetID(),
                       host->GetProcess().Handle());
}

void App::RenderProcessExited(content::RenderProcessHost* host) {
  ChildProcessDisconnected(host->GetID());
}

void App::ChildProcessLaunched(int process_type,
                               int pid,
                               base::ProcessHandle handle,
                               const std::string& service_name,
                               const std::string& name) {
#if BUILDFLAG(IS_MAC)
  auto metrics = base::ProcessMetrics::CreateProcessMetrics(
      handle, content::BrowserChildProcessHost::GetPortProvider());
#else
  auto metrics = base::ProcessMetrics::CreateProcessMetrics(handle);
#endif
  app_metrics_[pid] = std::make_unique<electron::ProcessMetric>(
      process_type, handle, std::move(metrics), service_name, name);
}

void App::ChildProcessDisconnected(int pid) {
  app_metrics_.erase(pid);
}

base::FilePath App::GetAppPath() const {
  return app_path_;
}

void App::SetAppPath(const base::FilePath& app_path) {
  app_path_ = app_path;
}

#if !BUILDFLAG(IS_MAC)
void App::SetAppLogsPath(gin_helper::ErrorThrower thrower,
                         std::optional<base::FilePath> custom_path) {
  if (custom_path.has_value()) {
    if (!custom_path->IsAbsolute()) {
      thrower.ThrowError("Path must be absolute");
      return;
    }
    {
      ScopedAllowBlockingForElectron allow_blocking;
      base::PathService::Override(DIR_APP_LOGS, custom_path.value());
    }
  } else {
    base::FilePath path;
    if (base::PathService::Get(chrome::DIR_USER_DATA, &path)) {
      path = path.Append(base::FilePath::FromUTF8Unsafe("logs"));
      {
        ScopedAllowBlockingForElectron allow_blocking;
        base::PathService::Override(DIR_APP_LOGS, path);
      }
    }
  }
}
#endif

// static
bool App::IsPackaged() {
  auto env = base::Environment::Create();
  if (env->HasVar("ELECTRON_FORCE_IS_PACKAGED"))
    return true;

  base::FilePath exe_path;
  base::PathService::Get(base::FILE_EXE, &exe_path);
  base::FilePath::StringType base_name =
      base::ToLowerASCII(exe_path.BaseName().value());

#if BUILDFLAG(IS_WIN)
  return base_name != FILE_PATH_LITERAL("electron.exe");
#else
  return base_name != FILE_PATH_LITERAL("electron");
#endif
}

base::FilePath App::GetPath(gin_helper::ErrorThrower thrower,
                            const std::string& name) {
  base::FilePath path;

  int key = GetPathConstant(name);
  if (key < 0 || !base::PathService::Get(key, &path))
    thrower.ThrowError("Failed to get '" + name + "' path");

  return path;
}

void App::SetPath(gin_helper::ErrorThrower thrower,
                  const std::string& name,
                  const base::FilePath& path) {
  if (!path.IsAbsolute()) {
    thrower.ThrowError("Path must be absolute");
    return;
  }

  int key = GetPathConstant(name);
  if (key < 0 || !base::PathService::OverrideAndCreateIfNeeded(
                     key, path, /* is_absolute = */ true, /* create = */ false))
    thrower.ThrowError("Failed to set path");
}

void App::SetDesktopName(const std::string& desktop_name) {
#if BUILDFLAG(IS_LINUX)
  auto env = base::Environment::Create();
  env->SetVar("CHROME_DESKTOP", desktop_name);
#endif
}

std::string App::GetLocale() {
  return g_browser_process->GetApplicationLocale();
}

std::string App::GetSystemLocale(gin_helper::ErrorThrower thrower) const {
  if (!Browser::Get()->is_ready()) {
    thrower.ThrowError(
        "app.getSystemLocale() can only be called "
        "after app is ready");
    return std::string();
  }
  return static_cast<BrowserProcessImpl*>(g_browser_process)->GetSystemLocale();
}

std::string App::GetLocaleCountryCode() {
  std::string region;
#if BUILDFLAG(IS_WIN)
  WCHAR locale_name[LOCALE_NAME_MAX_LENGTH] = {0};

  if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SISO3166CTRYNAME,
                      (LPWSTR)&locale_name,
                      sizeof(locale_name) / sizeof(WCHAR)) ||
      GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_SISO3166CTRYNAME,
                      (LPWSTR)&locale_name,
                      sizeof(locale_name) / sizeof(WCHAR))) {
    base::WideToUTF8(locale_name, wcslen(locale_name), &region);
  }
#elif BUILDFLAG(IS_MAC)
  CFLocaleRef locale = CFLocaleCopyCurrent();
  CFStringRef value = CFStringRef(
      static_cast<CFTypeRef>(CFLocaleGetValue(locale, kCFLocaleCountryCode)));
  if (value != nil) {
    char temporaryCString[3];
    const CFIndex kCStringSize = sizeof(temporaryCString);
    if (CFStringGetCString(value, temporaryCString, kCStringSize,
                           kCFStringEncodingUTF8)) {
      region = temporaryCString;
    }
  }
#else
  const char* locale_ptr = setlocale(LC_TIME, nullptr);
  if (!locale_ptr)
    locale_ptr = setlocale(LC_NUMERIC, nullptr);
  if (locale_ptr) {
    std::string locale = locale_ptr;
    std::string::size_type rpos = locale.find('.');
    if (rpos != std::string::npos)
      locale = locale.substr(0, rpos);
    rpos = locale.find('_');
    if (rpos != std::string::npos && rpos + 1 < locale.size())
      region = locale.substr(rpos + 1);
  }
#endif
  return region.size() == 2 ? region : std::string();
}

void App::OnSecondInstance(base::CommandLine cmd,
                           const base::FilePath& cwd,
                           const std::vector<uint8_t> additional_data) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Value> data_value =
      DeserializeV8Value(isolate, std::move(additional_data));
  Emit("second-instance", cmd.argv(), cwd, data_value);
}

bool App::HasSingleInstanceLock() const {
  if (process_singleton_)
    return true;
  return false;
}

bool App::RequestSingleInstanceLock(gin::Arguments* args) {
  if (HasSingleInstanceLock())
    return true;

  std::string program_name = electron::Browser::Get()->GetName();

  base::FilePath user_dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_dir);
  // The user_dir may not have been created yet.
  base::CreateDirectoryAndGetError(user_dir, nullptr);

  auto cb = base::BindRepeating(&App::OnSecondInstance, base::Unretained(this));

  blink::CloneableMessage additional_data_message;
  args->GetNext(&additional_data_message);
#if BUILDFLAG(IS_WIN)
  bool app_is_sandboxed =
      IsSandboxEnabled(base::CommandLine::ForCurrentProcess());
  process_singleton_ = std::make_unique<ProcessSingleton>(
      program_name, user_dir, additional_data_message.encoded_message,
      app_is_sandboxed, base::BindRepeating(NotificationCallbackWrapper, cb));
#else
  process_singleton_ = std::make_unique<ProcessSingleton>(
      user_dir, additional_data_message.encoded_message,
      base::BindRepeating(NotificationCallbackWrapper, cb));
#endif

  switch (process_singleton_->NotifyOtherProcessOrCreate()) {
    case ProcessSingleton::NotifyResult::PROCESS_NONE:
      if (content::BrowserThread::IsThreadInitialized(
              content::BrowserThread::IO)) {
        process_singleton_->StartWatching();
      } else {
        watch_singleton_socket_on_ready_ = true;
      }
      return true;
    case ProcessSingleton::NotifyResult::LOCK_ERROR:
    case ProcessSingleton::NotifyResult::PROFILE_IN_USE:
    case ProcessSingleton::NotifyResult::PROCESS_NOTIFIED: {
      process_singleton_.reset();
      return false;
    }
  }
}

void App::ReleaseSingleInstanceLock() {
  if (process_singleton_) {
    process_singleton_->Cleanup();
    process_singleton_.reset();
  }
}

bool App::Relaunch(gin::Arguments* js_args) {
  // Parse parameters.
  bool override_argv = false;
  base::FilePath exec_path;
  relauncher::StringVector args;

  gin_helper::Dictionary options;
  if (js_args->GetNext(&options)) {
    bool has_exec_path = options.Get("execPath", &exec_path);
    bool has_args = options.Get("args", &args);
    if (has_exec_path || has_args)
      override_argv = true;
  }

  if (!override_argv) {
    const relauncher::StringVector& argv =
        electron::ElectronCommandLine::argv();
    return relauncher::RelaunchApp(argv);
  }

  relauncher::StringVector argv;
  argv.reserve(1 + args.size());

  if (exec_path.empty()) {
    base::FilePath current_exe_path;
    base::PathService::Get(base::FILE_EXE, &current_exe_path);
    argv.push_back(current_exe_path.value());
  } else {
    argv.push_back(exec_path.value());
  }

  argv.insert(argv.end(), args.begin(), args.end());

  return relauncher::RelaunchApp(argv);
}

void App::DisableHardwareAcceleration(gin_helper::ErrorThrower thrower) {
  if (Browser::Get()->is_ready()) {
    thrower.ThrowError(
        "app.disableHardwareAcceleration() can only be called "
        "before app is ready");
    return;
  }
  if (content::GpuDataManager::Initialized()) {
    content::GpuDataManager::GetInstance()->DisableHardwareAcceleration();
  } else {
    disable_hw_acceleration_ = true;
  }
}

void App::DisableDomainBlockingFor3DAPIs(gin_helper::ErrorThrower thrower) {
  if (Browser::Get()->is_ready()) {
    thrower.ThrowError(
        "app.disableDomainBlockingFor3DAPIs() can only be called "
        "before app is ready");
    return;
  }
  if (content::GpuDataManager::Initialized()) {
    content::GpuDataManagerImpl::GetInstance()
        ->DisableDomainBlockingFor3DAPIsForTesting();
  } else {
    disable_domain_blocking_for_3DAPIs_ = true;
  }
}

bool App::IsAccessibilitySupportEnabled() {
  auto* ax_state = content::BrowserAccessibilityState::GetInstance();
  return ax_state->IsAccessibleBrowser();
}

void App::SetAccessibilitySupportEnabled(gin_helper::ErrorThrower thrower,
                                         bool enabled) {
  if (!Browser::Get()->is_ready()) {
    thrower.ThrowError(
        "app.setAccessibilitySupportEnabled() can only be called "
        "after app is ready");
    return;
  }

  auto* ax_state = content::BrowserAccessibilityState::GetInstance();
  if (enabled) {
    ax_state->OnScreenReaderDetected();
  } else {
    ax_state->DisableAccessibility();
  }
  Browser::Get()->OnAccessibilitySupportChanged();
}

v8::Local<v8::Value> App::GetLoginItemSettings(gin::Arguments* args) {
  LoginItemSettings options;
  args->GetNext(&options);
  return Browser::Get()->GetLoginItemSettings(options);
}

#if BUILDFLAG(USE_NSS_CERTS)
void App::ImportCertificate(gin_helper::ErrorThrower thrower,
                            base::Value options,
                            net::CompletionOnceCallback callback) {
  if (!options.is_dict()) {
    thrower.ThrowTypeError("Expected options to be an object");
    return;
  }

  if (!certificate_manager_model_) {
    CertificateManagerModel::Create(base::BindOnce(
        &App::OnCertificateManagerModelCreated, base::Unretained(this),
        std::move(options), std::move(callback)));
    return;
  }

  int rv =
      ImportIntoCertStore(certificate_manager_model_.get(), std::move(options));
  std::move(callback).Run(rv);
}

void App::OnCertificateManagerModelCreated(
    base::Value options,
    net::CompletionOnceCallback callback,
    std::unique_ptr<CertificateManagerModel> model) {
  certificate_manager_model_ = std::move(model);
  int rv =
      ImportIntoCertStore(certificate_manager_model_.get(), std::move(options));
  std::move(callback).Run(rv);
}
#endif

#if BUILDFLAG(IS_WIN)
v8::Local<v8::Value> App::GetJumpListSettings() {
  JumpList jump_list(Browser::Get()->GetAppUserModelID());

  int min_items = 10;
  std::vector<JumpListItem> removed_items;
  if (jump_list.Begin(&min_items, &removed_items)) {
    // We don't actually want to change anything, so abort the transaction.
    jump_list.Abort();
  } else {
    LOG(ERROR) << "Failed to begin Jump List transaction.";
  }

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("minItems", min_items);
  dict.Set("removedItems", gin::ConvertToV8(isolate, removed_items));
  return dict.GetHandle();
}

JumpListResult App::SetJumpList(v8::Local<v8::Value> val,
                                gin::Arguments* args) {
  std::vector<JumpListCategory> categories;
  bool delete_jump_list = val->IsNull();
  if (!delete_jump_list &&
      !gin::ConvertFromV8(args->isolate(), val, &categories)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowTypeError("Argument must be null or an array of categories");
    return JumpListResult::kArgumentError;
  }

  JumpList jump_list(Browser::Get()->GetAppUserModelID());

  if (delete_jump_list) {
    return jump_list.Delete() ? JumpListResult::kSuccess
                              : JumpListResult::kGenericError;
  }

  // Start a transaction that updates the JumpList of this application.
  if (!jump_list.Begin())
    return JumpListResult::kGenericError;

  JumpListResult result = jump_list.AppendCategories(categories);
  // AppendCategories may have failed to add some categories, but it's better
  // to have something than nothing so try to commit the changes anyway.
  if (!jump_list.Commit()) {
    LOG(ERROR) << "Failed to commit changes to custom Jump List.";
    // It's more useful to return the earlier error code that might give
    // some indication as to why the transaction actually failed, so don't
    // overwrite it with a "generic error" code here.
    if (result == JumpListResult::kSuccess)
      result = JumpListResult::kGenericError;
  }

  return result;
}
#endif  // BUILDFLAG(IS_WIN)

v8::Local<v8::Promise> App::GetFileIcon(const base::FilePath& path,
                                        gin::Arguments* args) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  gin_helper::Promise<gfx::Image> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();
  base::FilePath normalized_path = path.NormalizePathSeparators();

  IconLoader::IconSize icon_size;
  gin_helper::Dictionary options;
  if (!args->GetNext(&options)) {
    icon_size = IconLoader::IconSize::NORMAL;
  } else {
    std::string icon_size_string;
    options.Get("size", &icon_size_string);
    icon_size = GetIconSizeByString(icon_size_string);
  }

  auto* icon_manager = ElectronBrowserMainParts::Get()->GetIconManager();
  gfx::Image* icon =
      icon_manager->LookupIconFromFilepath(normalized_path, icon_size, 1.0f);
  if (icon) {
    promise.Resolve(*icon);
  } else {
    icon_manager->LoadIcon(
        normalized_path, icon_size, 1.0f,
        base::BindOnce(&OnIconDataAvailable, std::move(promise)),
        &cancelable_task_tracker_);
  }
  return handle;
}

std::vector<gin_helper::Dictionary> App::GetAppMetrics(v8::Isolate* isolate) {
  std::vector<gin_helper::Dictionary> result;
  result.reserve(app_metrics_.size());
  int processor_count = base::SysInfo::NumberOfProcessors();

  for (const auto& process_metric : app_metrics_) {
    auto pid_dict = gin_helper::Dictionary::CreateEmpty(isolate);
    auto cpu_dict = gin_helper::Dictionary::CreateEmpty(isolate);

    // Default usage percentage to 0 for compatibility
    double usagePercent = 0;
    if (auto usage = process_metric.second->metrics->GetCumulativeCPUUsage();
        usage.has_value()) {
      cpu_dict.Set("cumulativeCPUUsage", usage->InSecondsF());
      usagePercent =
          process_metric.second->metrics->GetPlatformIndependentCPUUsage(
              *usage);
    }

    cpu_dict.Set("percentCPUUsage", usagePercent / processor_count);

#if !BUILDFLAG(IS_WIN)
    cpu_dict.Set("idleWakeupsPerSecond",
                 process_metric.second->metrics->GetIdleWakeupsPerSecond());
#else
    // Chrome's underlying process_metrics.cc will throw a non-fatal warning
    // that this method isn't implemented on Windows, so set it to 0 instead
    // of calling it
    cpu_dict.Set("idleWakeupsPerSecond", 0);
#endif

    pid_dict.Set("cpu", cpu_dict);
    pid_dict.Set("pid", process_metric.second->process.Pid());
    pid_dict.Set("type", content::GetProcessTypeNameInEnglish(
                             process_metric.second->type));
    pid_dict.Set("creationTime", process_metric.second->process.CreationTime()
                                     .InMillisecondsFSinceUnixEpoch());

    if (!process_metric.second->service_name.empty()) {
      pid_dict.Set("serviceName", process_metric.second->service_name);
    }

    if (!process_metric.second->name.empty()) {
      pid_dict.Set("name", process_metric.second->name);
    }

#if !BUILDFLAG(IS_LINUX)
    auto memory_info = process_metric.second->GetMemoryInfo();

    auto memory_dict = gin_helper::Dictionary::CreateEmpty(isolate);
    memory_dict.Set("workingSetSize",
                    static_cast<double>(memory_info.working_set_size >> 10));
    memory_dict.Set(
        "peakWorkingSetSize",
        static_cast<double>(memory_info.peak_working_set_size >> 10));

#if BUILDFLAG(IS_WIN)
    memory_dict.Set("privateBytes",
                    static_cast<double>(memory_info.private_bytes >> 10));
#endif

    pid_dict.Set("memory", memory_dict);
#endif

#if BUILDFLAG(IS_MAC)
    pid_dict.Set("sandboxed", process_metric.second->IsSandboxed());
#elif BUILDFLAG(IS_WIN)
    auto integrity_level = process_metric.second->GetIntegrityLevel();
    auto sandboxed = ProcessMetric::IsSandboxed(integrity_level);
    pid_dict.Set("integrityLevel", integrity_level);
    pid_dict.Set("sandboxed", sandboxed);
#endif

    result.push_back(pid_dict);
  }

  return result;
}

v8::Local<v8::Value> App::GetGPUFeatureStatus(v8::Isolate* isolate) {
  return gin::ConvertToV8(isolate, content::GetFeatureStatus());
}

v8::Local<v8::Promise> App::GetGPUInfo(v8::Isolate* isolate,
                                       const std::string& info_type) {
  auto* const gpu_data_manager = content::GpuDataManagerImpl::GetInstance();
  gin_helper::Promise<base::Value> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();
  if (info_type != "basic" && info_type != "complete") {
    promise.RejectWithErrorMessage(
        "Invalid info type. Use 'basic' or 'complete'");
    return handle;
  }
  std::string reason;
  if (!gpu_data_manager->GpuAccessAllowed(&reason)) {
    promise.RejectWithErrorMessage("GPU access not allowed. Reason: " + reason);
    return handle;
  }

  auto* const info_mgr = GPUInfoManager::GetInstance();
  if (info_type == "complete") {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
    info_mgr->FetchCompleteInfo(std::move(promise));
#else
    info_mgr->FetchBasicInfo(std::move(promise));
#endif
  } else /* (info_type == "basic") */ {
    info_mgr->FetchBasicInfo(std::move(promise));
  }
  return handle;
}

static void RemoveNoSandboxSwitch(base::CommandLine* command_line) {
  if (command_line->HasSwitch(sandbox::policy::switches::kNoSandbox)) {
    const base::CommandLine::CharType* noSandboxArg =
        FILE_PATH_LITERAL("--no-sandbox");
    base::CommandLine::StringVector modified_command_line;
    for (auto& arg : command_line->argv()) {
      if (arg.compare(noSandboxArg) != 0) {
        modified_command_line.push_back(arg);
      }
    }
    command_line->InitFromArgv(modified_command_line);
  }
}

void App::EnableSandbox(gin_helper::ErrorThrower thrower) {
  if (Browser::Get()->is_ready()) {
    thrower.ThrowError(
        "app.enableSandbox() can only be called "
        "before app is ready");
    return;
  }

  auto* command_line = base::CommandLine::ForCurrentProcess();
  RemoveNoSandboxSwitch(command_line);
  command_line->AppendSwitch(switches::kEnableSandbox);
}

v8::Local<v8::Promise> App::SetProxy(gin::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  gin_helper::Promise<void> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  gin_helper::Dictionary options;
  args->GetNext(&options);

  if (!Browser::Get()->is_ready()) {
    promise.RejectWithErrorMessage(
        "app.setProxy() can only be called after app is ready.");
    return handle;
  }

  if (!g_browser_process->local_state()) {
    promise.RejectWithErrorMessage(
        "app.setProxy() failed due to internal error.");
    return handle;
  }

  std::string mode, proxy_rules, bypass_list, pac_url;

  options.Get("pacScript", &pac_url);
  options.Get("proxyRules", &proxy_rules);
  options.Get("proxyBypassRules", &bypass_list);

  ProxyPrefs::ProxyMode proxy_mode = ProxyPrefs::MODE_FIXED_SERVERS;
  if (!options.Get("mode", &mode)) {
    // pacScript takes precedence over proxyRules.
    if (!pac_url.empty()) {
      proxy_mode = ProxyPrefs::MODE_PAC_SCRIPT;
    }
  } else if (!ProxyPrefs::StringToProxyMode(mode, &proxy_mode)) {
    promise.RejectWithErrorMessage(
        "Invalid mode, must be one of direct, auto_detect, pac_script, "
        "fixed_servers or system");
    return handle;
  }

  base::Value::Dict proxy_config;
  switch (proxy_mode) {
    case ProxyPrefs::MODE_DIRECT:
      proxy_config = ProxyConfigDictionary::CreateDirect();
      break;
    case ProxyPrefs::MODE_SYSTEM:
      proxy_config = ProxyConfigDictionary::CreateSystem();
      break;
    case ProxyPrefs::MODE_AUTO_DETECT:
      proxy_config = ProxyConfigDictionary::CreateAutoDetect();
      break;
    case ProxyPrefs::MODE_PAC_SCRIPT:
      proxy_config = ProxyConfigDictionary::CreatePacScript(pac_url, true);
      break;
    case ProxyPrefs::MODE_FIXED_SERVERS:
      proxy_config =
          ProxyConfigDictionary::CreateFixedServers(proxy_rules, bypass_list);
      break;
    default:
      NOTIMPLEMENTED();
  }

  static_cast<BrowserProcessImpl*>(g_browser_process)
      ->in_memory_pref_store()
      ->SetValue(proxy_config::prefs::kProxy,
                 base::Value{std::move(proxy_config)},
                 WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);

  g_browser_process->system_network_context_manager()
      ->GetContext()
      ->ForceReloadProxyConfig(base::BindOnce(
          gin_helper::Promise<void>::ResolvePromise, std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> App::ResolveProxy(gin::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  gin_helper::Promise<std::string> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  GURL url;
  args->GetNext(&url);

  static_cast<BrowserProcessImpl*>(g_browser_process)
      ->GetResolveProxyHelper()
      ->ResolveProxy(
          url, base::BindOnce(gin_helper::Promise<std::string>::ResolvePromise,
                              std::move(promise)));

  return handle;
}

void App::SetUserAgentFallback(const std::string& user_agent) {
  ElectronBrowserClient::Get()->SetUserAgent(user_agent);
}

#if BUILDFLAG(IS_WIN)
bool App::IsRunningUnderARM64Translation() const {
  return base::win::OSInfo::IsRunningEmulatedOnArm64();
}
#endif

std::string App::GetUserAgentFallback() {
  return ElectronBrowserClient::Get()->GetUserAgent();
}

#if BUILDFLAG(IS_MAC)
bool App::MoveToApplicationsFolder(gin_helper::ErrorThrower thrower,
                                   gin::Arguments* args) {
  return ElectronBundleMover::Move(thrower, args);
}

bool App::IsInApplicationsFolder() {
  return ElectronBundleMover::IsCurrentAppInApplicationsFolder();
}

int DockBounce(gin::Arguments* args) {
  int request_id = -1;
  std::string type = "informational";
  args->GetNext(&type);

  if (type == "critical")
    request_id = Browser::Get()->DockBounce(Browser::BounceType::kCritical);
  else if (type == "informational")
    request_id =
        Browser::Get()->DockBounce(Browser::BounceType::kInformational);
  return request_id;
}

void DockSetMenu(electron::api::Menu* menu) {
  Browser::Get()->DockSetMenu(menu->model());
}

v8::Local<v8::Value> App::GetDockAPI(v8::Isolate* isolate) {
  if (dock_.IsEmpty()) {
    // Initialize the Dock API, the methods are bound to "dock" which exists
    // for the lifetime of "app"
    auto browser = base::Unretained(Browser::Get());
    auto dock_obj = gin_helper::Dictionary::CreateEmpty(isolate);
    dock_obj.SetMethod("bounce", &DockBounce);
    dock_obj.SetMethod(
        "cancelBounce",
        base::BindRepeating(&Browser::DockCancelBounce, browser));
    dock_obj.SetMethod(
        "downloadFinished",
        base::BindRepeating(&Browser::DockDownloadFinished, browser));
    dock_obj.SetMethod(
        "setBadge", base::BindRepeating(&Browser::DockSetBadgeText, browser));
    dock_obj.SetMethod(
        "getBadge", base::BindRepeating(&Browser::DockGetBadgeText, browser));
    dock_obj.SetMethod("hide",
                       base::BindRepeating(&Browser::DockHide, browser));
    dock_obj.SetMethod("show",
                       base::BindRepeating(&Browser::DockShow, browser));
    dock_obj.SetMethod("isVisible",
                       base::BindRepeating(&Browser::DockIsVisible, browser));
    dock_obj.SetMethod("setMenu", &DockSetMenu);
    dock_obj.SetMethod("setIcon",
                       base::BindRepeating(&Browser::DockSetIcon, browser));

    dock_.Reset(isolate, dock_obj.GetHandle());
  }
  return v8::Local<v8::Value>::New(isolate, dock_);
}
#endif

void ConfigureHostResolver(v8::Isolate* isolate,
                           const gin_helper::Dictionary& opts) {
  gin_helper::ErrorThrower thrower(isolate);
  if (!Browser::Get()->is_ready()) {
    thrower.ThrowError(
        "configureHostResolver cannot be called before the app is ready");
    return;
  }
  net::SecureDnsMode secure_dns_mode = net::SecureDnsMode::kOff;
  std::string default_doh_templates;
  net::DnsOverHttpsConfig doh_config;
  if (!default_doh_templates.empty() &&
      secure_dns_mode != net::SecureDnsMode::kOff) {
    auto maybe_doh_config =
        net::DnsOverHttpsConfig::FromString(default_doh_templates);
    if (maybe_doh_config.has_value())
      doh_config = maybe_doh_config.value();
  }

  bool enable_built_in_resolver =
      base::FeatureList::IsEnabled(net::features::kAsyncDns);
  bool additional_dns_query_types_enabled = true;

  if (opts.Has("enableBuiltInResolver") &&
      !opts.Get("enableBuiltInResolver", &enable_built_in_resolver)) {
    thrower.ThrowTypeError("enableBuiltInResolver must be a boolean");
    return;
  }

  if (opts.Has("secureDnsMode") &&
      !opts.Get("secureDnsMode", &secure_dns_mode)) {
    thrower.ThrowTypeError(
        "secureDnsMode must be one of: off, automatic, secure");
    return;
  }

  std::vector<std::string> secure_dns_server_strings;
  if (opts.Has("secureDnsServers")) {
    if (!opts.Get("secureDnsServers", &secure_dns_server_strings)) {
      thrower.ThrowTypeError("secureDnsServers must be an array of strings");
      return;
    }

    // Validate individual server templates prior to batch-assigning to
    // doh_config.
    std::vector<net::DnsOverHttpsServerConfig> servers;
    for (const std::string& server_template : secure_dns_server_strings) {
      std::optional<net::DnsOverHttpsServerConfig> server_config =
          net::DnsOverHttpsServerConfig::FromString(server_template);
      if (!server_config.has_value()) {
        thrower.ThrowTypeError(std::string("not a valid DoH template: ") +
                               server_template);
        return;
      }
      servers.push_back(*server_config);
    }
    doh_config = net::DnsOverHttpsConfig(std::move(servers));
  }

  if (opts.Has("enableAdditionalDnsQueryTypes") &&
      !opts.Get("enableAdditionalDnsQueryTypes",
                &additional_dns_query_types_enabled)) {
    thrower.ThrowTypeError("enableAdditionalDnsQueryTypes must be a boolean");
    return;
  }

  // Configure the stub resolver. This must be done after the system
  // NetworkContext is created, but before anything has the chance to use it.
  content::GetNetworkService()->ConfigureStubHostResolver(
      enable_built_in_resolver, secure_dns_mode, doh_config,
      additional_dns_query_types_enabled);
}

// static
App* App::Get() {
  static base::NoDestructor<App> app;
  return app.get();
}

// static
gin::Handle<App> App::Create(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, Get());
}

gin::ObjectTemplateBuilder App::GetObjectTemplateBuilder(v8::Isolate* isolate) {
  auto browser = base::Unretained(Browser::Get());
  return gin_helper::EventEmitterMixin<App>::GetObjectTemplateBuilder(isolate)
      .SetMethod("quit", base::BindRepeating(&Browser::Quit, browser))
      .SetMethod("exit", base::BindRepeating(&Browser::Exit, browser))
      .SetMethod("focus", base::BindRepeating(&Browser::Focus, browser))
      .SetMethod("getVersion",
                 base::BindRepeating(&Browser::GetVersion, browser))
      .SetMethod("setVersion",
                 base::BindRepeating(&Browser::SetVersion, browser))
      .SetMethod("getName", base::BindRepeating(&Browser::GetName, browser))
      .SetMethod("setName", base::BindRepeating(&Browser::SetName, browser))
      .SetMethod("isReady", base::BindRepeating(&Browser::is_ready, browser))
      .SetMethod("whenReady", base::BindRepeating(&Browser::WhenReady, browser))
      .SetMethod("addRecentDocument",
                 base::BindRepeating(&Browser::AddRecentDocument, browser))
      .SetMethod("clearRecentDocuments",
                 base::BindRepeating(&Browser::ClearRecentDocuments, browser))
#if BUILDFLAG(IS_WIN)
      .SetMethod("setAppUserModelId",
                 base::BindRepeating(&Browser::SetAppUserModelID, browser))
#endif
      .SetMethod(
          "isDefaultProtocolClient",
          base::BindRepeating(&Browser::IsDefaultProtocolClient, browser))
      .SetMethod(
          "setAsDefaultProtocolClient",
          base::BindRepeating(&Browser::SetAsDefaultProtocolClient, browser))
      .SetMethod(
          "removeAsDefaultProtocolClient",
          base::BindRepeating(&Browser::RemoveAsDefaultProtocolClient, browser))
#if !BUILDFLAG(IS_LINUX)
      .SetMethod(
          "getApplicationInfoForProtocol",
          base::BindRepeating(&Browser::GetApplicationInfoForProtocol, browser))
#endif
      .SetMethod(
          "getApplicationNameForProtocol",
          base::BindRepeating(&Browser::GetApplicationNameForProtocol, browser))
      .SetMethod("setBadgeCount",
                 base::BindRepeating(&Browser::SetBadgeCount, browser))
      .SetMethod("getBadgeCount",
                 base::BindRepeating(&Browser::badge_count, browser))
      .SetMethod("getLoginItemSettings", &App::GetLoginItemSettings)
      .SetMethod("setLoginItemSettings",
                 base::BindRepeating(&Browser::SetLoginItemSettings, browser))
      .SetMethod("isEmojiPanelSupported",
                 base::BindRepeating(&Browser::IsEmojiPanelSupported, browser))
#if BUILDFLAG(IS_MAC)
      .SetMethod("hide", base::BindRepeating(&Browser::Hide, browser))
      .SetMethod("isHidden", base::BindRepeating(&Browser::IsHidden, browser))
      .SetMethod("show", base::BindRepeating(&Browser::Show, browser))
      .SetMethod("setUserActivity",
                 base::BindRepeating(&Browser::SetUserActivity, browser))
      .SetMethod("getCurrentActivityType",
                 base::BindRepeating(&Browser::GetCurrentActivityType, browser))
      .SetMethod(
          "invalidateCurrentActivity",
          base::BindRepeating(&Browser::InvalidateCurrentActivity, browser))
      .SetMethod("resignCurrentActivity",
                 base::BindRepeating(&Browser::ResignCurrentActivity, browser))
      .SetMethod("updateCurrentActivity",
                 base::BindRepeating(&Browser::UpdateCurrentActivity, browser))
      .SetMethod("moveToApplicationsFolder", &App::MoveToApplicationsFolder)
      .SetMethod("isInApplicationsFolder", &App::IsInApplicationsFolder)
      .SetMethod("setActivationPolicy", &App::SetActivationPolicy)
#endif
      .SetMethod("setAboutPanelOptions",
                 base::BindRepeating(&Browser::SetAboutPanelOptions, browser))
      .SetMethod("showAboutPanel",
                 base::BindRepeating(&Browser::ShowAboutPanel, browser))
#if BUILDFLAG(IS_MAC)
      .SetMethod(
          "isSecureKeyboardEntryEnabled",
          base::BindRepeating(&Browser::IsSecureKeyboardEntryEnabled, browser))
      .SetMethod(
          "setSecureKeyboardEntryEnabled",
          base::BindRepeating(&Browser::SetSecureKeyboardEntryEnabled, browser))
#endif
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
      .SetMethod("showEmojiPanel",
                 base::BindRepeating(&Browser::ShowEmojiPanel, browser))
#endif
#if BUILDFLAG(IS_WIN)
      .SetMethod("setUserTasks",
                 base::BindRepeating(&Browser::SetUserTasks, browser))
      .SetMethod("getJumpListSettings", &App::GetJumpListSettings)
      .SetMethod("setJumpList", &App::SetJumpList)
#endif
#if BUILDFLAG(IS_LINUX)
      .SetMethod("isUnityRunning",
                 base::BindRepeating(&Browser::IsUnityRunning, browser))
#endif
      .SetProperty("isPackaged", &App::IsPackaged)
      .SetMethod("setAppPath", &App::SetAppPath)
      .SetMethod("getAppPath", &App::GetAppPath)
      .SetMethod("setPath", &App::SetPath)
      .SetMethod("getPath", &App::GetPath)
      .SetMethod("setAppLogsPath", &App::SetAppLogsPath)
      .SetMethod("setDesktopName", &App::SetDesktopName)
      .SetMethod("getLocale", &App::GetLocale)
      .SetMethod("getPreferredSystemLanguages", &GetPreferredLanguages)
      .SetMethod("getSystemLocale", &App::GetSystemLocale)
      .SetMethod("getLocaleCountryCode", &App::GetLocaleCountryCode)
#if BUILDFLAG(USE_NSS_CERTS)
      .SetMethod("importCertificate", &App::ImportCertificate)
#endif
      .SetMethod("hasSingleInstanceLock", &App::HasSingleInstanceLock)
      .SetMethod("requestSingleInstanceLock", &App::RequestSingleInstanceLock)
      .SetMethod("releaseSingleInstanceLock", &App::ReleaseSingleInstanceLock)
      .SetMethod("relaunch", &App::Relaunch)
      .SetMethod("isAccessibilitySupportEnabled",
                 &App::IsAccessibilitySupportEnabled)
      .SetMethod("setAccessibilitySupportEnabled",
                 &App::SetAccessibilitySupportEnabled)
      .SetMethod("disableHardwareAcceleration",
                 &App::DisableHardwareAcceleration)
      .SetMethod("disableDomainBlockingFor3DAPIs",
                 &App::DisableDomainBlockingFor3DAPIs)
      .SetMethod("getFileIcon", &App::GetFileIcon)
      .SetMethod("getAppMetrics", &App::GetAppMetrics)
      .SetMethod("getGPUFeatureStatus", &App::GetGPUFeatureStatus)
      .SetMethod("getGPUInfo", &App::GetGPUInfo)
#if IS_MAS_BUILD()
      .SetMethod("startAccessingSecurityScopedResource",
                 &App::StartAccessingSecurityScopedResource)
#endif
#if BUILDFLAG(IS_MAC)
      .SetProperty("dock", &App::GetDockAPI)
#endif
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
      .SetProperty("runningUnderARM64Translation",
                   &App::IsRunningUnderARM64Translation)
#endif
      .SetProperty("userAgentFallback", &App::GetUserAgentFallback,
                   &App::SetUserAgentFallback)
      .SetMethod("configureHostResolver", &ConfigureHostResolver)
      .SetMethod("enableSandbox", &App::EnableSandbox)
      .SetMethod("setProxy", &App::SetProxy)
      .SetMethod("resolveProxy", &App::ResolveProxy);
}

const char* App::GetTypeName() {
  return "App";
}

}  // namespace electron::api

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("app", electron::api::App::Create(isolate));
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_app, Initialize)
