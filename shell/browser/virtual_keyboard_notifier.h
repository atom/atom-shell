// Copyright (c) 2022 Microsoft. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_VIRTUAL_KEYBOARD_NOTIFIER_H_
#define ELECTRON_SHELL_BROWSER_VIRTUAL_KEYBOARD_NOTIFIER_H_

#include <unordered_set>

#include "base/memory/singleton.h"

namespace gfx {
class Rect;
}

namespace electron {

class VirtualKeyboardNotifier {
 public:
  class Observer {
   public:
    virtual void OnKeyboardVisible(const gfx::Rect& rect) = 0;
    virtual void OnKeyboardHidden() = 0;
  };

  static VirtualKeyboardNotifier* GetInstance();

  bool AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void NotifyKeyboardVisible(const gfx::Rect& rect);
  void NotifyKeyboardHidden();

  virtual ~VirtualKeyboardNotifier();

  VirtualKeyboardNotifier(const VirtualKeyboardNotifier&) = delete;
  VirtualKeyboardNotifier& operator=(const VirtualKeyboardNotifier&) = delete;

 private:
  VirtualKeyboardNotifier();
  friend struct base::DefaultSingletonTraits<VirtualKeyboardNotifier>;

  std::unordered_set<Observer*> observers_;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_VIRTUAL_KEYBOARD_NOTIFIER_H_
