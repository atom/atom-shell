// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_POWER_SAVE_BLOCKER_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_POWER_SAVE_BLOCKER_H_

#include "base/containers/flat_map.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/wake_lock.mojom.h"

namespace electron::api {

class PowerSaveBlocker : public gin::Wrappable<PowerSaveBlocker> {
 public:
  static gin::Handle<PowerSaveBlocker> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  PowerSaveBlocker(const PowerSaveBlocker&) = delete;
  PowerSaveBlocker& operator=(const PowerSaveBlocker&) = delete;

 protected:
  explicit PowerSaveBlocker(v8::Isolate* isolate);
  ~PowerSaveBlocker() override;

 private:
  void UpdatePowerSaveBlocker();
  int Start(device::mojom::WakeLockType type);
  bool Stop(int id);
  bool IsStarted(int id) const;

  device::mojom::WakeLock* GetWakeLock();

  // Current wake lock level.
  device::mojom::WakeLockType current_lock_type_;

  // Whether the wake lock is currently active.
  bool is_wake_lock_active_ = false;

  // Map from id to the corresponding blocker type for each request.
  base::flat_map<int, device::mojom::WakeLockType> wake_lock_types_;

  mojo::Remote<device::mojom::WakeLock> wake_lock_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_POWER_SAVE_BLOCKER_H_
