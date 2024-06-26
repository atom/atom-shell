// Copyright (c) 2024 Microsoft, GmbH
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/file_system_access/file_system_access_permission_context_factory.h"

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "shell/browser/file_system_access/file_system_access_permission_context.h"

namespace electron {

// static
electron::FileSystemAccessPermissionContext*
FileSystemAccessPermissionContextFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<electron::FileSystemAccessPermissionContext*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

// static
FileSystemAccessPermissionContextFactory*
FileSystemAccessPermissionContextFactory::GetInstance() {
  static base::NoDestructor<FileSystemAccessPermissionContextFactory> instance;
  return instance.get();
}

FileSystemAccessPermissionContextFactory::
    FileSystemAccessPermissionContextFactory()
    : BrowserContextKeyedServiceFactory(
          "FileSystemAccessPermissionContext",
          BrowserContextDependencyManager::GetInstance()) {}

FileSystemAccessPermissionContextFactory::
    ~FileSystemAccessPermissionContextFactory() = default;

// static
KeyedService* FileSystemAccessPermissionContextFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return BuildInstanceFor(context).release();
}

std::unique_ptr<KeyedService>
FileSystemAccessPermissionContextFactory::BuildInstanceFor(
    content::BrowserContext* context) {
  return std::make_unique<FileSystemAccessPermissionContext>(context);
}

}  // namespace electron
