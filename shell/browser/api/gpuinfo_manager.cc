// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/gpuinfo_manager.h"

#include <utility>

#include "base/memory/singleton.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/browser_thread.h"
#include "gpu/config/gpu_info_collector.h"
#include "shell/browser/api/gpu_info_enumerator.h"
#include "shell/common/gin_converters/value_converter.h"

namespace electron {

GPUInfoManager* GPUInfoManager::GetInstance() {
  return base::Singleton<GPUInfoManager>::get();
}

GPUInfoManager::GPUInfoManager()
    : gpu_data_manager_(content::GpuDataManagerImpl::GetInstance()) {
  gpu_data_manager_->AddObserver(this);
}

GPUInfoManager::~GPUInfoManager() {
  content::GpuDataManagerImpl::GetInstance()->RemoveObserver(this);
}

// Should be posted to the task runner
void GPUInfoManager::ProcessCompleteInfo() {
  base::Value::Dict result = EnumerateGPUInfo(gpu_data_manager_->GetGPUInfo());
  // We have received the complete information, resolve all promises that
  // were waiting for this info.
  for (auto& promise : complete_info_promise_set_) {
    promise.Resolve(base::Value(result.Clone()));
  }
  complete_info_promise_set_.clear();
}

void GPUInfoManager::OnGpuInfoUpdate() {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&GPUInfoManager::ProcessCompleteInfo,
                                base::Unretained(this)));
}

// Should be posted to the task runner
void GPUInfoManager::CompleteInfoFetcher(
    gin_helper::Promise<base::Value> promise) {
  complete_info_promise_set_.emplace_back(std::move(promise));
  gpu_data_manager_->RequestDx12VulkanVideoGpuInfoIfNeeded(
      content::GpuDataManagerImpl::kGpuInfoRequestAll, /* delayed */ false);
}

void GPUInfoManager::FetchCompleteInfo(
    gin_helper::Promise<base::Value> promise) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&GPUInfoManager::CompleteInfoFetcher,
                                base::Unretained(this), std::move(promise)));
}

// This fetches the info synchronously, so no need to post to the task queue.
// There cannot be multiple promises as they are resolved synchronously.
void GPUInfoManager::FetchBasicInfo(gin_helper::Promise<base::Value> promise) {
  gpu::GPUInfo gpu_info;
  CollectBasicGraphicsInfo(&gpu_info);
  promise.Resolve(base::Value(EnumerateGPUInfo(gpu_info)));
}

base::Value::Dict GPUInfoManager::EnumerateGPUInfo(
    gpu::GPUInfo gpu_info) const {
  GPUInfoEnumerator enumerator;
  gpu_info.EnumerateFields(&enumerator);
  return enumerator.GetDictionary();
}

}  // namespace electron
