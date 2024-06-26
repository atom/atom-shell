// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/app/electron_main_delegate.h"

#include <string>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/sys_string_conversions.h"
#include "content/browser/mac_helpers.h"
#include "content/public/common/content_paths.h"
#include "shell/browser/mac/electron_application.h"
#include "shell/common/application_info.h"
#include "shell/common/mac/main_application_bundle.h"

namespace electron {

namespace {

base::FilePath GetFrameworksPath() {
  return MainApplicationBundlePath().Append("Contents").Append("Frameworks");
}

base::FilePath GetHelperAppPath(const base::FilePath& frameworks_path,
                                const std::string& name) {
  // Figure out what helper we are running
  base::FilePath path;
  base::PathService::Get(base::FILE_EXE, &path);

  std::string helper_name = "Helper";
  if (const auto& val = path.value();
      val.ends_with(content::kMacHelperSuffix_renderer)) {
    helper_name += content::kMacHelperSuffix_renderer;
  } else if (val.ends_with(content::kMacHelperSuffix_gpu)) {
    helper_name += content::kMacHelperSuffix_gpu;
  } else if (val.ends_with(content::kMacHelperSuffix_plugin)) {
    helper_name += content::kMacHelperSuffix_plugin;
  }

  return frameworks_path.Append(name + " " + helper_name + ".app")
      .Append("Contents")
      .Append("MacOS")
      .Append(name + " " + helper_name);
}

}  // namespace

void ElectronMainDelegate::OverrideFrameworkBundlePath() {
  base::apple::SetOverrideFrameworkBundlePath(
      GetFrameworksPath().Append(ELECTRON_PRODUCT_NAME " Framework.framework"));
}

void ElectronMainDelegate::OverrideChildProcessPath() {
  base::FilePath frameworks_path = GetFrameworksPath();
  base::FilePath helper_path =
      GetHelperAppPath(frameworks_path, ELECTRON_PRODUCT_NAME);
  if (!base::PathExists(helper_path))
    helper_path = GetHelperAppPath(frameworks_path, GetApplicationName());
  if (!base::PathExists(helper_path))
    LOG(FATAL) << "Unable to find helper app";
  base::PathService::OverrideAndCreateIfNeeded(
      content::CHILD_PROCESS_EXE, helper_path, /*is_absolute=*/true,
      /*create=*/false);
}

void ElectronMainDelegate::SetUpBundleOverrides() {
  @autoreleasepool {
    NSBundle* bundle = MainApplicationBundle();
    std::string base_bundle_id =
        base::SysNSStringToUTF8([bundle bundleIdentifier]);
    NSString* team_id = [bundle objectForInfoDictionaryKey:@"ElectronTeamID"];
    if (team_id)
      base_bundle_id = base::SysNSStringToUTF8(team_id) + "." + base_bundle_id;
    base::apple::SetBaseBundleID(base_bundle_id.c_str());
  }
}

void RegisterAtomCrApp() {
  // Force the NSApplication subclass to be used.
  [AtomApplication sharedApplication];
}

}  // namespace electron
