// Copyright (c) 2024 Microsoft, GmbH.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_PRINTING_PRINTING_UTILS_H_
#define ELECTRON_SHELL_BROWSER_PRINTING_PRINTING_UTILS_H_

#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/task/task_runner.h"

namespace gfx {
class Size;
}

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace electron {

// This function returns the per-platform default printer's DPI.
gfx::Size GetDefaultPrinterDPI(const std::u16string& device_name);

// This will return false if no printer with the provided device_name can be
// found on the network. We need to check this because Chromium does not do
// sanity checking of device_name validity and so will crash on invalid names.
bool IsDeviceNameValid(const std::u16string& device_name);

content::RenderFrameHost* GetRenderFrameHostToUse(
    content::WebContents* contents);

// This function returns a validated device name.
// If the user passed one to webContents.print(), we check that it's valid and
// return it or fail if the network doesn't recognize it. If the user didn't
// pass a device name, we first try to return the system default printer. If one
// isn't set, then pull all the printers and use the first one or fail if none
// exist.
std::pair<std::string, std::u16string> GetDeviceNameToUse(
    const std::u16string& device_name);

// This function creates a task runner for use with printing tasks.
scoped_refptr<base::TaskRunner> CreatePrinterHandlerTaskRunner();

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_PRINTING_PRINTING_UTILS_H_
