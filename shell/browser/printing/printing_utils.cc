// Copyright (c) 2024 Microsoft, GmbH.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/printing/printing_utils.h"

#include "base/apple/scoped_typeref.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "content/public/browser/browser_thread.h"
#include "electron/buildflags/buildflags.h"
#include "printing/backend/print_backend.h"  // nogncheck
#include "printing/units.h"
#include "shell/common/thread_restrictions.h"

#if BUILDFLAG(IS_MAC)
#include <ApplicationServices/ApplicationServices.h>
#endif

#if BUILDFLAG(IS_LINUX)
#include <gtk/gtk.h>
#endif

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace electron {

gfx::Size GetDefaultPrinterDPI(const std::u16string& device_name) {
#if BUILDFLAG(IS_MAC)
  return gfx::Size(printing::kDefaultMacDpi, printing::kDefaultMacDpi);
#elif BUILDFLAG(IS_WIN)
  HDC hdc =
      CreateDC(L"WINSPOOL", base::as_wcstr(device_name), nullptr, nullptr);
  if (hdc == nullptr)
    return gfx::Size(printing::kDefaultPdfDpi, printing::kDefaultPdfDpi);

  int dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
  int dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
  DeleteDC(hdc);

  return gfx::Size(dpi_x, dpi_y);
#else
  GtkPrintSettings* print_settings = gtk_print_settings_new();
  int dpi = gtk_print_settings_get_resolution(print_settings);
  g_object_unref(print_settings);
  return gfx::Size(dpi, dpi);
#endif
}

bool IsDeviceNameValid(const std::u16string& device_name) {
#if BUILDFLAG(IS_MAC)
  base::apple::ScopedCFTypeRef<CFStringRef> new_printer_id(
      base::SysUTF16ToCFStringRef(device_name));
  PMPrinter new_printer = PMPrinterCreateFromPrinterID(new_printer_id.get());
  bool printer_exists = new_printer != nullptr;
  PMRelease(new_printer);
  return printer_exists;
#else
  scoped_refptr<printing::PrintBackend> print_backend =
      printing::PrintBackend::CreateInstance(
          g_browser_process->GetApplicationLocale());
  return print_backend->IsValidPrinter(base::UTF16ToUTF8(device_name));
#endif
}

std::pair<std::string, std::u16string> GetDeviceNameToUse(
    const std::u16string& device_name) {
#if BUILDFLAG(IS_WIN)
  // Blocking is needed here because Windows printer drivers are oftentimes
  // not thread-safe and have to be accessed on the UI thread.
  ScopedAllowBlockingForElectron allow_blocking;
#endif

  if (!device_name.empty()) {
    if (!IsDeviceNameValid(device_name))
      return std::make_pair("Invalid deviceName provided", std::u16string());
    return std::make_pair(std::string(), device_name);
  }

  scoped_refptr<printing::PrintBackend> print_backend =
      printing::PrintBackend::CreateInstance(
          g_browser_process->GetApplicationLocale());
  std::string printer_name;
  printing::mojom::ResultCode code =
      print_backend->GetDefaultPrinterName(printer_name);

  // We don't want to return if this fails since some devices won't have a
  // default printer.
  if (code != printing::mojom::ResultCode::kSuccess)
    LOG(ERROR) << "Failed to get default printer name";

  if (printer_name.empty()) {
    printing::PrinterList printers;
    if (print_backend->EnumeratePrinters(printers) !=
        printing::mojom::ResultCode::kSuccess)
      return std::make_pair("Failed to enumerate printers", std::u16string());
    if (printers.empty())
      return std::make_pair("No printers available on the network",
                            std::u16string());

    printer_name = printers.front().printer_name;
  }

  return std::make_pair(std::string(), base::UTF8ToUTF16(printer_name));
}

// Copied from
// chrome/browser/ui/webui/print_preview/local_printer_handler_default.cc:L36-L54
scoped_refptr<base::TaskRunner> CreatePrinterHandlerTaskRunner() {
  // USER_VISIBLE because the result is displayed in the print preview dialog.
#if !BUILDFLAG(IS_WIN)
  static constexpr base::TaskTraits kTraits = {
      base::MayBlock(), base::TaskPriority::USER_VISIBLE};
#endif

#if defined(USE_CUPS)
  // CUPS is thread safe.
  return base::ThreadPool::CreateTaskRunner(kTraits);
#elif BUILDFLAG(IS_WIN)
  // Windows drivers are likely not thread-safe and need to be accessed on the
  // UI thread.
  return content::GetUIThreadTaskRunner({base::TaskPriority::USER_VISIBLE});
#else
  // Be conservative on unsupported platforms.
  return base::ThreadPool::CreateSingleThreadTaskRunner(kTraits);
#endif
}

}  // namespace electron
