// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_browser_main_parts.h"

#include <string>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/path_service.h"
#include "services/device/public/cpp/geolocation/geolocation_system_permission_manager.h"
#include "services/device/public/cpp/geolocation/system_geolocation_source_apple.h"
#include "shell/browser/browser_process_impl.h"
#include "shell/browser/mac/electron_application.h"
#include "shell/browser/mac/electron_application_delegate.h"
#include "shell/common/electron_paths.h"
#include "ui/base/l10n/l10n_util_mac.h"

namespace electron {

static ElectronApplicationDelegate* __strong delegate_;

void ElectronBrowserMainParts::PreCreateMainMessageLoop() {
  // Set our own application delegate.
  delegate_ = [[ElectronApplicationDelegate alloc] init];
  [NSApp setDelegate:delegate_];

  PreCreateMainMessageLoopCommon();

  // Prevent Cocoa from turning command-line arguments into
  // |-application:openFiles:|, since we already handle them directly.
  [[NSUserDefaults standardUserDefaults]
      setObject:@"NO"
         forKey:@"NSTreatUnknownArgumentsAsOpen"];

  if (!device::GeolocationSystemPermissionManager::GetInstance()) {
    device::GeolocationSystemPermissionManager::SetInstance(
        device::SystemGeolocationSourceApple::
            CreateGeolocationSystemPermissionManager());
  }
}

void ElectronBrowserMainParts::FreeAppDelegate() {
  delegate_ = nil;
  [NSApp setDelegate:nil];
}

void ElectronBrowserMainParts::RegisterURLHandler() {
  [[AtomApplication sharedApplication] registerURLHandler];
}

// Replicates NSApplicationMain, but doesn't start a run loop.
void ElectronBrowserMainParts::InitializeMainNib() {
  auto infoDictionary = base::apple::OuterBundle().infoDictionary;

  auto principalClass =
      NSClassFromString([infoDictionary objectForKey:@"NSPrincipalClass"]);
  auto application = [principalClass sharedApplication];

  NSString* mainNibName = [infoDictionary objectForKey:@"NSMainNibFile"];

  NSNib* mainNib;

  @try {
    mainNib = [[NSNib alloc] initWithNibNamed:mainNibName
                                       bundle:base::apple::FrameworkBundle()];
    // Handle failure of initWithNibNamed on SMB shares
    // TODO(codebytere): Remove when
    // https://bugs.chromium.org/p/chromium/issues/detail?id=932935 is fixed
  } @catch (NSException* exception) {
    NSString* nibPath =
        [NSString stringWithFormat:@"Resources/%@.nib", mainNibName];
    nibPath = [base::apple::FrameworkBundle().bundlePath
        stringByAppendingPathComponent:nibPath];

    NSData* data = [NSData dataWithContentsOfFile:nibPath];
    mainNib = [[NSNib alloc] initWithNibData:data
                                      bundle:base::apple::FrameworkBundle()];
  }

  [mainNib instantiateWithOwner:application topLevelObjects:nil];
}

std::string ElectronBrowserMainParts::GetCurrentSystemLocale() {
  NSString* systemLocaleIdentifier =
      [[NSLocale currentLocale] localeIdentifier];

  // Mac OS X uses "_" instead of "-", so swap to get a real locale value.
  std::string locale_value = [[systemLocaleIdentifier
      stringByReplacingOccurrencesOfString:@"_"
                                withString:@"-"] UTF8String];

  return locale_value;
}

}  // namespace electron
