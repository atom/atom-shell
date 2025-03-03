// Copyright (c) 2025 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/mac/geo_utils.h"

#include "services/device/public/cpp/geolocation/geolocation_system_permission_manager.h"
#include "services/device/public/cpp/geolocation/system_geolocation_source_apple.h"

namespace electron {

void InitializeGeolocation() {
  if (!device::GeolocationSystemPermissionManager::GetInstance()) {
    device::GeolocationSystemPermissionManager::SetInstance(
        device::SystemGeolocationSourceApple::
            CreateGeolocationSystemPermissionManager());
  }
}

}  // namespace electron
