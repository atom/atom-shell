// Copyright (c) 2022 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_browser_main_parts.h"

#include <string_view>

#include "base/command_line.h"
#include "base/environment.h"
#include "ui/base/ozone_buildflags.h"
#include "ui/ozone/public/ozone_switches.h"

#if BUILDFLAG(IS_OZONE_WAYLAND)
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/nix/xdg_util.h"
#include "shell/common/thread_restrictions.h"
#endif

namespace electron {

namespace {

constexpr std::string_view kElectronOzonePlatformHint{
    "ELECTRON_OZONE_PLATFORM_HINT"};

#if BUILDFLAG(IS_OZONE_WAYLAND)

constexpr char kPlatformWayland[] = "wayland";

bool HasWaylandDisplay(base::Environment* env) {
  std::string wayland_display;
  const bool has_wayland_display =
      env->GetVar("WAYLAND_DISPLAY", &wayland_display) &&
      !wayland_display.empty();
  if (has_wayland_display)
    return true;

  std::string xdg_runtime_dir;
  const bool has_xdg_runtime_dir =
      env->GetVar("XDG_RUNTIME_DIR", &xdg_runtime_dir) &&
      !xdg_runtime_dir.empty();
  if (has_xdg_runtime_dir) {
    auto wayland_server_pipe =
        base::FilePath(xdg_runtime_dir).Append("wayland-0");
    // Normally, this should happen exactly once, at the startup of the main
    // process.
    electron::ScopedAllowBlockingForElectron allow_blocking;
    return base::PathExists(wayland_server_pipe);
  }

  return false;
}

#endif  // BUILDFLAG(IS_OZONE_WAYLAND)

#if BUILDFLAG(IS_OZONE_X11)
constexpr char kPlatformX11[] = "x11";
#endif

// Evaluates the environment and returns the effective platform name for the
// given |ozone_platform_hint|.
// For the "auto" value, returns "wayland" if the XDG session type is "wayland",
// "x11" otherwise.
// For the "wayland" value, checks if the Wayland server is available, and
// returns "x11" if it is not.
// See https://crbug.com/1246928.
std::string MaybeFixPlatformName(const std::string& ozone_platform_hint) {
#if BUILDFLAG(IS_OZONE_WAYLAND)
  // Wayland is selected if both conditions below are true:
  // 1. The user selected either 'wayland' or 'auto'.
  // 2. The XDG session type is 'wayland', OR the user has selected 'wayland'
  //    explicitly and a Wayland server is running.
  // Otherwise, fall back to X11.
  if (ozone_platform_hint == kPlatformWayland ||
      ozone_platform_hint == "auto") {
    auto env(base::Environment::Create());

    std::string xdg_session_type;
    const bool has_xdg_session_type =
        env->GetVar(base::nix::kXdgSessionTypeEnvVar, &xdg_session_type) &&
        !xdg_session_type.empty();

    if ((has_xdg_session_type && xdg_session_type == "wayland") ||
        (ozone_platform_hint == kPlatformWayland &&
         HasWaylandDisplay(env.get()))) {
      return kPlatformWayland;
    }
  }
#endif  // BUILDFLAG(IS_OZONE_WAYLAND)

#if BUILDFLAG(IS_OZONE_X11)
  if (ozone_platform_hint == kPlatformX11) {
    return kPlatformX11;
  }
#if BUILDFLAG(IS_OZONE_WAYLAND)
  if (ozone_platform_hint == kPlatformWayland ||
      ozone_platform_hint == "auto") {
    // We are here if:
    // - The binary has both X11 and Wayland backends.
    // - The user wanted Wayland but that did not work, otherwise it would have
    //   been returned above.
    if (ozone_platform_hint == kPlatformWayland) {
      LOG(WARNING) << "No Wayland server is available. Falling back to X11.";
    } else {
      LOG(WARNING) << "This is not a Wayland session.  Falling back to X11. "
                      "If you need to run Chrome on Wayland using some "
                      "embedded compositor, e. g., Weston, please specify "
                      "Wayland as your preferred Ozone platform, or use "
                      "--ozone-platform=wayland.";
    }
    return kPlatformX11;
  }
#endif  // BUILDFLAG(IS_OZONE_WAYLAND)
#endif  // BUILDFLAG(OZONE_PLATFORM_X11)

  return ozone_platform_hint;
}

}  // namespace

void ElectronBrowserMainParts::DetectOzonePlatform() {
  auto const env = base::Environment::Create();
  auto* const command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(switches::kOzonePlatform)) {
    auto ozone_platform_hint =
        command_line->GetSwitchValueASCII(switches::kOzonePlatformHint);
    if (ozone_platform_hint.empty()) {
      env->GetVar(kElectronOzonePlatformHint, &ozone_platform_hint);
    }
    if (!ozone_platform_hint.empty()) {
      command_line->AppendSwitchASCII(
          switches::kOzonePlatform, MaybeFixPlatformName(ozone_platform_hint));
    }
  }

  std::string desktop_startup_id;
  if (env->GetVar("DESKTOP_STARTUP_ID", &desktop_startup_id))
    command_line->AppendSwitchASCII("desktop-startup-id", desktop_startup_id);
}

}  // namespace electron
