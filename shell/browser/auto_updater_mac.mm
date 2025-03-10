// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/auto_updater.h"

#include <string>

#import <ReactiveObjC/NSObject+RACPropertySubscribing.h>
#import <ReactiveObjC/RACCommand.h>
#import <ReactiveObjC/RACSignal.h>
#import <Squirrel/Squirrel.h>

#include "base/functional/bind.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "gin/arguments.h"
#include "shell/browser/browser.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/error_thrower.h"

namespace auto_updater {

namespace {

// The global SQRLUpdater object.
SQRLUpdater* __strong g_updater = nil;

}  // namespace

namespace {

bool g_update_available = false;
std::string update_url_ = "";  // NOLINT(runtime/string)

}  // namespace

std::string AutoUpdater::GetFeedURL() {
  return update_url_;
}

// static
void AutoUpdater::SetFeedURL(gin::Arguments* args) {
  gin_helper::ErrorThrower thrower(args->isolate());
  gin_helper::Dictionary opts;

  std::string feed;
  HeaderMap requestHeaders;
  std::string serverType = "default";
  v8::Local<v8::Value> first_arg = args->PeekNext();
  if (!first_arg.IsEmpty() && first_arg->IsString()) {
    if (args->GetNext(&feed)) {
      args->GetNext(&requestHeaders);
    }
  } else if (args->GetNext(&opts)) {
    if (!opts.Get("url", &feed)) {
      thrower.ThrowError(
          "Expected options object to contain a 'url' string property in "
          "setFeedUrl call");
      return;
    }
    opts.Get("headers", &requestHeaders);
    opts.Get("serverType", &serverType);
    if (serverType != "default" && serverType != "json") {
      thrower.ThrowError("Expected serverType to be 'default' or 'json'");
      return;
    }
  } else {
    thrower.ThrowError(
        "Expected an options object with a 'url' property to be provided");
    return;
  }

  Delegate* delegate = GetDelegate();
  if (!delegate)
    return;

  update_url_ = feed;

  NSURL* url = [NSURL URLWithString:base::SysUTF8ToNSString(feed)];
  NSMutableURLRequest* urlRequest = [NSMutableURLRequest requestWithURL:url];

  for (const auto& it : requestHeaders) {
    [urlRequest setValue:base::SysUTF8ToNSString(it.second)
        forHTTPHeaderField:base::SysUTF8ToNSString(it.first)];
  }

  if (g_updater)
    g_updater = nil;

  // Initialize the SQRLUpdater.
  @try {
    if (serverType == "json") {
      NSString* nsAppVersion =
          base::SysUTF8ToNSString(electron::Browser::Get()->GetVersion());
      g_updater = [[SQRLUpdater alloc] initWithUpdateRequest:urlRequest
                                                  forVersion:nsAppVersion];
    } else {
      // default
      g_updater = [[SQRLUpdater alloc] initWithUpdateRequest:urlRequest];
    }
  } @catch (NSException* error) {
    delegate->OnError(base::SysNSStringToUTF8(error.reason));
    return;
  }

  [[g_updater rac_valuesForKeyPath:@"state" observer:g_updater]
      subscribeNext:^(NSNumber* stateNumber) {
        int state = [stateNumber integerValue];
        // Dispatching the event on main thread.
        dispatch_async(dispatch_get_main_queue(), ^{
          if (state == SQRLUpdaterStateCheckingForUpdate)
            delegate->OnCheckingForUpdate();
          else if (state == SQRLUpdaterStateDownloadingUpdate)
            delegate->OnUpdateAvailable();
        });
      }];
}

// static
void AutoUpdater::CheckForUpdates() {
  Delegate* delegate = GetDelegate();
  if (!delegate)
    return;

  [[[[g_updater.checkForUpdatesCommand execute:nil]
      // Send a `nil` after everything...
      concat:[RACSignal return:nil]]
      // But only take the first value. If an update is sent, we'll get that.
      // Otherwise, we'll get our inserted `nil` value.
      take:1]
      subscribeNext:^(SQRLDownloadedUpdate* downloadedUpdate) {
        if (downloadedUpdate) {
          g_update_available = true;
          SQRLUpdate* update = downloadedUpdate.update;
          // There is a new update that has been downloaded.
          delegate->OnUpdateDownloaded(
              base::SysNSStringToUTF8(update.releaseNotes),
              base::SysNSStringToUTF8(update.releaseName),
              base::Time::FromSecondsSinceUnixEpoch(
                  update.releaseDate.timeIntervalSince1970),
              base::SysNSStringToUTF8(update.updateURL.absoluteString));
        } else {
          g_update_available = false;
          // When the completed event is sent with no update, then we know there
          // is no update available.
          delegate->OnUpdateNotAvailable();
        }
      }
      error:^(NSError* error) {
        NSMutableString* failureString =
            [NSMutableString stringWithString:error.localizedDescription];
        if (error.localizedFailureReason) {
          [failureString appendString:@": "];
          [failureString appendString:error.localizedFailureReason];
        }
        if (error.localizedRecoverySuggestion) {
          if (![failureString hasSuffix:@"."])
            [failureString appendString:@"."];
          [failureString appendString:@" "];
          [failureString appendString:error.localizedRecoverySuggestion];
        }
        delegate->OnError(base::SysNSStringToUTF8(failureString), error.code,
                          base::SysNSStringToUTF8(error.domain));
      }];
}

void AutoUpdater::QuitAndInstall() {
  Delegate* delegate = AutoUpdater::GetDelegate();
  if (g_update_available) {
    [[g_updater relaunchToInstallUpdate] subscribeError:^(NSError* error) {
      if (delegate)
        delegate->OnError(base::SysNSStringToUTF8(error.localizedDescription),
                          error.code, base::SysNSStringToUTF8(error.domain));
    }];
  } else {
    if (delegate)
      delegate->OnError("No update available, can't quit and install");
  }
}

bool AutoUpdater::PrepareUpdateFromFile(gin::Arguments* args) {
  gin_helper::ErrorThrower thrower(args->isolate());

  if (update_url_ != "") {
    thrower.ThrowError(
        "prepareUpdateFromFile cannot be called after setFeedUrl");
    return false;
  }

  if (g_update_available) {
    thrower.ThrowError("An update is already pending");
    return false;
  }

  std::string filename;
  v8::Local<v8::Value> first_arg = args->PeekNext();
  if (!first_arg.IsEmpty() && first_arg->IsString()) {
    args->GetNext(&filename);
  } else {
    thrower.ThrowError("missing filename of bundle");
    return false;
  }

  Delegate* delegate = GetDelegate();
  if (!delegate) {
    thrower.ThrowError("updater not fully initialised!");
    return false;
  }

  if (g_updater)
    g_updater = nil;

  // Initialize the SQRLUpdater.
  g_updater = [SQRLUpdater alloc];

  // TODO - convert better
  NSURL* url = [NSURL URLWithString:base::SysUTF8ToNSString(filename)];
  NSBundle* bundle = [NSBundle bundleWithURL:url];

  if (!bundle) {
    thrower.ThrowError("app bundle not found!");
    return false;
  }

  SQRLUpdate* update =
      [SQRLUpdate alloc];  // This appears to not really be used, but is a
                           // necessary parameter

  [[g_updater verifyAndPrepareUpdateExternal:update fromBundle:bundle]
      subscribeError:^(NSError* error) {
        if (delegate)
          delegate->OnError(base::SysNSStringToUTF8(error.localizedDescription),
                            error.code, base::SysNSStringToUTF8(error.domain));
      }
      completed:^() {
        g_update_available = true;
        delegate->OnUpdateAvailable();
      }];

  return true;
}

bool AutoUpdater::IsVersionAllowedForUpdate(const std::string& current_version,
                                            const std::string& target_version) {
  return [SQRLUpdater
      isVersionAllowedForUpdate:base::SysUTF8ToNSString(target_version)
                           from:base::SysUTF8ToNSString(current_version)];
}

}  // namespace auto_updater
