// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_AUTO_UPDATER_H_
#define ELECTRON_SHELL_BROWSER_AUTO_UPDATER_H_

#include <map>
#include <string>

namespace base {
class Time;
}

namespace gin {
class Arguments;
}

namespace auto_updater {

class Delegate {
 public:
  // An error happened.
  virtual void OnError(const std::string& message) {}

  virtual void OnError(const std::string& message,
                       const int code,
                       const std::string& domain) {}

  // Checking to see if there is an update
  virtual void OnCheckingForUpdate() {}

  // There is an update available and it is being downloaded
  virtual void OnUpdateAvailable() {}

  // There is no available update.
  virtual void OnUpdateNotAvailable() {}

  // There is a new update which has been downloaded.
  virtual void OnUpdateDownloaded(const std::string& release_notes,
                                  const std::string& release_name,
                                  const base::Time& release_date,
                                  const std::string& update_url) {}

 protected:
  virtual ~Delegate() = default;
};

class AutoUpdater {
 public:
  typedef std::map<std::string, std::string> HeaderMap;

  AutoUpdater() = delete;

  // disable copy
  AutoUpdater(const AutoUpdater&) = delete;
  AutoUpdater& operator=(const AutoUpdater&) = delete;

  // Gets/Sets the delegate.
  static Delegate* GetDelegate();
  static void SetDelegate(Delegate* delegate);

  static std::string GetFeedURL();
  // FIXME(zcbenz): We should not do V8 in this file, this method should only
  // accept C++ struct as parameter, and atom_api_auto_updater.cc is responsible
  // for parsing the parameter from JavaScript.
  static void SetFeedURL(gin::Arguments* args);
  static void CheckForUpdates();
  static void QuitAndInstall();
  static bool PrepareUpdateFromFile(gin::Arguments* args);

  static bool IsVersionAllowedForUpdate(const std::string& current_version,
                                        const std::string& target_version);

 private:
  static Delegate* delegate_;
};

}  // namespace auto_updater

#endif  // ELECTRON_SHELL_BROWSER_AUTO_UPDATER_H_
