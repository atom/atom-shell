// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/content_settings_observer.h"

#include "base/command_line.h"
#include "content/public/renderer/render_frame.h"
#include "shell/common/options_switches.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/platform/url_conversion.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace electron {

ContentSettingsObserver::ContentSettingsObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {
  render_frame->GetWebFrame()->SetContentSettingsClient(this);
}

ContentSettingsObserver::~ContentSettingsObserver() = default;

bool ContentSettingsObserver::AllowStorageAccessSync(StorageType storage_type) {
  blink::WebFrame* frame = render_frame()->GetWebFrame();
  if (frame->GetSecurityOrigin().IsOpaque() ||
      frame->Top()->GetSecurityOrigin().IsOpaque())
    return false;
  auto origin = blink::WebStringToGURL(frame->GetSecurityOrigin().ToString());
  if (!origin.IsStandard())
    return false;
  return true;
}

void ContentSettingsObserver::OnDestruct() {
  delete this;
}

}  // namespace electron
