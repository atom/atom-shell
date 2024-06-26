// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_AUTO_UPDATER_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_AUTO_UPDATER_H_

#include <string>

#include "gin/handle.h"
#include "gin/wrappable.h"
#include "shell/browser/auto_updater.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/browser/window_list_observer.h"

namespace electron::api {

class AutoUpdater : public gin::Wrappable<AutoUpdater>,
                    public gin_helper::EventEmitterMixin<AutoUpdater>,
                    public auto_updater::Delegate,
                    private WindowListObserver {
 public:
  static gin::Handle<AutoUpdater> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  AutoUpdater(const AutoUpdater&) = delete;
  AutoUpdater& operator=(const AutoUpdater&) = delete;

 protected:
  AutoUpdater();
  ~AutoUpdater() override;

  // auto_updater::Delegate:
  void OnError(const std::string& message) override;
  void OnError(const std::string& message,
               const int code,
               const std::string& domain) override;
  void OnCheckingForUpdate() override;
  void OnUpdateAvailable() override;
  void OnUpdateNotAvailable() override;
  void OnUpdateDownloaded(const std::string& release_notes,
                          const std::string& release_name,
                          const base::Time& release_date,
                          const std::string& update_url) override;

  // WindowListObserver:
  void OnWindowAllClosed() override;

 private:
  std::string GetFeedURL();
  void QuitAndInstall();
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_AUTO_UPDATER_H_
