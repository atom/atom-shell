// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_power_monitor.h"

#include <vector>

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>

@interface MacLockMonitor : NSObject {
 @private
  std::vector<electron::api::PowerMonitor*> emitters;
}

- (void)addEmitter:(electron::api::PowerMonitor*)monitor_;

@end

@implementation MacLockMonitor

- (id)init {
  if ((self = [super init])) {
    NSDistributedNotificationCenter* distributed_center =
        [NSDistributedNotificationCenter defaultCenter];
    // A notification that the screen was locked.
    [distributed_center addObserver:self
                           selector:@selector(onScreenLocked:)
                               name:@"com.apple.screenIsLocked"
                             object:nil];
    // A notification that the screen was unlocked by the user.
    [distributed_center addObserver:self
                           selector:@selector(onScreenUnlocked:)
                               name:@"com.apple.screenIsUnlocked"
                             object:nil];
    // A notification that the workspace posts before the machine goes to sleep.
    [distributed_center addObserver:self
                           selector:@selector(isSuspending:)
                               name:NSWorkspaceWillSleepNotification
                             object:nil];
    // A notification that the workspace posts when the machine wakes from
    // sleep.
    [distributed_center addObserver:self
                           selector:@selector(isResuming:)
                               name:NSWorkspaceDidWakeNotification
                             object:nil];

    NSNotificationCenter* shared_center =
        [[NSWorkspace sharedWorkspace] notificationCenter];

    // A notification that the workspace posts when the user session becomes
    // active.
    [shared_center addObserver:self
                      selector:@selector(onUserDidBecomeActive:)
                          name:NSWorkspaceSessionDidBecomeActiveNotification
                        object:nil];
    // A notification that the workspace posts when the user session becomes
    // inactive.
    [shared_center addObserver:self
                      selector:@selector(onUserDidResignActive:)
                          name:NSWorkspaceSessionDidResignActiveNotification
                        object:nil];
  }
  return self;
}

- (void)dealloc {
  [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
}

- (void)addEmitter:(electron::api::PowerMonitor*)monitor_ {
  self->emitters.push_back(monitor_);
}

- (void)isSuspending:(NSNotification*)notify {
  for (auto* emitter : self->emitters) {
    emitter->Emit("suspend");
  }
}

- (void)isResuming:(NSNotification*)notify {
  for (auto* emitter : self->emitters) {
    emitter->Emit("resume");
  }
}

- (void)onScreenLocked:(NSNotification*)notification {
  for (auto* emitter : self->emitters) {
    emitter->Emit("lock-screen");
  }
}

- (void)onScreenUnlocked:(NSNotification*)notification {
  for (auto* emitter : self->emitters) {
    emitter->Emit("unlock-screen");
  }
}

- (void)onUserDidBecomeActive:(NSNotification*)notification {
  for (auto* emitter : self->emitters) {
    emitter->Emit("user-did-become-active");
  }
}

- (void)onUserDidResignActive:(NSNotification*)notification {
  for (auto* emitter : self->emitters) {
    emitter->Emit("user-did-resign-active");
  }
}

@end

namespace electron::api {

static MacLockMonitor* g_lock_monitor = nil;

void PowerMonitor::InitPlatformSpecificMonitors() {
  if (!g_lock_monitor)
    g_lock_monitor = [[MacLockMonitor alloc] init];
  [g_lock_monitor addEmitter:this];
}

}  // namespace electron::api
