// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_GLOBAL_SHORTCUT_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_GLOBAL_SHORTCUT_H_

#include <map>
#include <vector>

#include "base/functional/callback.h"
#include "chrome/browser/extensions/global_shortcut_listener.h"
#include "gin/handle.h"
#include "gin/wrappable.h"
#include "ui/base/accelerators/accelerator.h"

namespace electron::api {

class GlobalShortcut : private extensions::GlobalShortcutListener::Observer,
                       public gin::Wrappable<GlobalShortcut> {
 public:
  static gin::Handle<GlobalShortcut> Create(v8::Isolate* isolate);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

  // disable copy
  GlobalShortcut(const GlobalShortcut&) = delete;
  GlobalShortcut& operator=(const GlobalShortcut&) = delete;

 protected:
  explicit GlobalShortcut(v8::Isolate* isolate);
  ~GlobalShortcut() override;

 private:
  typedef std::map<ui::Accelerator, base::RepeatingClosure>
      AcceleratorCallbackMap;

  bool RegisterAll(const std::vector<ui::Accelerator>& accelerators,
                   const base::RepeatingClosure& callback);
  bool Register(const ui::Accelerator& accelerator,
                const base::RepeatingClosure& callback);
  bool IsRegistered(const ui::Accelerator& accelerator);
  void Unregister(const ui::Accelerator& accelerator);
  void UnregisterSome(const std::vector<ui::Accelerator>& accelerators);
  void UnregisterAll();

  // GlobalShortcutListener::Observer implementation.
  void OnKeyPressed(const ui::Accelerator& accelerator) override;

  AcceleratorCallbackMap accelerator_callback_map_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_GLOBAL_SHORTCUT_H_
