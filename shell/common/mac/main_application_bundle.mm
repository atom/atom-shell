// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Adam Roben <adam@roben.org>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "shell/common/mac/main_application_bundle.h"

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "content/browser/mac_helpers.h"

namespace electron {

namespace {

bool HasMainProcessKey() {
  NSDictionary* info_dictionary = [base::apple::MainBundle() infoDictionary];
  return
      [[info_dictionary objectForKey:@"ElectronMainProcess"] boolValue] != NO;
}

}  // namespace

base::FilePath MainApplicationBundlePath() {
  // Start out with the path to the running executable.
  base::FilePath path;
  base::PathService::Get(base::FILE_EXE, &path);

  // Up to Contents.
  if (!HasMainProcessKey() &&
      (path.value().ends_with(" Helper") ||
       path.value().ends_with(content::kMacHelperSuffix_plugin) ||
       path.value().ends_with(content::kMacHelperSuffix_renderer) ||
       path.value().ends_with(content::kMacHelperSuffix_gpu))) {
    // The running executable is the helper. Go up five steps:
    // Contents/Frameworks/Helper.app/Contents/MacOS/Helper
    // ^ to here                                     ^ from here
    path = path.DirName().DirName().DirName().DirName().DirName();
  } else {
    // One step up to MacOS, another to Contents.
    path = path.DirName().DirName();
  }
  DCHECK_EQ(path.BaseName().value(), "Contents");

  // Up one more level to the .app.
  path = path.DirName();
  DCHECK_EQ(path.BaseName().Extension(), ".app");

  return path;
}

NSBundle* MainApplicationBundle() {
  return [NSBundle bundleWithPath:base::apple::FilePathToNSString(
                                      MainApplicationBundlePath())];
}

}  // namespace electron
