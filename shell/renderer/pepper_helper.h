// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_RENDERER_PEPPER_HELPER_H_
#define ELECTRON_SHELL_RENDERER_PEPPER_HELPER_H_

#include "base/compiler_specific.h"
#include "base/component_export.h"
#include "content/public/renderer/render_frame_observer.h"

// This class listens for Pepper creation events from the RenderFrame and
// attaches the parts required for plugin support.
class PepperHelper : private content::RenderFrameObserver {
 public:
  explicit PepperHelper(content::RenderFrame* render_frame);
  ~PepperHelper() override;

  // disable copy
  PepperHelper(const PepperHelper&) = delete;
  PepperHelper& operator=(const PepperHelper&) = delete;

 private:
  // RenderFrameObserver.
  void DidCreatePepperPlugin(content::RendererPpapiHost* host) override;
  void OnDestruct() override;
};

#endif  // ELECTRON_SHELL_RENDERER_PEPPER_HELPER_H_
