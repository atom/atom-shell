// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/web_contents_preferences.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/fixed_flat_map.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/base/switches.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/content_switches.h"
#include "net/base/filename_util.h"
#include "sandbox/policy/switches.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/native_window.h"
#include "shell/browser/session_preferences.h"
#include "shell/common/color_util.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/options_switches.h"
#include "shell/common/process_util.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/mojom/v8_cache_options.mojom.h"
#include "third_party/blink/public/mojom/webpreferences/web_preferences.mojom.h"

#if BUILDFLAG(IS_WIN)
#include "ui/gfx/switches.h"
#endif

namespace gin {

template <>
struct Converter<blink::mojom::AutoplayPolicy> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     blink::mojom::AutoplayPolicy* out) {
    using Val = blink::mojom::AutoplayPolicy;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"document-user-activation-required",
             Val::kDocumentUserActivationRequired},
            {"no-user-gesture-required", Val::kNoUserGestureRequired},
            {"user-gesture-required", Val::kUserGestureRequired},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

template <>
struct Converter<blink::mojom::V8CacheOptions> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     blink::mojom::V8CacheOptions* out) {
    using Val = blink::mojom::V8CacheOptions;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"bypassHeatCheck", Val::kCodeWithoutHeatCheck},
            {"bypassHeatCheckAndEagerCompile", Val::kFullCodeWithoutHeatCheck},
            {"code", Val::kCode},
            {"none", Val::kNone},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

}  // namespace gin

namespace electron {

namespace {
std::vector<WebContentsPreferences*>& Instances() {
  static base::NoDestructor<std::vector<WebContentsPreferences*>> g_instances;
  return *g_instances;
}
}  // namespace

WebContentsPreferences::WebContentsPreferences(
    content::WebContents* web_contents,
    const gin_helper::Dictionary& web_preferences)
    : content::WebContentsUserData<WebContentsPreferences>(*web_contents),
      web_contents_(web_contents) {
  web_contents->SetUserData(UserDataKey(), base::WrapUnique(this));
  Instances().push_back(this);
  SetFromDictionary(web_preferences);

  // If this is a <webview> tag, and the embedder is offscreen-rendered, then
  // this WebContents is also offscreen-rendered.
  if (auto* api_web_contents = api::WebContents::From(web_contents_)) {
    if (electron::api::WebContents* embedder = api_web_contents->embedder()) {
      auto* embedder_preferences =
          WebContentsPreferences::From(embedder->web_contents());
      if (embedder_preferences && embedder_preferences->IsOffscreen()) {
        offscreen_ = true;
      }
    }
  }
}

WebContentsPreferences::~WebContentsPreferences() {
  std::erase(Instances(), this);
}

void WebContentsPreferences::Clear() {
  plugins_ = false;
  experimental_features_ = false;
  node_integration_ = false;
  node_integration_in_sub_frames_ = false;
  node_integration_in_worker_ = false;
  disable_html_fullscreen_window_resize_ = false;
  webview_tag_ = false;
  sandbox_ = std::nullopt;
  context_isolation_ = true;
  javascript_ = true;
  images_ = true;
  text_areas_are_resizable_ = true;
  webgl_ = true;
  enable_preferred_size_mode_ = false;
  web_security_ = true;
  allow_running_insecure_content_ = false;
  offscreen_ = false;
  navigate_on_drag_drop_ = false;
  autoplay_policy_ = blink::mojom::AutoplayPolicy::kNoUserGestureRequired;
  default_font_family_.clear();
  default_font_size_ = std::nullopt;
  default_monospace_font_size_ = std::nullopt;
  minimum_font_size_ = std::nullopt;
  default_encoding_ = std::nullopt;
  is_webview_ = false;
  custom_args_.clear();
  custom_switches_.clear();
  enable_blink_features_ = std::nullopt;
  disable_blink_features_ = std::nullopt;
  disable_popups_ = false;
  disable_dialogs_ = false;
  safe_dialogs_ = false;
  safe_dialogs_message_ = std::nullopt;
  ignore_menu_shortcuts_ = false;
  background_color_ = std::nullopt;
  image_animation_policy_ =
      blink::mojom::ImageAnimationPolicy::kImageAnimationPolicyAllowed;
  preload_path_ = std::nullopt;
  v8_cache_options_ = blink::mojom::V8CacheOptions::kDefault;

#if BUILDFLAG(IS_MAC)
  scroll_bounce_ = false;
#endif
#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  spellcheck_ = true;
#endif
}

void WebContentsPreferences::SetFromDictionary(
    const gin_helper::Dictionary& web_preferences) {
  Clear();

  web_preferences.Get(options::kPlugins, &plugins_);
  web_preferences.Get(options::kExperimentalFeatures, &experimental_features_);
  web_preferences.Get(options::kNodeIntegration, &node_integration_);
  web_preferences.Get(options::kNodeIntegrationInSubFrames,
                      &node_integration_in_sub_frames_);
  web_preferences.Get(options::kNodeIntegrationInWorker,
                      &node_integration_in_worker_);
  web_preferences.Get(options::kDisableHtmlFullscreenWindowResize,
                      &disable_html_fullscreen_window_resize_);
  web_preferences.Get(options::kWebviewTag, &webview_tag_);
  bool sandbox;
  if (web_preferences.Get(options::kSandbox, &sandbox))
    sandbox_ = sandbox;
  web_preferences.Get(options::kContextIsolation, &context_isolation_);
  web_preferences.Get(options::kJavaScript, &javascript_);
  web_preferences.Get(options::kImages, &images_);
  web_preferences.Get(options::kTextAreasAreResizable,
                      &text_areas_are_resizable_);
  web_preferences.Get(options::kWebGL, &webgl_);
  web_preferences.Get(options::kEnablePreferredSizeMode,
                      &enable_preferred_size_mode_);
  web_preferences.Get(options::kWebSecurity, &web_security_);
  if (!web_preferences.Get(options::kAllowRunningInsecureContent,
                           &allow_running_insecure_content_) &&
      !web_security_)
    allow_running_insecure_content_ = true;
  web_preferences.Get(options::kOffscreen, &offscreen_);
  web_preferences.Get(options::kNavigateOnDragDrop, &navigate_on_drag_drop_);
  web_preferences.Get("autoplayPolicy", &autoplay_policy_);
  web_preferences.Get("defaultFontFamily", &default_font_family_);
  int size;
  if (web_preferences.Get("defaultFontSize", &size))
    default_font_size_ = size;
  if (web_preferences.Get("defaultMonospaceFontSize", &size))
    default_monospace_font_size_ = size;
  if (web_preferences.Get("minimumFontSize", &size))
    minimum_font_size_ = size;
  std::string encoding;
  if (web_preferences.Get("defaultEncoding", &encoding))
    default_encoding_ = encoding;
  web_preferences.Get(options::kCustomArgs, &custom_args_);
  web_preferences.Get("commandLineSwitches", &custom_switches_);
  web_preferences.Get("disablePopups", &disable_popups_);
  web_preferences.Get("disableDialogs", &disable_dialogs_);
  web_preferences.Get("safeDialogs", &safe_dialogs_);
  // preferences don't save a transparency option,
  // apply any existing transparency setting to background_color_
  bool transparent;
  if (web_preferences.Get(options::kTransparent, &transparent) && transparent) {
    background_color_ = SK_ColorTRANSPARENT;
  }
  std::string background_color;
  if (web_preferences.GetHidden(options::kBackgroundColor, &background_color))
    background_color_ = ParseCSSColor(background_color);
  std::string safe_dialogs_message;
  if (web_preferences.Get("safeDialogsMessage", &safe_dialogs_message))
    safe_dialogs_message_ = safe_dialogs_message;
  web_preferences.Get("ignoreMenuShortcuts", &ignore_menu_shortcuts_);
  std::string enable_blink_features;
  if (web_preferences.Get(options::kEnableBlinkFeatures,
                          &enable_blink_features))
    enable_blink_features_ = enable_blink_features;
  std::string disable_blink_features;
  if (web_preferences.Get(options::kDisableBlinkFeatures,
                          &disable_blink_features))
    disable_blink_features_ = disable_blink_features;

  base::FilePath::StringType preload_path;
  if (web_preferences.Get(options::kPreloadScript, &preload_path)) {
    base::FilePath preload(preload_path);
    if (preload.IsAbsolute()) {
      preload_path_ = preload;
    } else {
      LOG(ERROR) << "preload script must have absolute path.";
    }
  }

  std::string type;
  if (web_preferences.Get(options::kType, &type)) {
    is_webview_ = type == "webview";
  }

  web_preferences.Get("v8CacheOptions", &v8_cache_options_);

#if BUILDFLAG(IS_MAC)
  web_preferences.Get(options::kScrollBounce, &scroll_bounce_);
#endif

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  web_preferences.Get(options::kSpellcheck, &spellcheck_);
#endif

  SaveLastPreferences();
}

bool WebContentsPreferences::GetSafeDialogsMessage(std::string* message) const {
  if (safe_dialogs_message_) {
    *message = *safe_dialogs_message_;
    return true;
  }
  return false;
}

bool WebContentsPreferences::SetImageAnimationPolicy(std::string policy) {
  if (policy == "animate") {
    image_animation_policy_ =
        blink::mojom::ImageAnimationPolicy::kImageAnimationPolicyAllowed;
    return true;
  } else if (policy == "animateOnce") {
    image_animation_policy_ =
        blink::mojom::ImageAnimationPolicy::kImageAnimationPolicyAnimateOnce;
    return true;
  } else if (policy == "noAnimation") {
    image_animation_policy_ =
        blink::mojom::ImageAnimationPolicy::kImageAnimationPolicyNoAnimation;
    return true;
  }
  return false;
}

bool WebContentsPreferences::GetPreloadPath(base::FilePath* path) const {
  DCHECK(path);
  if (preload_path_) {
    *path = *preload_path_;
    return true;
  }
  return false;
}

bool WebContentsPreferences::IsSandboxed() const {
  if (sandbox_)
    return *sandbox_;
  bool sandbox_disabled_by_default =
      node_integration_ || node_integration_in_worker_;
  return !sandbox_disabled_by_default;
}

// static
content::WebContents* WebContentsPreferences::GetWebContentsFromProcessID(
    int process_id) {
  for (WebContentsPreferences* preferences : Instances()) {
    content::WebContents* web_contents = preferences->web_contents_;
    if (web_contents->GetPrimaryMainFrame()->GetProcess()->GetID() ==
        process_id)
      return web_contents;
  }
  return nullptr;
}

// static
WebContentsPreferences* WebContentsPreferences::From(
    content::WebContents* web_contents) {
  if (!web_contents)
    return nullptr;
  return FromWebContents(web_contents);
}

void WebContentsPreferences::AppendCommandLineSwitches(
    base::CommandLine* command_line,
    bool is_subframe) {
  // Experimental flags.
  if (experimental_features_)
    command_line->AppendSwitch(
        ::switches::kEnableExperimentalWebPlatformFeatures);

  // Sandbox can be enabled for renderer processes hosting cross-origin frames
  // unless nodeIntegrationInSubFrames is enabled
  bool can_sandbox_frame = is_subframe && !node_integration_in_sub_frames_;

  if (IsSandboxed() || can_sandbox_frame) {
    command_line->AppendSwitch(switches::kEnableSandbox);
  } else if (!command_line->HasSwitch(switches::kEnableSandbox)) {
    command_line->AppendSwitch(sandbox::policy::switches::kNoSandbox);
    command_line->AppendSwitch(::switches::kNoZygote);
  }

#if BUILDFLAG(IS_MAC)
  // Enable scroll bounce.
  if (scroll_bounce_)
    command_line->AppendSwitch(switches::kScrollBounce);
#endif

  // Custom args for renderer process
  for (const auto& arg : custom_args_)
    if (!arg.empty())
      command_line->AppendArg(arg);

  // Custom command line switches.
  for (const auto& arg : custom_switches_)
    if (!arg.empty())
      command_line->AppendSwitch(arg);

  if (enable_blink_features_)
    command_line->AppendSwitchASCII(::switches::kEnableBlinkFeatures,
                                    *enable_blink_features_);
  if (disable_blink_features_)
    command_line->AppendSwitchASCII(::switches::kDisableBlinkFeatures,
                                    *disable_blink_features_);

  if (node_integration_in_worker_)
    command_line->AppendSwitch(switches::kNodeIntegrationInWorker);

  // We are appending args to a webContents so let's save the current state
  // of our preferences object so that during the lifetime of the WebContents
  // we can fetch the options used to initially configure the WebContents
  // last_preference_ = preference_.Clone();
  SaveLastPreferences();
}

void WebContentsPreferences::SaveLastPreferences() {
  base::Value::Dict dict;
  dict.Set(options::kNodeIntegration, node_integration_);
  dict.Set(options::kNodeIntegrationInSubFrames,
           node_integration_in_sub_frames_);
  dict.Set(options::kSandbox, IsSandboxed());
  dict.Set(options::kContextIsolation, context_isolation_);
  dict.Set(options::kJavaScript, javascript_);
  dict.Set(options::kWebviewTag, webview_tag_);
  dict.Set("disablePopups", disable_popups_);
  dict.Set(options::kWebSecurity, web_security_);
  dict.Set(options::kAllowRunningInsecureContent,
           allow_running_insecure_content_);
  dict.Set(options::kExperimentalFeatures, experimental_features_);
  dict.Set(options::kEnableBlinkFeatures, enable_blink_features_.value_or(""));
  dict.Set("disableDialogs", disable_dialogs_);
  dict.Set("safeDialogs", safe_dialogs_);
  dict.Set("safeDialogsMessage", safe_dialogs_message_.value_or(""));

  last_web_preferences_ = base::Value(std::move(dict));
}

void WebContentsPreferences::OverrideWebkitPrefs(
    blink::web_pref::WebPreferences* prefs,
    blink::RendererPreferences* renderer_prefs) {
  prefs->javascript_enabled = javascript_;
  prefs->images_enabled = images_;
  prefs->animation_policy = image_animation_policy_;
  prefs->text_areas_are_resizable = text_areas_are_resizable_;
  prefs->autoplay_policy = autoplay_policy_;

  // TODO: navigate_on_drag_drop was removed from web prefs in favor of the
  // equivalent option in renderer prefs. this option should be deprecated from
  // our API and then removed here.
  renderer_prefs->can_accept_load_drops = navigate_on_drag_drop_;

  // Check if webgl should be enabled.
  prefs->webgl1_enabled = webgl_;
  prefs->webgl2_enabled = webgl_;

  // Check if web security should be enabled.
  prefs->web_security_enabled = web_security_;
  prefs->allow_running_insecure_content = allow_running_insecure_content_;

  if (!default_font_family_.empty()) {
    if (auto iter = default_font_family_.find("standard");
        iter != default_font_family_.end())
      prefs->standard_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
    if (auto iter = default_font_family_.find("serif");
        iter != default_font_family_.end())
      prefs->serif_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
    if (auto iter = default_font_family_.find("sansSerif");
        iter != default_font_family_.end())
      prefs->sans_serif_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
    if (auto iter = default_font_family_.find("monospace");
        iter != default_font_family_.end())
      prefs->fixed_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
    if (auto iter = default_font_family_.find("cursive");
        iter != default_font_family_.end())
      prefs->cursive_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
    if (auto iter = default_font_family_.find("fantasy");
        iter != default_font_family_.end())
      prefs->fantasy_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
    if (auto iter = default_font_family_.find("math");
        iter != default_font_family_.end())
      prefs->math_font_family_map[blink::web_pref::kCommonScript] =
          iter->second;
  }

  if (default_font_size_)
    prefs->default_font_size = *default_font_size_;
  if (default_monospace_font_size_)
    prefs->default_fixed_font_size = *default_monospace_font_size_;
  if (minimum_font_size_)
    prefs->minimum_font_size = *minimum_font_size_;
  if (default_encoding_)
    prefs->default_encoding = *default_encoding_;

  // Run Electron APIs and preload script in isolated world
  prefs->context_isolation = context_isolation_;
  prefs->is_webview = is_webview_;

  prefs->hidden_page = false;
  // Webview `document.visibilityState` tracks window visibility so we need
  // to let it know if the window happens to be hidden right now.
  if (auto* api_web_contents = api::WebContents::From(web_contents_)) {
    if (electron::api::WebContents* embedder = api_web_contents->embedder()) {
      if (auto* relay =
              NativeWindowRelay::FromWebContents(embedder->web_contents())) {
        if (auto* window = relay->GetNativeWindow()) {
          const bool visible = window->IsVisible() && !window->IsMinimized();
          if (!visible) {
            prefs->hidden_page = true;
          }
        }
      }
    }
  }

  prefs->offscreen = offscreen_;

  prefs->node_integration = node_integration_;
  prefs->node_integration_in_worker = node_integration_in_worker_;
  prefs->node_integration_in_sub_frames = node_integration_in_sub_frames_;

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  prefs->enable_spellcheck = spellcheck_;
#endif

  prefs->enable_plugins = plugins_;
  prefs->webview_tag = webview_tag_;

  prefs->v8_cache_options = v8_cache_options_;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(WebContentsPreferences);

}  // namespace electron
