// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_APP_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_APP_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/process_singleton.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "content/public/browser/gpu_data_manager_observer.h"
#include "content/public/browser/render_process_host.h"
#include "crypto/crypto_buildflags.h"
#include "gin/handle.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/ssl/client_cert_identity.h"
#include "shell/browser/api/process_metric.h"
#include "shell/browser/browser.h"
#include "shell/browser/browser_observer.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/promise.h"

#if BUILDFLAG(USE_NSS_CERTS)
#include "shell/browser/certificate_manager_model.h"
#endif

namespace base {
class FilePath;
}

namespace electron {

#if BUILDFLAG(IS_WIN)
enum class JumpListResult : int;
#endif

namespace api {

class App : public ElectronBrowserClient::Delegate,
            public gin::Wrappable<App>,
            public gin_helper::EventEmitterMixin<App>,
            private BrowserObserver,
            private content::GpuDataManagerObserver,
            private content::BrowserChildProcessObserver {
 public:
  using FileIconCallback =
      base::RepeatingCallback<void(v8::Local<v8::Value>, const gfx::Image&)>;

  static gin::Handle<App> Create(v8::Isolate* isolate);
  static App* Get();

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

#if BUILDFLAG(USE_NSS_CERTS)
  void OnCertificateManagerModelCreated(
      base::Value options,
      net::CompletionOnceCallback callback,
      std::unique_ptr<CertificateManagerModel> model);
#endif

  base::FilePath GetAppPath() const;
  void RenderProcessReady(content::RenderProcessHost* host);
  void RenderProcessExited(content::RenderProcessHost* host);

  static bool IsPackaged();

  App();

  // disable copy
  App(const App&) = delete;
  App& operator=(const App&) = delete;

 private:
  ~App() override;

  // BrowserObserver:
  void OnBeforeQuit(bool* prevent_default) override;
  void OnWillQuit(bool* prevent_default) override;
  void OnWindowAllClosed() override;
  void OnQuit() override;
  void OnOpenFile(bool* prevent_default, const std::string& file_path) override;
  void OnOpenURL(const std::string& url) override;
  void OnActivate(bool has_visible_windows) override;
  void OnWillFinishLaunching() override;
  void OnFinishLaunching(base::Value::Dict launch_info) override;
  void OnAccessibilitySupportChanged() override;
  void OnPreMainMessageLoopRun() override;
  void OnPreCreateThreads() override;
#if BUILDFLAG(IS_MAC)
  void OnWillContinueUserActivity(bool* prevent_default,
                                  const std::string& type) override;
  void OnDidFailToContinueUserActivity(const std::string& type,
                                       const std::string& error) override;
  void OnContinueUserActivity(bool* prevent_default,
                              const std::string& type,
                              base::Value::Dict user_info,
                              base::Value::Dict details) override;
  void OnUserActivityWasContinued(const std::string& type,
                                  base::Value::Dict user_info) override;
  void OnUpdateUserActivityState(bool* prevent_default,
                                 const std::string& type,
                                 base::Value::Dict user_info) override;
  void OnNewWindowForTab() override;
  void OnDidBecomeActive() override;
  void OnDidResignActive() override;
#endif

  // content::ContentBrowserClient:
  void AllowCertificateError(
      content::WebContents* web_contents,
      int cert_error,
      const net::SSLInfo& ssl_info,
      const GURL& request_url,
      bool is_main_frame_request,
      bool strict_enforcement,
      base::OnceCallback<void(content::CertificateRequestResultType)> callback)
      override;
  base::OnceClosure SelectClientCertificate(
      content::BrowserContext* browser_context,
      content::WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      net::ClientCertIdentityList identities,
      std::unique_ptr<content::ClientCertificateDelegate> delegate) override;
  bool CanCreateWindow(content::RenderFrameHost* opener,
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
                       bool* no_javascript_access) override;

  // content::GpuDataManagerObserver:
  void OnGpuInfoUpdate() override;

  // content::BrowserChildProcessObserver:
  void BrowserChildProcessLaunchedAndConnected(
      const content::ChildProcessData& data) override;
  void BrowserChildProcessHostDisconnected(
      const content::ChildProcessData& data) override;
  void BrowserChildProcessCrashed(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override;
  void BrowserChildProcessKilled(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override;

 private:
  void BrowserChildProcessCrashedOrKilled(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info);

  void SetAppPath(const base::FilePath& app_path);
  void ChildProcessLaunched(int process_type,
                            int pid,
                            base::ProcessHandle handle,
                            const std::string& service_name = std::string(),
                            const std::string& name = std::string());
  void ChildProcessDisconnected(int pid);

  void SetAppLogsPath(gin_helper::ErrorThrower thrower,
                      std::optional<base::FilePath> custom_path);

  // Get/Set the pre-defined path in PathService.
  base::FilePath GetPath(gin_helper::ErrorThrower thrower,
                         const std::string& name);
  void SetPath(gin_helper::ErrorThrower thrower,
               const std::string& name,
               const base::FilePath& path);

  void SetDesktopName(const std::string& desktop_name);
  std::string GetLocale();
  std::string GetLocaleCountryCode();
  std::string GetSystemLocale(gin_helper::ErrorThrower thrower) const;
  void OnSecondInstance(base::CommandLine cmd,
                        const base::FilePath& cwd,
                        const std::vector<uint8_t> additional_data);
  bool HasSingleInstanceLock() const;
  bool RequestSingleInstanceLock(gin::Arguments* args);
  void ReleaseSingleInstanceLock();
  bool Relaunch(gin::Arguments* args);
  void DisableHardwareAcceleration(gin_helper::ErrorThrower thrower);
  void DisableDomainBlockingFor3DAPIs(gin_helper::ErrorThrower thrower);
  bool IsAccessibilitySupportEnabled();
  void SetAccessibilitySupportEnabled(gin_helper::ErrorThrower thrower,
                                      bool enabled);
  v8::Local<v8::Value> GetLoginItemSettings(gin::Arguments* args);
#if BUILDFLAG(USE_NSS_CERTS)
  void ImportCertificate(gin_helper::ErrorThrower thrower,
                         base::Value options,
                         net::CompletionOnceCallback callback);
#endif
  v8::Local<v8::Promise> GetFileIcon(const base::FilePath& path,
                                     gin::Arguments* args);

  std::vector<gin_helper::Dictionary> GetAppMetrics(v8::Isolate* isolate);
  v8::Local<v8::Value> GetGPUFeatureStatus(v8::Isolate* isolate);
  v8::Local<v8::Promise> GetGPUInfo(v8::Isolate* isolate,
                                    const std::string& info_type);
  void EnableSandbox(gin_helper::ErrorThrower thrower);
  void SetUserAgentFallback(const std::string& user_agent);
  std::string GetUserAgentFallback();
  v8::Local<v8::Promise> SetProxy(gin::Arguments* args);
  v8::Local<v8::Promise> ResolveProxy(gin::Arguments* args);

#if BUILDFLAG(IS_MAC)
  void SetActivationPolicy(gin_helper::ErrorThrower thrower,
                           const std::string& policy);
  bool MoveToApplicationsFolder(gin_helper::ErrorThrower, gin::Arguments* args);
  bool IsInApplicationsFolder();
  v8::Local<v8::Value> GetDockAPI(v8::Isolate* isolate);
  v8::Global<v8::Value> dock_;
#endif

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  bool IsRunningUnderARM64Translation() const;
#endif

#if IS_MAS_BUILD()
  base::RepeatingCallback<void()> StartAccessingSecurityScopedResource(
      gin::Arguments* args);
#endif

#if BUILDFLAG(IS_WIN)
  // Get the current Jump List settings.
  v8::Local<v8::Value> GetJumpListSettings();

  // Set or remove a custom Jump List for the application.
  JumpListResult SetJumpList(v8::Local<v8::Value> val, gin::Arguments* args);
#endif  // BUILDFLAG(IS_WIN)

  std::unique_ptr<ProcessSingleton> process_singleton_;

#if BUILDFLAG(USE_NSS_CERTS)
  std::unique_ptr<CertificateManagerModel> certificate_manager_model_;
#endif

  // Tracks tasks requesting file icons.
  base::CancelableTaskTracker cancelable_task_tracker_;

  base::FilePath app_path_;

  // pid -> electron::ProcessMetric
  base::flat_map<int, std::unique_ptr<electron::ProcessMetric>> app_metrics_;

  bool disable_hw_acceleration_ = false;
  bool disable_domain_blocking_for_3DAPIs_ = false;
  bool watch_singleton_socket_on_ready_ = false;
};

}  // namespace api

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_APP_H_
