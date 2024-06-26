// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "electron/shell/browser/extensions/api/streams_private/streams_private_api.h"

#include <memory>
#include <utility>

#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "electron/buildflags/buildflags.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_stream_manager.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest.h"
#include "extensions/common/manifest_handlers/mime_types_handler.h"
#include "shell/browser/api/electron_api_web_contents.h"

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "base/feature_list.h"
#include "chrome/browser/pdf/pdf_viewer_stream_manager.h"
#include "extensions/common/constants.h"
#include "pdf/pdf_features.h"
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

namespace extensions {

void StreamsPrivateAPI::SendExecuteMimeTypeHandlerEvent(
    const std::string& extension_id,
    const std::string& stream_id,
    bool embedded,
    int frame_tree_node_id,
    blink::mojom::TransferrableURLLoaderPtr transferrable_loader,
    const GURL& original_url,
    const std::string& internal_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  content::WebContents* web_contents =
      content::WebContents::FromFrameTreeNodeId(frame_tree_node_id);
  if (!web_contents)
    return;

  auto* browser_context = web_contents->GetBrowserContext();

  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(browser_context)
          ->enabled_extensions()
          .GetByID(extension_id);
  if (!extension)
    return;

  MimeTypesHandler* handler = MimeTypesHandler::GetHandler(extension);
  if (!handler->HasPlugin())
    return;

  // If the mime handler uses MimeHandlerViewGuest, the MimeHandlerViewGuest
  // will take ownership of the stream.
  GURL handler_url(
      extensions::Extension::GetBaseURLFromExtensionId(extension_id).spec() +
      handler->handler_url());

  int tab_id = -1;
  auto* api_contents = electron::api::WebContents::From(web_contents);
  if (api_contents)
    tab_id = api_contents->ID();

  auto stream_container = std::make_unique<extensions::StreamContainer>(
      tab_id, embedded, handler_url, extension_id,
      std::move(transferrable_loader), original_url);

#if BUILDFLAG(ENABLE_PDF_VIEWER)
  if (base::FeatureList::IsEnabled(chrome_pdf::features::kPdfOopif) &&
      extension_id == extension_misc::kPdfExtensionId) {
    pdf::PdfViewerStreamManager::Create(web_contents);
    pdf::PdfViewerStreamManager::FromWebContents(web_contents)
        ->AddStreamContainer(frame_tree_node_id, internal_id,
                             std::move(stream_container));
    return;
  }
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

  extensions::MimeHandlerStreamManager::Get(browser_context)
      ->AddStream(stream_id, std::move(stream_container), frame_tree_node_id);
}

}  // namespace extensions
