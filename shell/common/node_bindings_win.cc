// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/node_bindings_win.h"

#include <windows.h>

#include "base/logging.h"
#include "base/system/sys_info.h"

namespace electron {

NodeBindingsWin::NodeBindingsWin(BrowserEnvironment browser_env)
    : NodeBindings(browser_env) {
  auto* const event_loop = uv_loop();

  // on single-core the io comp port NumberOfConcurrentThreads needs to be 2
  // to avoid cpu pegging likely caused by a busy loop in PollEvents
  if (base::SysInfo::NumberOfProcessors() == 1) {
    // the expectation is the event_loop has just been initialized
    // which makes iocp replacement safe
    CHECK_EQ(0u, event_loop->active_handles);
    CHECK_EQ(0u, event_loop->active_reqs.count);

    if (event_loop->iocp && event_loop->iocp != INVALID_HANDLE_VALUE)
      CloseHandle(event_loop->iocp);
    event_loop->iocp =
        CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 2);
  }
}

void NodeBindingsWin::PollEvents() {
  auto* const event_loop = uv_loop();

  // If there are other kinds of events pending, uv_backend_timeout will
  // instruct us not to wait.
  DWORD bytes, timeout;
  ULONG_PTR key;
  OVERLAPPED* overlapped;

  timeout = uv_backend_timeout(event_loop);

  GetQueuedCompletionStatus(event_loop->iocp, &bytes, &key, &overlapped,
                            timeout);

  // Give the event back so libuv can deal with it.
  if (overlapped != nullptr)
    PostQueuedCompletionStatus(event_loop->iocp, bytes, key, overlapped);
}

// static
std::unique_ptr<NodeBindings> NodeBindings::Create(BrowserEnvironment env) {
  return std::make_unique<NodeBindingsWin>(env);
}

}  // namespace electron
