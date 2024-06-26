// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_POWER_MONITOR_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_POWER_MONITOR_H_

#include "base/power_monitor/power_observer.h"
#include "gin/wrappable.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/common/gin_helper/pinnable.h"
#include "ui/base/idle/idle.h"

#if BUILDFLAG(IS_LINUX)
#include "shell/browser/lib/power_observer_linux.h"
#endif

namespace electron::api {

class PowerMonitor : public gin::Wrappable<PowerMonitor>,
                     public gin_helper::EventEmitterMixin<PowerMonitor>,
                     public gin_helper::Pinnable<PowerMonitor>,
                     private base::PowerStateObserver,
                     private base::PowerSuspendObserver,
                     private base::PowerThermalObserver {
 public:
  static v8::Local<v8::Value> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  PowerMonitor(const PowerMonitor&) = delete;
  PowerMonitor& operator=(const PowerMonitor&) = delete;

 private:
  explicit PowerMonitor(v8::Isolate* isolate);
  ~PowerMonitor() override;

#if BUILDFLAG(IS_LINUX)
  void SetListeningForShutdown(bool);
#endif

  // Called by native calles.
  bool ShouldShutdown();

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  void InitPlatformSpecificMonitors();
#endif

  // base::PowerStateObserver implementations:
  void OnPowerStateChange(bool on_battery_power) override;

  // base::PowerSuspendObserver implementations:
  void OnSuspend() override;
  void OnResume() override;

  // base::PowerThermalObserver
  void OnThermalStateChange(DeviceThermalState new_state) override;
  void OnSpeedLimitChange(int speed_limit) override;

#if BUILDFLAG(IS_WIN)
  // Static callback invoked when a message comes in to our messaging window.
  static LRESULT CALLBACK WndProcStatic(HWND hwnd,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam);

  LRESULT CALLBACK WndProc(HWND hwnd,
                           UINT message,
                           WPARAM wparam,
                           LPARAM lparam);

  // The window class of |window_|.
  ATOM atom_;

  // The handle of the module that contains the window procedure of |window_|.
  HMODULE instance_;

  // The window used for processing events.
  HWND window_;
#endif

#if BUILDFLAG(IS_LINUX)
  PowerObserverLinux power_observer_linux_{this};
#endif
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_POWER_MONITOR_H_
