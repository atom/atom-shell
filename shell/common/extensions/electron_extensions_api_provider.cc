// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/common/extensions/electron_extensions_api_provider.h"

#include <memory>
#include <string>

#include "base/containers/span.h"
#include "chrome/common/extensions/chrome_manifest_url_handlers.h"
#include "chrome/common/extensions/manifest_handlers/minimum_chrome_version_checker.h"  // nogncheck
#include "electron/buildflags/buildflags.h"
#include "electron/shell/common/extensions/api/generated_schemas.h"
#include "extensions/common/alias.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/features/json_feature_provider_source.h"
#include "extensions/common/features/simple_feature.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handler.h"
#include "extensions/common/manifest_handler_registry.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/manifest_url_handlers.h"
#include "extensions/common/permissions/permissions_info.h"
#include "shell/common/extensions/api/api_features.h"
#include "shell/common/extensions/api/manifest_features.h"
#include "shell/common/extensions/api/permission_features.h"

namespace extensions {
namespace {

constexpr APIPermissionInfo::InitInfo permissions_to_register[] = {
    {mojom::APIPermissionID::kDevtools, "devtools",
     APIPermissionInfo::kFlagImpliesFullURLAccess |
         APIPermissionInfo::kFlagCannotBeOptional |
         APIPermissionInfo::kFlagInternal},
    {mojom::APIPermissionID::kResourcesPrivate, "resourcesPrivate",
     APIPermissionInfo::kFlagCannotBeOptional},
#if BUILDFLAG(ENABLE_PDF_VIEWER)
    {mojom::APIPermissionID::kPdfViewerPrivate, "pdfViewerPrivate"},
#endif
    {mojom::APIPermissionID::kManagement, "management"},
    {mojom::APIPermissionID::kTab, "tabs",
     APIPermissionInfo::kFlagRequiresManagementUIWarning},
    {mojom::APIPermissionID::kScripting, "scripting",
     APIPermissionInfo::kFlagRequiresManagementUIWarning},
};
base::span<const APIPermissionInfo::InitInfo> GetPermissionInfos() {
  return base::make_span(permissions_to_register);
}
base::span<const Alias> GetPermissionAliases() {
  return base::span<const Alias>();
}

}  // namespace
}  // namespace extensions

namespace electron {

ElectronExtensionsAPIProvider::ElectronExtensionsAPIProvider() = default;
ElectronExtensionsAPIProvider::~ElectronExtensionsAPIProvider() = default;

void ElectronExtensionsAPIProvider::AddAPIFeatures(
    extensions::FeatureProvider* provider) {
  extensions::AddElectronAPIFeatures(provider);
}

void ElectronExtensionsAPIProvider::AddManifestFeatures(
    extensions::FeatureProvider* provider) {
  extensions::AddElectronManifestFeatures(provider);
}

void ElectronExtensionsAPIProvider::AddPermissionFeatures(
    extensions::FeatureProvider* provider) {
  extensions::AddElectronPermissionFeatures(provider);
}

void ElectronExtensionsAPIProvider::AddBehaviorFeatures(
    extensions::FeatureProvider* provider) {
  // No shell-specific behavior features.
}

void ElectronExtensionsAPIProvider::AddAPIJSONSources(
    extensions::JSONFeatureProviderSource* json_source) {
  // json_source->LoadJSON(IDR_SHELL_EXTENSION_API_FEATURES);
}

bool ElectronExtensionsAPIProvider::IsAPISchemaGenerated(
    const std::string& name) {
  return extensions::api::ElectronGeneratedSchemas::IsGenerated(name);
}

std::string_view ElectronExtensionsAPIProvider::GetAPISchema(
    const std::string& name) {
  return extensions::api::ElectronGeneratedSchemas::Get(name);
}

void ElectronExtensionsAPIProvider::RegisterPermissions(
    extensions::PermissionsInfo* permissions_info) {
  permissions_info->RegisterPermissions(extensions::GetPermissionInfos(),
                                        extensions::GetPermissionAliases());
}

void ElectronExtensionsAPIProvider::RegisterManifestHandlers() {
  extensions::ManifestHandlerRegistry* registry =
      extensions::ManifestHandlerRegistry::Get();
  registry->RegisterHandler(
      std::make_unique<extensions::DevToolsPageHandler>());
  registry->RegisterHandler(
      std::make_unique<extensions::MinimumChromeVersionChecker>());
}

}  // namespace electron
