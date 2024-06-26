// Copyright (c) 2022 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/plugin_info.h"

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "base/strings/utf_string_conversions.h"
#include "components/pdf/common/constants.h"
#include "extensions/common/constants.h"
#include "shell/common/electron_constants.h"
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

namespace electron {

void GetInternalPlugins(std::vector<content::WebPluginInfo>* plugins) {
#if BUILDFLAG(ENABLE_PDF_VIEWER)
  // NB. in Chrome, this plugin isn't registered until the PDF extension is
  // loaded. However, in Electron, we load the PDF extension unconditionally
  // when it is enabled in the build, so we're OK to load the plugin eagerly
  // here.
  plugins->push_back(GetPDFPluginInfo());
#endif
}

#if BUILDFLAG(ENABLE_PDF_VIEWER)
content::WebPluginInfo GetPDFPluginInfo() {
  content::WebPluginInfo info;
  info.type = content::WebPluginInfo::PLUGIN_TYPE_BROWSER_PLUGIN;
  info.name = base::ASCIIToUTF16(kPDFExtensionPluginName);
  // This isn't a real file path; it's just used as a unique identifier.
  info.path = base::FilePath::FromUTF8Unsafe(extension_misc::kPdfExtensionId);
  info.background_color = content::WebPluginInfo::kDefaultBackgroundColor;
  info.mime_types.emplace_back(pdf::kPDFMimeType, "pdf",
                               "Portable Document Format");
  return info;
}
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

}  // namespace electron
