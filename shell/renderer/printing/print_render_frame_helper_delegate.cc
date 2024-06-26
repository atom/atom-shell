// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/printing/print_render_frame_helper_delegate.h"

#include <utility>

#include "content/public/renderer/render_frame.h"
#include "electron/buildflags/buildflags.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "components/pdf/common/pdf_util.h"
#include "extensions/common/constants.h"
#include "extensions/renderer/guest_view/mime_handler_view/post_message_support.h"
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

namespace electron {

PrintRenderFrameHelperDelegate::PrintRenderFrameHelperDelegate() = default;

PrintRenderFrameHelperDelegate::~PrintRenderFrameHelperDelegate() = default;

// Return the PDF object element if |frame| is the out of process PDF extension.
blink::WebElement PrintRenderFrameHelperDelegate::GetPdfElement(
    blink::WebLocalFrame* frame) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  if (frame->Parent() &&
      IsPdfInternalPluginAllowedOrigin(frame->Parent()->GetSecurityOrigin())) {
    auto plugin_element = frame->GetDocument().QuerySelector("embed");
    DCHECK(!plugin_element.IsNull());
    return plugin_element;
  }
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  return blink::WebElement();
}

bool PrintRenderFrameHelperDelegate::IsPrintPreviewEnabled() {
  return false;
}

bool PrintRenderFrameHelperDelegate::OverridePrint(
    blink::WebLocalFrame* frame) {
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  auto* post_message_support =
      extensions::PostMessageSupport::FromWebLocalFrame(frame);
  if (post_message_support) {
    // This message is handled in chrome/browser/resources/pdf/pdf_viewer.js and
    // instructs the PDF plugin to print. This is to make window.print() on a
    // PDF plugin document correctly print the PDF. See
    // https://crbug.com/448720.
    base::Value::Dict message;
    message.Set("type", "print");
    post_message_support->PostMessageFromValue(base::Value(std::move(message)));
    return true;
  }
#endif  // BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  return false;
}

}  // namespace electron
