// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/platform_util.h"

#include <stdio.h>

#include "base/files/file_util.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "url/gurl.h"

namespace {

bool XDGUtil(const std::string& util, const std::string& arg) {
  std::vector<std::string> argv;
  argv.push_back(util);
  argv.push_back(arg);

  base::LaunchOptions options;
  options.allow_new_privs = true;
  // xdg-open can fall back on mailcap which eventually might plumb through
  // to a command that needs a terminal.  Set the environment variable telling
  // it that we definitely don't have a terminal available and that it should
  // bring up a new terminal if necessary.  See "man mailcap".
  options.environ["MM_NOTTTY"] = "1";

  base::Process process = base::LaunchProcess(argv, options);
  if (!process.IsValid())
    return false;

  int exit_code = -1;
  if (!process.WaitForExit(&exit_code))
    return false;

  return (exit_code == 0);
}

bool XDGOpen(const std::string& path) {
  return XDGUtil("xdg-open", path);
}

bool XDGEmail(const std::string& email) {
  return XDGUtil("xdg-email", email);
}

}  // namespace

namespace platform_util {

// TODO(estade): It would be nice to be able to select the file in the file
// manager, but that probably requires extending xdg-open. For now just
// show the folder.
void ShowItemInFolder(const base::FilePath& full_path) {
  base::FilePath dir = full_path.DirName();
  if (!base::DirectoryExists(dir))
    return;

  XDGOpen(dir.value());
}

void OpenItem(const base::FilePath& full_path) {
  XDGOpen(full_path.value());
}

bool OpenExternal(const GURL& url, const bool without_activation) {
  if (url.SchemeIs("mailto"))
    return XDGEmail(url.spec());
  else
    return XDGOpen(url.spec());
}

bool MoveItemToTrash(const base::FilePath& full_path) {
  return XDGUtil("gvfs-trash", full_path.value());
}

void Beep() {
  // echo '\a' > /dev/console
  FILE* console = fopen("/dev/console", "r");
  if (console == NULL)
    return;
  fprintf(console, "\a");
  fclose(console);
}

}  // namespace platform_util
