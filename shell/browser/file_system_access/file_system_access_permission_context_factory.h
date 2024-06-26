// Copyright (c) 2024 Microsoft, GmbH
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_FILE_SYSTEM_ACCESS_FILE_SYSTEM_ACCESS_PERMISSION_CONTEXT_FACTORY_H_
#define ELECTRON_SHELL_BROWSER_FILE_SYSTEM_ACCESS_FILE_SYSTEM_ACCESS_PERMISSION_CONTEXT_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "shell/browser/file_system_access/file_system_access_permission_context.h"

namespace electron {

class FileSystemAccessPermissionContextFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static FileSystemAccessPermissionContext* GetForBrowserContext(
      content::BrowserContext* context);
  static FileSystemAccessPermissionContextFactory* GetInstance();

  static std::unique_ptr<KeyedService> BuildInstanceFor(
      content::BrowserContext* context);

  FileSystemAccessPermissionContextFactory(
      const FileSystemAccessPermissionContextFactory&) = delete;
  FileSystemAccessPermissionContextFactory& operator=(
      const FileSystemAccessPermissionContextFactory&) = delete;

 private:
  friend class base::NoDestructor<FileSystemAccessPermissionContextFactory>;

  FileSystemAccessPermissionContextFactory();
  ~FileSystemAccessPermissionContextFactory() override;

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_FILE_SYSTEM_ACCESS_FILE_SYSTEM_ACCESS_PERMISSION_CONTEXT_FACTORY_H_
