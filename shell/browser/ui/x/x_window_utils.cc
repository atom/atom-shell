// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/x/x_window_utils.h"

#include <memory>

#include "base/environment.h"
#include "base/strings/string_util.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "shell/common/thread_restrictions.h"
#include "ui/base/x/x11_util.h"
#include "ui/gfx/x/atom_cache.h"
#include "ui/gfx/x/connection.h"
#include "ui/gfx/x/xproto.h"

namespace electron {

void SetWindowType(x11::Window window, const std::string& type) {
  std::string type_prefix = "_NET_WM_WINDOW_TYPE_";
  std::string window_type_str = type_prefix + base::ToUpperASCII(type);
  x11::Atom window_type = x11::GetAtom(window_type_str.c_str());
  auto* connection = x11::Connection::Get();
  connection->SetProperty(window, x11::GetAtom("_NET_WM_WINDOW_TYPE"),
                          x11::Atom::ATOM, window_type);
}

bool ShouldUseGlobalMenuBar() {
  ScopedAllowBlockingForElectron allow_blocking;
  auto env = base::Environment::Create();
  if (env->HasVar("ELECTRON_FORCE_WINDOW_MENU_BAR"))
    return false;

  dbus::Bus::Options options;
  auto bus = base::MakeRefCounted<dbus::Bus>(options);

  dbus::ObjectProxy* object_proxy =
      bus->GetObjectProxy(DBUS_SERVICE_DBUS, dbus::ObjectPath(DBUS_PATH_DBUS));
  dbus::MethodCall method_call(DBUS_INTERFACE_DBUS, "ListNames");
  std::unique_ptr<dbus::Response> response =
      object_proxy
          ->CallMethodAndBlock(&method_call,
                               dbus::ObjectProxy::TIMEOUT_USE_DEFAULT)
          .value_or(nullptr);
  if (!response) {
    bus->ShutdownAndBlock();
    return false;
  }

  dbus::MessageReader reader(response.get());
  dbus::MessageReader array_reader(nullptr);
  if (!reader.PopArray(&array_reader)) {
    bus->ShutdownAndBlock();
    return false;
  }
  while (array_reader.HasMoreData()) {
    std::string name;
    if (array_reader.PopString(&name) &&
        name == "com.canonical.AppMenu.Registrar") {
      bus->ShutdownAndBlock();
      return true;
    }
  }

  bus->ShutdownAndBlock();
  return false;
}

void MoveWindowToForeground(x11::Window window) {
  MoveWindowAbove(window, static_cast<x11::Window>(0));
}

void MoveWindowAbove(x11::Window window, x11::Window other_window) {
  ui::SendClientMessage(window, ui::GetX11RootWindow(),
                        x11::GetAtom("_NET_RESTACK_WINDOW"),
                        {2, static_cast<uint32_t>(other_window),
                         static_cast<uint32_t>(x11::StackMode::Above), 0, 0});
}

bool IsWindowValid(x11::Window window) {
  auto* conn = x11::Connection::Get();
  return conn->GetWindowAttributes({window}).Sync();
}

}  // namespace electron
