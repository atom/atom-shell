// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#pragma once

#include "atom/browser/api/atom_api_menu.h"
#include "ui/display/screen.h"

namespace atom {

namespace api {

class MenuViews : public Menu {
 public:
  MenuViews(v8::Isolate* isolate, v8::Local<v8::Object> wrapper);

 protected:
  void PopupAt(Window* window, int x, int y, int positioning_item) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MenuViews);
};

}  // namespace api

}  // namespace atom
