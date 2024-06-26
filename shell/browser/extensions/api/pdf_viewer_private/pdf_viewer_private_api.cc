// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/extensions/api/pdf_viewer_private/pdf_viewer_private_api.h"

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/pdf/pdf_viewer_stream_manager.h"
#include "chrome/common/extensions/api/pdf_viewer_private.h"
#include "chrome/common/pref_names.h"
#include "components/pdf/common/constants.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest.h"
#include "shell/browser/electron_browser_context.h"
#include "url/url_constants.h"

namespace extensions {

namespace {

namespace IsAllowedLocalFileAccess =
    api::pdf_viewer_private::IsAllowedLocalFileAccess;

namespace SetPdfOcrPref = api::pdf_viewer_private::SetPdfOcrPref;

namespace SetPdfPluginAttributes =
    api::pdf_viewer_private::SetPdfPluginAttributes;

namespace SetPdfDocumentTitle = api::pdf_viewer_private::SetPdfDocumentTitle;

// Check if the current URL is allowed based on a list of allowlisted domains.
bool IsUrlAllowedToEmbedLocalFiles(
    const GURL& current_url,
    const base::Value::List& allowlisted_domains) {
  if (!current_url.is_valid() || !current_url.SchemeIs(url::kHttpsScheme)) {
    return false;
  }

  for (auto const& value : allowlisted_domains) {
    const std::string* domain = value.GetIfString();
    if (!domain) {
      continue;
    }

    if (current_url.DomainIs(*domain)) {
      return true;
    }
  }
  return false;
}

// Get the `StreamContainer` associated with the `extension_host`.
base::WeakPtr<StreamContainer> GetStreamContainer(
    content::RenderFrameHost* extension_host) {
  content::RenderFrameHost* embedder_host = extension_host->GetParent();
  if (!embedder_host) {
    return nullptr;
  }

  auto* pdf_viewer_stream_manager =
      pdf::PdfViewerStreamManager::FromRenderFrameHost(embedder_host);
  if (!pdf_viewer_stream_manager) {
    return nullptr;
  }

  return pdf_viewer_stream_manager->GetStreamContainer(embedder_host);
}

}  // namespace

PdfViewerPrivateGetStreamInfoFunction::PdfViewerPrivateGetStreamInfoFunction() =
    default;

PdfViewerPrivateGetStreamInfoFunction::
    ~PdfViewerPrivateGetStreamInfoFunction() = default;

ExtensionFunction::ResponseAction PdfViewerPrivateGetStreamInfoFunction::Run() {
  base::WeakPtr<StreamContainer> stream =
      GetStreamContainer(render_frame_host());
  if (!stream) {
    return RespondNow(Error("Failed to get StreamContainer"));
  }

  api::pdf_viewer_private::StreamInfo stream_info;
  stream_info.original_url = stream->original_url().spec();
  stream_info.stream_url = stream->stream_url().spec();
  stream_info.tab_id = stream->tab_id();
  stream_info.embedded = stream->embedded();
  return RespondNow(WithArguments(stream_info.ToValue()));
}

PdfViewerPrivateIsAllowedLocalFileAccessFunction::
    PdfViewerPrivateIsAllowedLocalFileAccessFunction() = default;

PdfViewerPrivateIsAllowedLocalFileAccessFunction::
    ~PdfViewerPrivateIsAllowedLocalFileAccessFunction() = default;

ExtensionFunction::ResponseAction
PdfViewerPrivateIsAllowedLocalFileAccessFunction::Run() {
  std::optional<IsAllowedLocalFileAccess::Params> params =
      IsAllowedLocalFileAccess::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  return RespondNow(WithArguments(
      IsUrlAllowedToEmbedLocalFiles(GURL(params->url), base::Value::List())));
}

PdfViewerPrivateSetPdfDocumentTitleFunction::
    PdfViewerPrivateSetPdfDocumentTitleFunction() = default;

PdfViewerPrivateSetPdfDocumentTitleFunction::
    ~PdfViewerPrivateSetPdfDocumentTitleFunction() = default;

// This function is only called for full-page PDFs.
ExtensionFunction::ResponseAction
PdfViewerPrivateSetPdfDocumentTitleFunction::Run() {
  content::WebContents* web_contents = GetSenderWebContents();
  if (!web_contents) {
    return RespondNow(Error("Could not find a valid web contents."));
  }

  // Title should only be set for full-page PDFs.
  // MIME type associated with sender `WebContents` must be `application/pdf`
  // for a full-page PDF.
  EXTENSION_FUNCTION_VALIDATE(web_contents->GetContentsMimeType() ==
                              pdf::kPDFMimeType);

  std::optional<SetPdfDocumentTitle::Params> params =
      SetPdfDocumentTitle::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  web_contents->UpdateTitleForEntry(
      web_contents->GetController().GetLastCommittedEntry(),
      base::UTF8ToUTF16(params->title));

  return RespondNow(NoArguments());
}

PdfViewerPrivateIsPdfOcrAlwaysActiveFunction::
    PdfViewerPrivateIsPdfOcrAlwaysActiveFunction() = default;

PdfViewerPrivateIsPdfOcrAlwaysActiveFunction::
    ~PdfViewerPrivateIsPdfOcrAlwaysActiveFunction() = default;

// TODO(codebytere): enable when https://crbug.com/1393069 works properly.
ExtensionFunction::ResponseAction
PdfViewerPrivateIsPdfOcrAlwaysActiveFunction::Run() {
  return RespondNow(WithArguments(false));
}

PdfViewerPrivateSetPdfOcrPrefFunction::PdfViewerPrivateSetPdfOcrPrefFunction() =
    default;

PdfViewerPrivateSetPdfOcrPrefFunction::
    ~PdfViewerPrivateSetPdfOcrPrefFunction() = default;

// TODO(codebytere): enable when https://crbug.com/1393069 works properly.
ExtensionFunction::ResponseAction PdfViewerPrivateSetPdfOcrPrefFunction::Run() {
  std::optional<SetPdfOcrPref::Params> params =
      SetPdfOcrPref::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  return RespondNow(WithArguments(false));
}

PdfViewerPrivateSetPdfPluginAttributesFunction::
    PdfViewerPrivateSetPdfPluginAttributesFunction() = default;

PdfViewerPrivateSetPdfPluginAttributesFunction::
    ~PdfViewerPrivateSetPdfPluginAttributesFunction() = default;

ExtensionFunction::ResponseAction
PdfViewerPrivateSetPdfPluginAttributesFunction::Run() {
  std::optional<SetPdfPluginAttributes::Params> params =
      SetPdfPluginAttributes::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  base::WeakPtr<StreamContainer> stream =
      GetStreamContainer(render_frame_host());
  if (!stream) {
    return RespondNow(Error("Failed to get StreamContainer"));
  }

  const api::pdf_viewer_private::PdfPluginAttributes& attributes =
      params->attributes;
  // Check the `background_color` is an integer.
  double whole = 0.0;
  if (std::modf(attributes.background_color, &whole) != 0.0) {
    return RespondNow(Error("Background color is not an integer"));
  }

  // Check the `background_color` is within the range of a uint32_t.
  if (!base::IsValueInRangeForNumericType<uint32_t>(
          attributes.background_color)) {
    return RespondNow(Error("Background color out of bounds"));
  }

  stream->set_pdf_plugin_attributes(mime_handler::PdfPluginAttributes::New(
      /*background_color=*/attributes.background_color,
      /*allow_javascript=*/attributes.allow_javascript));
  return RespondNow(NoArguments());
}

}  // namespace extensions
