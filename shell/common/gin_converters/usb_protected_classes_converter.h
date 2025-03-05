// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_GIN_CONVERTERS_USB_PROTECTED_CLASSES_CONVERTER_H_
#define ELECTRON_SHELL_COMMON_GIN_CONVERTERS_USB_PROTECTED_CLASSES_CONVERTER_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gin/converter.h"
#include "services/device/public/mojom/usb_device.mojom-forward.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/common/gin_converters/std_converter.h"
#include "third_party/abseil-cpp/absl/container/inlined_vector.h"

namespace gin {

static auto constexpr ClassMapping =
    std::array<std::pair<uint8_t, std::string_view>, 7>{
        {{device::mojom::kUsbAudioClass, "audio"},
         {device::mojom::kUsbHidClass, "hid"},
         {device::mojom::kUsbMassStorageClass, "mass-storage"},
         {device::mojom::kUsbSmartCardClass, "smart-card"},
         {device::mojom::kUsbVideoClass, "video"},
         {device::mojom::kUsbAudioVideoClass, "audio-video"},
         {device::mojom::kUsbWirelessClass, "wireless"}}};

template <>
struct Converter<electron::ElectronPermissionManager::USBProtectedClasses> {
  static v8::Local<v8::Value> ToV8(
      v8::Isolate* isolate,
      const electron::ElectronPermissionManager::USBProtectedClasses& classes) {
    absl::InlinedVector<std::string_view, ClassMapping.size()> class_strings;
    for (const auto& itr : classes) {
      for (const auto& [usb_class, name] : ClassMapping) {
        if (usb_class == itr)
          class_strings.emplace_back(name);
      }
    }
    return gin::ConvertToV8(isolate, std::span(class_strings));
  }

  static bool FromV8(
      v8::Isolate* isolate,
      v8::Local<v8::Value> val,
      electron::ElectronPermissionManager::USBProtectedClasses* out) {
    std::vector<std::string> class_strings;
    if (ConvertFromV8(isolate, val, &class_strings)) {
      out->reserve(std::size(class_strings));
      for (const auto& itr : class_strings) {
        for (const auto& [usb_class, name] : ClassMapping) {
          if (name == itr)
            out->emplace_back(usb_class);
        }
      }
      return true;
    }
    return false;
  }
};

}  // namespace gin

#endif  // ELECTRON_SHELL_COMMON_GIN_CONVERTERS_USB_PROTECTED_CLASSES_CONVERTER_H_
