// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/node_bindings_linux.h"

#include <sys/epoll.h>

namespace electron {

NodeBindingsLinux::NodeBindingsLinux(BrowserEnvironment browser_env)
    : NodeBindings(browser_env), epoll_(epoll_create(1)) {
  auto* const event_loop = uv_loop();

  int backend_fd = uv_backend_fd(event_loop);
  struct epoll_event ev = {0};
  ev.events = EPOLLIN;
  ev.data.fd = backend_fd;
  epoll_ctl(epoll_, EPOLL_CTL_ADD, backend_fd, &ev);
}

void NodeBindingsLinux::PollEvents() {
  auto* const event_loop = uv_loop();

  int timeout = uv_backend_timeout(event_loop);

  // Wait for new libuv events.
  int r;
  do {
    struct epoll_event ev;
    r = epoll_wait(epoll_, &ev, 1, timeout);
  } while (r == -1 && errno == EINTR);
}

// static
std::unique_ptr<NodeBindings> NodeBindings::Create(BrowserEnvironment env) {
  return std::make_unique<NodeBindingsLinux>(env);
}

}  // namespace electron
