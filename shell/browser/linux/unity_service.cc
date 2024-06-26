// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/linux/unity_service.h"

#include <dlfcn.h>
#include <gtk/gtk.h>

#include <string>

#include "base/environment.h"
#include "base/nix/xdg_util.h"

// Unity data typedefs.
typedef struct _UnityInspector UnityInspector;
typedef UnityInspector* (*unity_inspector_get_default_func)();
typedef gboolean (*unity_inspector_get_unity_running_func)(
    UnityInspector* self);

typedef struct _UnityLauncherEntry UnityLauncherEntry;
typedef UnityLauncherEntry* (*unity_launcher_entry_get_for_desktop_id_func)(
    const gchar* desktop_id);
typedef void (*unity_launcher_entry_set_count_func)(UnityLauncherEntry* self,
                                                    gint64 value);
typedef void (*unity_launcher_entry_set_count_visible_func)(
    UnityLauncherEntry* self,
    gboolean value);
typedef void (*unity_launcher_entry_set_progress_func)(UnityLauncherEntry* self,
                                                       gdouble value);
typedef void (*unity_launcher_entry_set_progress_visible_func)(
    UnityLauncherEntry* self,
    gboolean value);

namespace {

bool attempted_load = false;

// Unity has a singleton object that we can ask whether the unity is running.
UnityInspector* inspector = nullptr;

// A link to the desktop entry in the panel.
UnityLauncherEntry* chrome_entry = nullptr;

// Retrieved functions from libunity.
unity_inspector_get_unity_running_func get_unity_running = nullptr;
unity_launcher_entry_set_count_func entry_set_count = nullptr;
unity_launcher_entry_set_count_visible_func entry_set_count_visible = nullptr;
unity_launcher_entry_set_progress_func entry_set_progress = nullptr;
unity_launcher_entry_set_progress_visible_func entry_set_progress_visible =
    nullptr;

void EnsureLibUnityLoaded() {
  if (attempted_load)
    return;
  attempted_load = true;

  // Ubuntu still hasn't given us a nice libunity.so symlink.
  void* unity_lib = dlopen("libunity.so.4", RTLD_LAZY);
  if (!unity_lib)
    unity_lib = dlopen("libunity.so.6", RTLD_LAZY);
  if (!unity_lib)
    unity_lib = dlopen("libunity.so.9", RTLD_LAZY);
  if (!unity_lib)
    return;

  unity_inspector_get_default_func inspector_get_default =
      reinterpret_cast<unity_inspector_get_default_func>(
          dlsym(unity_lib, "unity_inspector_get_default"));
  if (inspector_get_default) {
    inspector = inspector_get_default();

    get_unity_running =
        reinterpret_cast<unity_inspector_get_unity_running_func>(
            dlsym(unity_lib, "unity_inspector_get_unity_running"));
  }

  unity_launcher_entry_get_for_desktop_id_func entry_get_for_desktop_id =
      reinterpret_cast<unity_launcher_entry_get_for_desktop_id_func>(
          dlsym(unity_lib, "unity_launcher_entry_get_for_desktop_id"));
  if (entry_get_for_desktop_id) {
    std::string desktop_id = getenv("CHROME_DESKTOP");
    chrome_entry = entry_get_for_desktop_id(desktop_id.c_str());

    entry_set_count = reinterpret_cast<unity_launcher_entry_set_count_func>(
        dlsym(unity_lib, "unity_launcher_entry_set_count"));

    entry_set_count_visible =
        reinterpret_cast<unity_launcher_entry_set_count_visible_func>(
            dlsym(unity_lib, "unity_launcher_entry_set_count_visible"));

    entry_set_progress =
        reinterpret_cast<unity_launcher_entry_set_progress_func>(
            dlsym(unity_lib, "unity_launcher_entry_set_progress"));

    entry_set_progress_visible =
        reinterpret_cast<unity_launcher_entry_set_progress_visible_func>(
            dlsym(unity_lib, "unity_launcher_entry_set_progress_visible"));
  }
}

}  // namespace

namespace unity {

bool IsRunning() {
  EnsureLibUnityLoaded();
  if (inspector && get_unity_running)
    return get_unity_running(inspector);

  return false;
}

void SetDownloadCount(int count) {
  EnsureLibUnityLoaded();
  if (chrome_entry && entry_set_count && entry_set_count_visible) {
    entry_set_count(chrome_entry, count);
    entry_set_count_visible(chrome_entry, count != 0);
  }
}

void SetProgressFraction(float percentage) {
  EnsureLibUnityLoaded();
  if (chrome_entry && entry_set_progress && entry_set_progress_visible) {
    entry_set_progress(chrome_entry, percentage);
    entry_set_progress_visible(chrome_entry,
                               percentage > 0.0 && percentage < 1.0);
  }
}

}  // namespace unity
