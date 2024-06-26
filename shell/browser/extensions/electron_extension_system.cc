// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/browser/extensions/electron_extension_system.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_string_value_serializer.h"
#include "base/path_service.h"
#include "base/values.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/grit/browser_resources.h"
#include "components/value_store/value_store_factory_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "electron/buildflags/buildflags.h"
#include "extensions/browser/api/app_runtime/app_runtime_api.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/null_app_sorting.h"
#include "extensions/browser/quota_service.h"
#include "extensions/browser/service_worker_manager.h"
#include "extensions/browser/user_script_manager.h"
#include "extensions/common/constants.h"
#include "extensions/common/file_util.h"
#include "shell/browser/extensions/electron_extension_loader.h"

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "chrome/browser/pdf/pdf_extension_util.h"  // nogncheck
#endif

using content::BrowserContext;
using content::BrowserThread;

namespace extensions {

ElectronExtensionSystem::ElectronExtensionSystem(
    BrowserContext* browser_context)
    : browser_context_(browser_context),
      store_factory_(base::MakeRefCounted<value_store::ValueStoreFactoryImpl>(
          browser_context->GetPath())) {}

ElectronExtensionSystem::~ElectronExtensionSystem() = default;

void ElectronExtensionSystem::LoadExtension(
    const base::FilePath& extension_dir,
    int load_flags,
    base::OnceCallback<void(const Extension*, const std::string&)> cb) {
  extension_loader_->LoadExtension(extension_dir, load_flags, std::move(cb));
}

void ElectronExtensionSystem::FinishInitialization() {
  // Inform the rest of the extensions system to start.
  ready_.Signal();
}

void ElectronExtensionSystem::ReloadExtension(const ExtensionId& extension_id) {
  extension_loader_->ReloadExtension(extension_id);
}

void ElectronExtensionSystem::RemoveExtension(const ExtensionId& extension_id) {
  extension_loader_->UnloadExtension(
      extension_id, extensions::UnloadedExtensionReason::UNINSTALL);
}

void ElectronExtensionSystem::Shutdown() {
  extension_loader_.reset();
}

void ElectronExtensionSystem::InitForRegularProfile(bool extensions_enabled) {
  service_worker_manager_ =
      std::make_unique<ServiceWorkerManager>(browser_context_);
  quota_service_ = std::make_unique<QuotaService>();
  user_script_manager_ = std::make_unique<UserScriptManager>(browser_context_);
  app_sorting_ = std::make_unique<NullAppSorting>();
  extension_loader_ =
      std::make_unique<ElectronExtensionLoader>(browser_context_);

  if (!browser_context_->IsOffTheRecord())
    LoadComponentExtensions();

  management_policy_ = std::make_unique<ManagementPolicy>();
}

std::unique_ptr<base::Value::Dict> ParseManifest(
    const std::string_view manifest_contents) {
  JSONStringValueDeserializer deserializer(manifest_contents);
  std::unique_ptr<base::Value> manifest =
      deserializer.Deserialize(nullptr, nullptr);

  if (!manifest.get() || !manifest->is_dict()) {
    LOG(ERROR) << "Failed to parse extension manifest.";
    return std::unique_ptr<base::Value::Dict>();
  }
  return std::make_unique<base::Value::Dict>(std::move(*manifest).TakeDict());
}

void ElectronExtensionSystem::LoadComponentExtensions() {
  std::string utf8_error;
#if BUILDFLAG(ENABLE_PDF_VIEWER)
  std::string pdf_manifest_string = pdf_extension_util::GetManifest();
  std::unique_ptr<base::Value::Dict> pdf_manifest =
      ParseManifest(pdf_manifest_string);
  if (pdf_manifest) {
    base::FilePath root_directory;
    CHECK(base::PathService::Get(chrome::DIR_RESOURCES, &root_directory));
    root_directory = root_directory.Append(FILE_PATH_LITERAL("pdf"));
    scoped_refptr<const Extension> pdf_extension =
        extensions::Extension::Create(
            root_directory, extensions::mojom::ManifestLocation::kComponent,
            *pdf_manifest, extensions::Extension::REQUIRE_KEY, &utf8_error);
    extension_loader_->registrar()->AddExtension(pdf_extension);
  }
#endif
}

ExtensionService* ElectronExtensionSystem::extension_service() {
  return nullptr;
}

ManagementPolicy* ElectronExtensionSystem::management_policy() {
  return management_policy_.get();
}

ServiceWorkerManager* ElectronExtensionSystem::service_worker_manager() {
  return service_worker_manager_.get();
}

UserScriptManager* ElectronExtensionSystem::user_script_manager() {
  return user_script_manager_.get();
}

StateStore* ElectronExtensionSystem::state_store() {
  return nullptr;
}

StateStore* ElectronExtensionSystem::rules_store() {
  return nullptr;
}

StateStore* ElectronExtensionSystem::dynamic_user_scripts_store() {
  return nullptr;
}

scoped_refptr<value_store::ValueStoreFactory>
ElectronExtensionSystem::store_factory() {
  return store_factory_;
}

QuotaService* ElectronExtensionSystem::quota_service() {
  return quota_service_.get();
}

AppSorting* ElectronExtensionSystem::app_sorting() {
  return app_sorting_.get();
}

const base::OneShotEvent& ElectronExtensionSystem::ready() const {
  return ready_;
}

bool ElectronExtensionSystem::is_ready() const {
  return ready_.is_signaled();
}

ContentVerifier* ElectronExtensionSystem::content_verifier() {
  return nullptr;
}

std::unique_ptr<ExtensionSet> ElectronExtensionSystem::GetDependentExtensions(
    const Extension* extension) {
  return std::make_unique<ExtensionSet>();
}

void ElectronExtensionSystem::InstallUpdate(
    const std::string& extension_id,
    const std::string& public_key,
    const base::FilePath& temp_dir,
    bool install_immediately,
    InstallUpdateCallback install_update_callback) {
  NOTREACHED();
}

bool ElectronExtensionSystem::FinishDelayedInstallationIfReady(
    const std::string& extension_id,
    bool install_immediately) {
  NOTREACHED();
}

void ElectronExtensionSystem::PerformActionBasedOnOmahaAttributes(
    const std::string& extension_id,
    const base::Value::Dict& attributes) {
  NOTREACHED();
}

void ElectronExtensionSystem::OnExtensionRegisteredWithRequestContexts(
    scoped_refptr<Extension> extension) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context_);
  registry->AddReady(extension);
  registry->TriggerOnReady(extension.get());
}

}  // namespace extensions
