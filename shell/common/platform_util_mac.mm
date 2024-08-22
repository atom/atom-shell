// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/platform_util.h"

#include <string>
#include <utility>

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>

#include "base/apple/foundation_util.h"
#include "base/apple/osstatus_logging.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/mac/scoped_aedesc.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/browser_task_traits.h"
#include "net/base/apple/url_conversions.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

NSString* GetLoginHelperBundleIdentifier() {
  return [[[NSBundle mainBundle] bundleIdentifier]
      stringByAppendingString:@".loginhelper"];
}

// https://developer.apple.com/documentation/servicemanagement/1561515-service_management_errors?language=objc
std::string GetLaunchStringForError(NSError* error) {
  if (@available(macOS 13, *)) {
    switch ([error code]) {
      case kSMErrorAlreadyRegistered:
        return "The application is already registered";
      case kSMErrorAuthorizationFailure:
        return "The authorization requested failed";
      case kSMErrorLaunchDeniedByUser:
        return "The user denied the app's launch request";
      case kSMErrorInternalFailure:
        return "An internal failure has occurred";
      case kSMErrorInvalidPlist:
        return "The app's property list is invalid";
      case kSMErrorInvalidSignature:
        return "The app's code signature doesn't meet the requirements to "
               "perform the operation";
      case kSMErrorJobMustBeEnabled:
        return "The specified job is not enabled";
      case kSMErrorJobNotFound:
        return "The system can't find the specified job";
      case kSMErrorJobPlistNotFound:
        return "The app's property list cannot be found";
      case kSMErrorServiceUnavailable:
        return "The service necessary to perform this operation is unavailable "
               "or is no longer accepting requests";
      case kSMErrorToolNotValid:
        return "The specified path doesn't exist or the helper tool at the "
               "specified path isn't valid";
      default:
        return base::SysNSStringToUTF8([error localizedDescription]);
    }
  }

  return "";
}

SMAppService* GetServiceForType(const std::string& type,
                                const std::string& name)
    API_AVAILABLE(macosx(13.0)) {
  NSString* service_name = [NSString stringWithUTF8String:name.c_str()];
  if (type == "mainAppService") {
    return [SMAppService mainAppService];
  } else if (type == "agentService") {
    return [SMAppService agentServiceWithPlistName:service_name];
  } else if (type == "daemonService") {
    return [SMAppService daemonServiceWithPlistName:service_name];
  } else if (type == "loginItemService") {
    return [SMAppService loginItemServiceWithIdentifier:service_name];
  } else {
    LOG(ERROR) << "Unrecognized login item type";
    return nullptr;
  }
}

bool GetLoginItemEnabledDeprecated() {
  BOOL enabled = NO;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // SMJobCopyDictionary does not work in sandbox (see rdar://13626319)
  CFArrayRef jobs = SMCopyAllJobDictionaries(kSMDomainUserLaunchd);
#pragma clang diagnostic pop
  NSArray* jobs_ = CFBridgingRelease(jobs);
  NSString* identifier = GetLoginHelperBundleIdentifier();
  if (jobs_ && [jobs_ count] > 0) {
    for (NSDictionary* job in jobs_) {
      if ([identifier isEqualToString:[job objectForKey:@"Label"]]) {
        enabled = [[job objectForKey:@"OnDemand"] boolValue];
        break;
      }
    }
  }
  return enabled;
}

}  // namespace

namespace platform_util {

void ShowItemInFolder(const base::FilePath& path) {
  // The API only takes absolute path.
  base::FilePath full_path =
      path.IsAbsolute() ? path : base::MakeAbsoluteFilePath(path);

  DCHECK([NSThread isMainThread]);
  NSString* path_string = base::SysUTF8ToNSString(full_path.value());
  if (!path_string || ![[NSWorkspace sharedWorkspace] selectFile:path_string
                                        inFileViewerRootedAtPath:@""]) {
    LOG(WARNING) << "NSWorkspace failed to select file " << full_path.value();
  }
}

void OpenPath(const base::FilePath& full_path, OpenCallback callback) {
  DCHECK([NSThread isMainThread]);
  NSURL* ns_url = base::apple::FilePathToNSURL(full_path);
  if (!ns_url) {
    std::move(callback).Run("Invalid path");
    return;
  }

  bool success = [[NSWorkspace sharedWorkspace] openURL:ns_url];
  std::move(callback).Run(success ? "" : "Failed to open path");
}

void OpenExternal(const GURL& url,
                  const OpenExternalOptions& options,
                  OpenCallback callback) {
  DCHECK([NSThread isMainThread]);
  NSURL* ns_url = net::NSURLWithGURL(url);
  if (!ns_url) {
    std::move(callback).Run("Invalid URL");
    return;
  }

  bool success = [[NSWorkspace sharedWorkspace] openURL:ns_url];
  if (success && options.activate)
    [NSApp activateIgnoringOtherApps:YES];

  std::move(callback).Run(success ? "" : "Failed to open URL");
}

bool MoveItemToTrashWithError(const base::FilePath& full_path,
                              bool delete_on_fail,
                              std::string* error) {
  NSURL* url = base::apple::FilePathToNSURL(full_path);
  if (!url) {
    *error = "Invalid file path: " + full_path.value();
    LOG(WARNING) << *error;
    return false;
  }

  NSError* err = nil;
  BOOL did_trash = [[NSFileManager defaultManager] trashItemAtURL:url
                                                 resultingItemURL:nil
                                                            error:&err];

  if (delete_on_fail) {
    // Some volumes may not support a Trash folder or it may be disabled
    // so these methods will report failure by returning NO or nil and
    // an NSError with NSFeatureUnsupportedError.
    // Handle this by deleting the item as a fallback.
    if (!did_trash && [err code] == NSFeatureUnsupportedError) {
      did_trash = [[NSFileManager defaultManager] removeItemAtURL:url
                                                            error:&err];
    }
  }

  if (!did_trash) {
    *error = base::SysNSStringToUTF8([err localizedDescription]);
    LOG(WARNING) << "NSWorkspace failed to move file " << full_path.value()
                 << " to trash: " << *error;
  }

  return did_trash;
}

namespace internal {

bool PlatformTrashItem(const base::FilePath& full_path, std::string* error) {
  return MoveItemToTrashWithError(full_path, false, error);
}

}  // namespace internal

void Beep() {
  NSBeep();
}

std::string GetLoginItemEnabled(const std::string& type,
                                const std::string& service_name) {
  bool enabled = GetLoginItemEnabledDeprecated();
  if (@available(macOS 13, *)) {
    SMAppService* service = GetServiceForType(type, service_name);
    SMAppServiceStatus status = [service status];
    if (status == SMAppServiceStatusNotRegistered)
      return "not-registered";
    else if (status == SMAppServiceStatusEnabled)
      return "enabled";
    else if (status == SMAppServiceStatusRequiresApproval)
      return "requires-approval";
    else if (status == SMAppServiceStatusNotFound) {
      // If the login item was enabled with the old API, return that.
      return enabled ? "enabled-deprecated" : "not-found";
    }
  }
  return enabled ? "enabled" : "not-registered";
}

bool SetLoginItemEnabled(const std::string& type,
                         const std::string& service_name,
                         bool enabled) {
  if (@available(macOS 13, *)) {
#if IS_MAS_BUILD()
    // If the app was previously set as a LoginItem with the old API, remove it
    // as a LoginItem via the old API before re-enabling with the new API.
    if (GetLoginItemEnabledDeprecated() && enabled) {
      NSString* identifier = GetLoginHelperBundleIdentifier();
      SMLoginItemSetEnabled((__bridge CFStringRef)identifier, false);
    }
#endif
    SMAppService* service = GetServiceForType(type, service_name);
    NSError* error = nil;
    bool result = enabled ? [service registerAndReturnError:&error]
                          : [service unregisterAndReturnError:&error];
    if (error != nil)
      LOG(ERROR) << "Unable to set login item: "
                 << GetLaunchStringForError(error);
    return result;
  } else {
    NSString* identifier = GetLoginHelperBundleIdentifier();
    return SMLoginItemSetEnabled((__bridge CFStringRef)identifier, enabled);
  }
}

}  // namespace platform_util
