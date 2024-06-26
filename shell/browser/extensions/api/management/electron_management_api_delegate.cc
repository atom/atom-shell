// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(sentialx): emit relevant events in Electron's session?
#include "shell/browser/extensions/api/management/electron_management_api_delegate.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/extensions/extension_metrics.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/management/management_api.h"
#include "extensions/browser/api/management/management_api_constants.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/api/management.h"
#include "extensions/common/extension.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom.h"

namespace {
class ManagementSetEnabledFunctionInstallPromptDelegate
    : public extensions::InstallPromptDelegate {
 public:
  ManagementSetEnabledFunctionInstallPromptDelegate(
      content::WebContents* web_contents,
      content::BrowserContext* browser_context,
      const extensions::Extension* extension,
      base::OnceCallback<void(bool)> callback) {
    // TODO(sentialx): emit event
  }
  ~ManagementSetEnabledFunctionInstallPromptDelegate() override = default;

  // disable copy
  ManagementSetEnabledFunctionInstallPromptDelegate(
      const ManagementSetEnabledFunctionInstallPromptDelegate&) = delete;
  ManagementSetEnabledFunctionInstallPromptDelegate& operator=(
      const ManagementSetEnabledFunctionInstallPromptDelegate&) = delete;

 private:
  base::WeakPtrFactory<ManagementSetEnabledFunctionInstallPromptDelegate>
      weak_factory_{this};
};

class ManagementUninstallFunctionUninstallDialogDelegate
    : public extensions::UninstallDialogDelegate {
 public:
  ManagementUninstallFunctionUninstallDialogDelegate(
      extensions::ManagementUninstallFunctionBase* function,
      const extensions::Extension* target_extension,
      bool show_programmatic_uninstall_ui) {
    // TODO(sentialx): emit event
  }

  ~ManagementUninstallFunctionUninstallDialogDelegate() override = default;

  // disable copy
  ManagementUninstallFunctionUninstallDialogDelegate(
      const ManagementUninstallFunctionUninstallDialogDelegate&) = delete;
  ManagementUninstallFunctionUninstallDialogDelegate& operator=(
      const ManagementUninstallFunctionUninstallDialogDelegate&) = delete;
};

}  // namespace

ElectronManagementAPIDelegate::ElectronManagementAPIDelegate() = default;

ElectronManagementAPIDelegate::~ElectronManagementAPIDelegate() = default;

bool ElectronManagementAPIDelegate::LaunchAppFunctionDelegate(
    const extensions::Extension* extension,
    content::BrowserContext* context) const {
  // Note: Chromium looks for an existing profile and determines whether
  // the user has set a launch preference
  // See: https://chromium-review.googlesource.com/c/chromium/src/+/4389621
  // For Electron, this should always return the default (true).
  // TODO(sentialx): emit event
  extensions::RecordAppLaunchType(extension_misc::APP_LAUNCH_EXTENSION_API,
                                  extension->GetType());
  return true;
}

GURL ElectronManagementAPIDelegate::GetFullLaunchURL(
    const extensions::Extension* extension) const {
  return extensions::AppLaunchInfo::GetFullLaunchURL(extension);
}

extensions::LaunchType ElectronManagementAPIDelegate::GetLaunchType(
    const extensions::ExtensionPrefs* prefs,
    const extensions::Extension* extension) const {
  // TODO(sentialx)
  return extensions::LAUNCH_TYPE_DEFAULT;
}

std::unique_ptr<extensions::InstallPromptDelegate>
ElectronManagementAPIDelegate::SetEnabledFunctionDelegate(
    content::WebContents* web_contents,
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    base::OnceCallback<void(bool)> callback) const {
  return std::make_unique<ManagementSetEnabledFunctionInstallPromptDelegate>(
      web_contents, browser_context, extension, std::move(callback));
}

std::unique_ptr<extensions::UninstallDialogDelegate>
ElectronManagementAPIDelegate::UninstallFunctionDelegate(
    extensions::ManagementUninstallFunctionBase* function,
    const extensions::Extension* target_extension,
    bool show_programmatic_uninstall_ui) const {
  return std::make_unique<ManagementUninstallFunctionUninstallDialogDelegate>(
      function, target_extension, show_programmatic_uninstall_ui);
}

bool ElectronManagementAPIDelegate::CreateAppShortcutFunctionDelegate(
    extensions::ManagementCreateAppShortcutFunction* function,
    const extensions::Extension* extension,
    std::string* error) const {
  return false;  // TODO(sentialx): route event and return true
}

std::unique_ptr<extensions::AppForLinkDelegate>
ElectronManagementAPIDelegate::GenerateAppForLinkFunctionDelegate(
    extensions::ManagementGenerateAppForLinkFunction* function,
    content::BrowserContext* context,
    const std::string& title,
    const GURL& launch_url) const {
  // TODO(sentialx)
  return nullptr;
}

bool ElectronManagementAPIDelegate::CanContextInstallWebApps(
    content::BrowserContext* context) const {
  // TODO(sentialx)
  return false;
}

void ElectronManagementAPIDelegate::InstallOrLaunchReplacementWebApp(
    content::BrowserContext* context,
    const GURL& web_app_url,
    InstallOrLaunchWebAppCallback callback) const {
  // TODO(sentialx)
}

void ElectronManagementAPIDelegate::EnableExtension(
    content::BrowserContext* context,
    const extensions::ExtensionId& extension_id) const {
  // const extensions::Extension* extension =
  //     extensions::ExtensionRegistry::Get(context)->GetExtensionById(
  //         extension_id, extensions::ExtensionRegistry::EVERYTHING);

  // TODO(sentialx): we don't have ExtensionService
  // If the extension was disabled for a permissions increase, the Management
  // API will have displayed a re-enable prompt to the user, so we know it's
  // safe to grant permissions here.
  // extensions::ExtensionSystem::Get(context)
  //     ->extension_service()
  //     ->GrantPermissionsAndEnableExtension(extension);
}

void ElectronManagementAPIDelegate::DisableExtension(
    content::BrowserContext* context,
    const extensions::Extension* source_extension,
    const extensions::ExtensionId& extension_id,
    extensions::disable_reason::DisableReason disable_reason) const {
  // TODO(sentialx): we don't have ExtensionService
  // extensions::ExtensionSystem::Get(context)
  //     ->extension_service()
  //     ->DisableExtensionWithSource(source_extension, extension_id,
  //                                  disable_reason);
}

bool ElectronManagementAPIDelegate::UninstallExtension(
    content::BrowserContext* context,
    const extensions::ExtensionId& transient_extension_id,
    extensions::UninstallReason reason,
    std::u16string* error) const {
  // TODO(sentialx): we don't have ExtensionService
  // return extensions::ExtensionSystem::Get(context)
  //     ->extension_service()
  //     ->UninstallExtension(transient_extension_id, reason, error);
  return false;
}

void ElectronManagementAPIDelegate::SetLaunchType(
    content::BrowserContext* context,
    const extensions::ExtensionId& extension_id,
    extensions::LaunchType launch_type) const {
  // TODO(sentialx)
  // extensions::SetLaunchType(context, extension_id, launch_type);
}

GURL ElectronManagementAPIDelegate::GetIconURL(
    const extensions::Extension* extension,
    int icon_size,
    ExtensionIconSet::Match match,
    bool grayscale) const {
  GURL icon_url(base::StringPrintf(
      "%s%s/%d/%d%s", chrome::kChromeUIExtensionIconURL,
      extension->id().c_str(), icon_size, static_cast<int>(match),
      grayscale ? "?grayscale=true" : ""));
  CHECK(icon_url.is_valid());
  return icon_url;
}

GURL ElectronManagementAPIDelegate::GetEffectiveUpdateURL(
    const extensions::Extension& extension,
    content::BrowserContext* context) const {
  // TODO(codebytere): we do not currently support ExtensionManagement.
  return GURL::EmptyGURL();
}
