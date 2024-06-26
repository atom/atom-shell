// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_SAVE_PAGE_HANDLER_H_
#define ELECTRON_SHELL_BROWSER_API_SAVE_PAGE_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/save_page_type.h"
#include "shell/common/gin_helper/promise.h"
#include "v8/include/v8.h"

namespace base {
class FilePath;
}

namespace content {
class WebContents;
}

namespace electron::api {

// A self-destroyed class for handling save page request.
class SavePageHandler : private content::DownloadManager::Observer,
                        private download::DownloadItem::Observer {
 public:
  SavePageHandler(content::WebContents* web_contents,
                  gin_helper::Promise<void> promise);
  ~SavePageHandler() override;

  bool Handle(const base::FilePath& full_path,
              const content::SavePageType& save_type);

 private:
  void Destroy(download::DownloadItem* item);

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;

  raw_ptr<content::WebContents> web_contents_;  // weak
  gin_helper::Promise<void> promise_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_SAVE_PAGE_HANDLER_H_
