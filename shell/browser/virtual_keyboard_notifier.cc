// Copyright (c) 2022 Microsoft. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/shell/browser/virtual_keyboard_notifier.h"

#include "base/logging.h"

namespace electron {

VirtualKeyboardNotifier* VirtualKeyboardNotifier::GetInstance() {
  return base::Singleton<VirtualKeyboardNotifier>::get();
}

bool VirtualKeyboardNotifier::AddObserver(Observer* observer) {
  if (observers_.find(observer) != observers_.end()) {
    LOG(WARNING)
        << "Adding observer to VirtualKeyboardNotifier that is already present."
           "Please make sure you have code that removes an observer"
           "and/or you do not add observer more than once.";
    return false;
  }

  observers_.insert(observer);
  return true;
}

void VirtualKeyboardNotifier::RemoveObserver(Observer* observer) {
  observers_.erase(observer);
}

void VirtualKeyboardNotifier::NotifyKeyboardVisible(const gfx::Rect& rect) {
  for (Observer* observer : observers_) {
    observer->OnKeyboardVisible(rect);
  }
}

void VirtualKeyboardNotifier::NotifyKeyboardHidden() {
  for (Observer* observer : observers_) {
    observer->OnKeyboardHidden();
  }
}

VirtualKeyboardNotifier::~VirtualKeyboardNotifier() {
  if (!observers_.empty()) {
    LOG(WARNING)
        << "Not all observers were removed from VirtualKeyboardNotifier "
           "before its destruction. Please make sure you call RemoveObserver "
           "on observer's destruction and make sure that observer is "
           "destructable at all.";
  }
}

VirtualKeyboardNotifier::VirtualKeyboardNotifier() = default;

}  // namespace electron
