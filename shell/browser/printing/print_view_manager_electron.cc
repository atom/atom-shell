// Copyright 2020 Microsoft, Inc. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/printing/print_view_manager_electron.h"

#include <utility>

#include "base/containers/contains.h"
#include "base/functional/bind.h"
#include "build/build_config.h"
#include "components/printing/browser/print_to_pdf/pdf_print_utils.h"
#include "printing/mojom/print.mojom.h"
#include "printing/page_range.h"
#include "third_party/abseil-cpp/absl/types/variant.h"

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
#include "mojo/public/cpp/bindings/message.h"
#endif

namespace electron {

namespace {

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
constexpr char kInvalidSetupScriptedPrintPreviewCall[] =
    "Invalid SetupScriptedPrintPreview Call";
constexpr char kInvalidShowScriptedPrintPreviewCall[] =
    "Invalid ShowScriptedPrintPreview Call";
constexpr char kInvalidRequestPrintPreviewCall[] =
    "Invalid RequestPrintPreview Call";
#endif

}  // namespace

// This file subclasses printing::PrintViewManagerBase
// but the implementations are duplicated from
// components/printing/browser/print_to_pdf/pdf_print_manager.cc.

PrintViewManagerElectron::PrintViewManagerElectron(
    content::WebContents* web_contents)
    : printing::PrintViewManagerBase(web_contents),
      content::WebContentsUserData<PrintViewManagerElectron>(*web_contents) {}

PrintViewManagerElectron::~PrintViewManagerElectron() = default;

// static
void PrintViewManagerElectron::BindPrintManagerHost(
    mojo::PendingAssociatedReceiver<printing::mojom::PrintManagerHost> receiver,
    content::RenderFrameHost* rfh) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(rfh);
  if (!web_contents)
    return;

  auto* print_manager = PrintViewManagerElectron::FromWebContents(web_contents);
  if (!print_manager)
    return;

  print_manager->BindReceiver(std::move(receiver), rfh);
}

void PrintViewManagerElectron::DidPrintToPdf(
    int cookie,
    PrintToPdfCallback callback,
    print_to_pdf::PdfPrintResult result,
    scoped_refptr<base::RefCountedMemory> memory) {
  std::erase(pdf_jobs_, cookie);
  std::move(callback).Run(result, memory);
}

void PrintViewManagerElectron::PrintToPdf(
    content::RenderFrameHost* rfh,
    const std::string& page_ranges,
    printing::mojom::PrintPagesParamsPtr print_pages_params,
    PrintToPdfCallback callback) {
  // Store cookie in order to track job uniqueness and differentiate
  // between regular and headless print jobs.
  int cookie = print_pages_params->params->document_cookie;
  pdf_jobs_.emplace_back(cookie);

  print_to_pdf::PdfPrintJob::StartJob(
      web_contents(), rfh, GetPrintRenderFrame(rfh), page_ranges,
      std::move(print_pages_params),
      base::BindOnce(&PrintViewManagerElectron::DidPrintToPdf,
                     weak_factory_.GetWeakPtr(), cookie, std::move(callback)));
}

void PrintViewManagerElectron::GetDefaultPrintSettings(
    GetDefaultPrintSettingsCallback callback) {
  // This isn't ideal, but we're not able to access the document cookie here.
  if (pdf_jobs_.size() > 0) {
    LOG(ERROR) << "Scripted print is not supported";
    std::move(callback).Run(printing::mojom::PrintParams::New());
  } else {
    PrintViewManagerBase::GetDefaultPrintSettings(std::move(callback));
  }
}

void PrintViewManagerElectron::ScriptedPrint(
    printing::mojom::ScriptedPrintParamsPtr params,
    ScriptedPrintCallback callback) {
  if (!base::Contains(pdf_jobs_, params->cookie)) {
    PrintViewManagerBase::ScriptedPrint(std::move(params), std::move(callback));
    return;
  }

  auto default_param = printing::mojom::PrintPagesParams::New();
  default_param->params = printing::mojom::PrintParams::New();
  LOG(ERROR) << "Scripted print is not supported";
  std::move(callback).Run(std::move(default_param));
}

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
void PrintViewManagerElectron::SetupScriptedPrintPreview(
    SetupScriptedPrintPreviewCallback callback) {
  mojo::ReportBadMessage(kInvalidSetupScriptedPrintPreviewCall);
}

void PrintViewManagerElectron::ShowScriptedPrintPreview(
    bool source_is_modifiable) {
  mojo::ReportBadMessage(kInvalidShowScriptedPrintPreviewCall);
}

void PrintViewManagerElectron::RequestPrintPreview(
    printing::mojom::RequestPrintPreviewParamsPtr params) {
  mojo::ReportBadMessage(kInvalidRequestPrintPreviewCall);
}

void PrintViewManagerElectron::CheckForCancel(int32_t preview_ui_id,
                                              int32_t request_id,
                                              CheckForCancelCallback callback) {
  std::move(callback).Run(false);
}
#endif  // BUILDFLAG(ENABLE_PRINT_PREVIEW)

void PrintViewManagerElectron::DidGetPrintedPagesCount(int32_t cookie,
                                                       uint32_t number_pages) {
  if (!base::Contains(pdf_jobs_, cookie)) {
    PrintViewManagerBase::DidGetPrintedPagesCount(cookie, number_pages);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PrintViewManagerElectron);

}  // namespace electron
