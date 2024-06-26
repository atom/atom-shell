// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_APP_ELECTRON_MAIN_DELEGATE_H_
#define ELECTRON_SHELL_APP_ELECTRON_MAIN_DELEGATE_H_

#include <memory>
#include <string>

#include "content/public/app/content_main_delegate.h"
#include "content/public/common/content_client.h"

namespace tracing {
class TracingSamplerProfiler;
}

namespace electron {

std::string LoadResourceBundle(const std::string& locale);

class ElectronMainDelegate : public content::ContentMainDelegate {
 public:
  static const char* const kNonWildcardDomainNonPortSchemes[];
  static const size_t kNonWildcardDomainNonPortSchemesSize;
  ElectronMainDelegate();
  ~ElectronMainDelegate() override;

  // disable copy
  ElectronMainDelegate(const ElectronMainDelegate&) = delete;
  ElectronMainDelegate& operator=(const ElectronMainDelegate&) = delete;

 protected:
  // content::ContentMainDelegate:
  base::StringPiece GetBrowserV8SnapshotFilename() override;
  std::optional<int> BasicStartupComplete() override;
  void PreSandboxStartup() override;
  void SandboxInitialized(const std::string& process_type) override;
  std::optional<int> PreBrowserMain() override;
  content::ContentClient* CreateContentClient() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentGpuClient* CreateContentGpuClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;
  content::ContentUtilityClient* CreateContentUtilityClient() override;
  absl::variant<int, content::MainFunctionParams> RunProcess(
      const std::string& process_type,
      content::MainFunctionParams main_function_params) override;
  bool ShouldCreateFeatureList(InvokedIn invoked_in) override;
  bool ShouldInitializeMojo(InvokedIn invoked_in) override;
  bool ShouldLockSchemeRegistry() override;
#if BUILDFLAG(IS_LINUX)
  void ZygoteForked() override;
#endif

 private:
#if BUILDFLAG(IS_MAC)
  void OverrideChildProcessPath();
  void OverrideFrameworkBundlePath();
  void SetUpBundleOverrides();
#endif

  std::unique_ptr<content::ContentBrowserClient> browser_client_;
  std::unique_ptr<content::ContentClient> content_client_;
  std::unique_ptr<content::ContentGpuClient> gpu_client_;
  std::unique_ptr<content::ContentRendererClient> renderer_client_;
  std::unique_ptr<content::ContentUtilityClient> utility_client_;
  std::unique_ptr<tracing::TracingSamplerProfiler> tracing_sampler_profiler_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_APP_ELECTRON_MAIN_DELEGATE_H_
