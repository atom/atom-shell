// Copyright (c) 2022 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <utility>

#include "base/at_exit.h"
#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/i18n/icu_util.h"
#include "chrome/browser/headless/headless_mode_util.h"
#include "content/public/app/content_main.h"
#include "electron/buildflags/buildflags.h"
#include "electron/fuses.h"
#include "shell/app/electron_main_delegate.h"  // NOLINT
#include "shell/app/node_main.h"
#include "shell/app/uv_stdio_fix.h"
#include "shell/common/electron_command_line.h"
#include "shell/common/electron_constants.h"

namespace {

bool IsEnvSet(const char* name) {
  char* indicator = getenv(name);
  return indicator && indicator[0] != '\0';
}

}  // namespace

int main(int argc, char* argv[]) {
  FixStdioStreams();

  if (electron::fuses::IsRunAsNodeEnabled() && IsEnvSet(electron::kRunAsNode)) {
    base::i18n::InitializeICU();
    base::AtExitManager atexit_manager;
    return electron::NodeMain(argc, argv);
  }

  electron::ElectronMainDelegate delegate;
  content::ContentMainParams params(&delegate);

  base::CommandLine::Init(argc, argv);
  [[maybe_unused]] base::CommandLine* command_line(
      base::CommandLine::ForCurrentProcess());
  if (headless::IsHeadlessMode()) {
    headless::SetUpCommandLine(command_line);
  }

  electron::ElectronCommandLine::InitializeFromCommandLine();
  return content::ContentMain(std::move(params));
}
