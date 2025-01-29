// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_PLATFORM_UTIL_H_
#define ELECTRON_SHELL_COMMON_PLATFORM_UTIL_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "build/build_config.h"

class GURL;

namespace platform_util {

typedef base::OnceCallback<void(const std::string&)> OpenCallback;

// Show the given file in a file manager. If possible, select the file.
// Must be called from the UI thread.
void ShowItemInFolder(const base::FilePath& full_path);

// Open the given file in the desktop's default manner.
// Must be called from the UI thread.
void OpenPath(const base::FilePath& full_path, OpenCallback callback);

struct OpenExternalOptions {
  bool activate = true;
  base::FilePath working_dir;
  bool log_usage = false;
};

// Open the given external protocol URL in the desktop's default manner.
// (For example, mailto: URLs in the default mail user agent.)
void OpenExternal(const GURL& url,
                  const OpenExternalOptions& options,
                  OpenCallback callback);

// Move a file to trash, asynchronously.
void TrashItem(const base::FilePath& full_path,
               base::OnceCallback<void(bool, const std::string&)> callback);

void Beep();

#if BUILDFLAG(IS_WIN)
// SHGetFolderPath calls not covered by Chromium
bool GetFolderPath(int key, base::FilePath* result);

// Check if nul device can be used.
bool IsNulDeviceEnabled();
#endif

#if BUILDFLAG(IS_MAC)
std::string GetLoginItemEnabled(const std::string& type,
                                const std::string& service_name);
bool SetLoginItemEnabled(const std::string& type,
                         const std::string& service_name,
                         bool enabled);
#endif

#if BUILDFLAG(IS_LINUX)
// Returns a success flag.
// Unlike libgtkui, does *not* use "chromium-browser.desktop" as a fallback.
bool GetDesktopName(std::string* setme);

// The XDG application ID must match the name of the desktop entry file without
// the .desktop extension.
std::string GetXdgAppId();
#endif

}  // namespace platform_util

#endif  // ELECTRON_SHELL_COMMON_PLATFORM_UTIL_H_
