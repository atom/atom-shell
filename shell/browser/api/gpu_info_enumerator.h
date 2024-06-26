// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_GPU_INFO_ENUMERATOR_H_
#define ELECTRON_SHELL_BROWSER_API_GPU_INFO_ENUMERATOR_H_

#include <memory>
#include <stack>
#include <string>

#include "base/values.h"
#include "gpu/config/gpu_info.h"

namespace electron {

// This class implements the enumerator for reading all the attributes in
// GPUInfo into a dictionary.
class GPUInfoEnumerator final : public gpu::GPUInfo::Enumerator {
  const char* const kGPUDeviceKey = "gpuDevice";
  const char* const kVideoDecodeAcceleratorSupportedProfileKey =
      "videoDecodeAcceleratorSupportedProfile";
  const char* const kVideoEncodeAcceleratorSupportedProfileKey =
      "videoEncodeAcceleratorSupportedProfile";
  const char* const kImageDecodeAcceleratorSupportedProfileKey =
      "imageDecodeAcceleratorSupportedProfile";
  const char* const kAuxAttributesKey = "auxAttributes";
  const char* const kOverlayInfo = "overlayInfo";

 public:
  GPUInfoEnumerator();
  ~GPUInfoEnumerator() override;

  // gpu::GPUInfo::Enumerator
  void AddInt64(const char* name, int64_t value) override;
  void AddInt(const char* name, int value) override;
  void AddString(const char* name, const std::string& value) override;
  void AddBool(const char* name, bool value) override;
  void AddTimeDeltaInSecondsF(const char* name,
                              const base::TimeDelta& value) override;
  void AddBinary(const char* name,
                 const base::span<const uint8_t>& value) override;
  void BeginGPUDevice() override;
  void EndGPUDevice() override;
  void BeginVideoDecodeAcceleratorSupportedProfile() override;
  void EndVideoDecodeAcceleratorSupportedProfile() override;
  void BeginVideoEncodeAcceleratorSupportedProfile() override;
  void EndVideoEncodeAcceleratorSupportedProfile() override;
  void BeginImageDecodeAcceleratorSupportedProfile() override;
  void EndImageDecodeAcceleratorSupportedProfile() override;
  void BeginAuxAttributes() override;
  void EndAuxAttributes() override;
  void BeginOverlayInfo() override;
  void EndOverlayInfo() override;
  base::Value::Dict GetDictionary();

 private:
  // The stack is used to manage nested values
  std::stack<base::Value::Dict> value_stack_;
  base::Value::Dict current_;
};

}  // namespace electron
#endif  // ELECTRON_SHELL_BROWSER_API_GPU_INFO_ENUMERATOR_H_
