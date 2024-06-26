// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/extensions/api/resources_private/resources_private_api.h"

#include <string>
#include <utility>

#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/extensions/api/resources_private.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/zoom/page_zoom_constants.h"
#include "electron/buildflags/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "chrome/browser/pdf/pdf_extension_util.h"
#include "pdf/pdf_features.h"
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

// To add a new component to this API, simply:
// 1. Add your component to the Component enum in
//      shell/common/extensions/api/resources_private.idl
// 2. Create an AddStringsForMyComponent(base::Value::Dict* dict) method.
// 3. Tie in that method to the switch statement in Run()

namespace extensions {

namespace get_strings = api::resources_private::GetStrings;

ResourcesPrivateGetStringsFunction::ResourcesPrivateGetStringsFunction() =
    default;

ResourcesPrivateGetStringsFunction::~ResourcesPrivateGetStringsFunction() =
    default;

ExtensionFunction::ResponseAction ResourcesPrivateGetStringsFunction::Run() {
  std::optional<get_strings::Params> params(
      get_strings::Params::Create(args()));
  base::Value::Dict dict;

  api::resources_private::Component component = params->component;

  switch (component) {
    case api::resources_private::Component::kPdf:
#if BUILDFLAG(ENABLE_PDF_VIEWER)
      pdf_extension_util::AddStrings(
          pdf_extension_util::PdfViewerContext::kPdfViewer, &dict);
      pdf_extension_util::AddAdditionalData(true, false, &dict);
#endif
      break;
    case api::resources_private::Component::kIdentity:
      NOTREACHED();
    case api::resources_private::Component::kNone:
      NOTREACHED();
  }

  const std::string& app_locale = g_browser_process->GetApplicationLocale();
  webui::SetLoadTimeDataDefaults(app_locale, &dict);

  return RespondNow(WithArguments(std::move(dict)));
}

}  // namespace extensions
