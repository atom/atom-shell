// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_NODE_BINDINGS_WIN_H_
#define ELECTRON_SHELL_COMMON_NODE_BINDINGS_WIN_H_

#include "base/compiler_specific.h"
#include "shell/common/node_bindings.h"

namespace electron {

class NodeBindingsWin : public NodeBindings {
 public:
  explicit NodeBindingsWin(BrowserEnvironment browser_env);

 private:
  // NodeBindings
  void PollEvents() override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_COMMON_NODE_BINDINGS_WIN_H_
