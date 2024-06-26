// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_GPUINFO_MANAGER_H_
#define ELECTRON_SHELL_BROWSER_API_GPUINFO_MANAGER_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"  // nogncheck
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/browser/gpu_data_manager_observer.h"
#include "shell/common/gin_helper/promise.h"

namespace electron {

// GPUInfoManager is a singleton used to manage and fetch GPUInfo
class GPUInfoManager : private content::GpuDataManagerObserver {
 public:
  static GPUInfoManager* GetInstance();

  GPUInfoManager();
  ~GPUInfoManager() override;

  // disable copy
  GPUInfoManager(const GPUInfoManager&) = delete;
  GPUInfoManager& operator=(const GPUInfoManager&) = delete;

  void FetchCompleteInfo(gin_helper::Promise<base::Value> promise);
  void FetchBasicInfo(gin_helper::Promise<base::Value> promise);

 private:
  // content::GpuDataManagerObserver
  void OnGpuInfoUpdate() override;

  base::Value::Dict EnumerateGPUInfo(gpu::GPUInfo gpu_info) const;

  // These should be posted to the task queue
  void CompleteInfoFetcher(gin_helper::Promise<base::Value> promise);
  void ProcessCompleteInfo();

  // This set maintains all the promises that should be fulfilled
  // once we have the complete information data
  std::vector<gin_helper::Promise<base::Value>> complete_info_promise_set_;
  raw_ptr<content::GpuDataManagerImpl> gpu_data_manager_;
};

}  // namespace electron
#endif  // ELECTRON_SHELL_BROWSER_API_GPUINFO_MANAGER_H_
