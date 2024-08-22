// Copyright (c) 2019 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/extensions/api/tabs/tabs_api.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/strings/pattern.h"
#include "base/types/expected_macros.h"
#include "chrome/common/url_constants.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/navigation_entry.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/mojom/host_id.mojom.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/switches.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/native_window.h"
#include "shell/browser/web_contents_zoom_controller.h"
#include "shell/browser/window_list.h"
#include "shell/common/extensions/api/tabs.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "url/gurl.h"

using electron::WebContentsZoomController;

namespace extensions {

namespace tabs = api::tabs;

const char kFrameNotFoundError[] = "No frame with id * in tab *.";
const char kPerOriginOnlyInAutomaticError[] =
    "Can only set scope to "
    "\"per-origin\" in \"automatic\" mode.";

using api::extension_types::InjectDetails;

namespace {
void ZoomModeToZoomSettings(WebContentsZoomController::ZoomMode zoom_mode,
                            api::tabs::ZoomSettings* zoom_settings) {
  DCHECK(zoom_settings);
  switch (zoom_mode) {
    case WebContentsZoomController::ZOOM_MODE_DEFAULT:
      zoom_settings->mode = api::tabs::ZoomSettingsMode::kAutomatic;
      zoom_settings->scope = api::tabs::ZoomSettingsScope::kPerOrigin;
      break;
    case WebContentsZoomController::ZOOM_MODE_ISOLATED:
      zoom_settings->mode = api::tabs::ZoomSettingsMode::kAutomatic;
      zoom_settings->scope = api::tabs::ZoomSettingsScope::kPerTab;
      break;
    case WebContentsZoomController::ZOOM_MODE_MANUAL:
      zoom_settings->mode = api::tabs::ZoomSettingsMode::kManual;
      zoom_settings->scope = api::tabs::ZoomSettingsScope::kPerTab;
      break;
    case WebContentsZoomController::ZOOM_MODE_DISABLED:
      zoom_settings->mode = api::tabs::ZoomSettingsMode::kDisabled;
      zoom_settings->scope = api::tabs::ZoomSettingsScope::kPerTab;
      break;
  }
}

// Returns true if either |boolean| is disengaged, or if |boolean| and
// |value| are equal. This function is used to check if a tab's parameters match
// those of the browser.
bool MatchesBool(const std::optional<bool>& boolean, bool value) {
  return !boolean || *boolean == value;
}

api::tabs::MutedInfo CreateMutedInfo(content::WebContents* contents) {
  DCHECK(contents);
  api::tabs::MutedInfo info;
  info.muted = contents->IsAudioMuted();
  info.reason = api::tabs::MutedInfoReason::kUser;
  return info;
}

}  // namespace

ExecuteCodeInTabFunction::ExecuteCodeInTabFunction() : execute_tab_id_(-1) {}

ExecuteCodeInTabFunction::~ExecuteCodeInTabFunction() = default;

ExecuteCodeFunction::InitResult ExecuteCodeInTabFunction::Init() {
  if (init_result_)
    return init_result_.value();

  if (args().size() < 2)
    return set_init_result(VALIDATION_FAILURE);

  const auto& tab_id_value = args()[0];
  // |tab_id| is optional so it's ok if it's not there.
  int tab_id = -1;
  if (tab_id_value.is_int()) {
    // But if it is present, it needs to be non-negative.
    tab_id = tab_id_value.GetInt();
    if (tab_id < 0) {
      return set_init_result(VALIDATION_FAILURE);
    }
  }

  // |details| are not optional.
  const base::Value& details_value = args()[1];
  if (!details_value.is_dict())
    return set_init_result(VALIDATION_FAILURE);
  auto details = InjectDetails::FromValue(details_value.GetDict());
  if (!details) {
    return set_init_result(VALIDATION_FAILURE);
  }

  if (tab_id == -1) {
    // There's no useful concept of a "default tab" in Electron.
    // TODO(nornagon): we could potentially kick this to an event to allow the
    // app to decide what "default tab" means for them?
    return set_init_result(VALIDATION_FAILURE);
  }

  execute_tab_id_ = tab_id;
  details_ = std::move(details);
  set_host_id(
      mojom::HostID(mojom::HostID::HostType::kExtensions, extension()->id()));
  return set_init_result(SUCCESS);
}

bool ExecuteCodeInTabFunction::CanExecuteScriptOnPage(std::string* error) {
  // If |tab_id| is specified, look for the tab. Otherwise default to selected
  // tab in the current window.
  CHECK_GE(execute_tab_id_, 0);
  auto* contents = electron::api::WebContents::FromID(execute_tab_id_);
  if (!contents) {
    return false;
  }

  int frame_id = details_->frame_id ? *details_->frame_id
                                    : ExtensionApiFrameIdMap::kTopFrameId;
  content::RenderFrameHost* rfh =
      ExtensionApiFrameIdMap::GetRenderFrameHostById(contents->web_contents(),
                                                     frame_id);
  if (!rfh) {
    *error = ErrorUtils::FormatErrorMessage(
        kFrameNotFoundError, base::NumberToString(frame_id),
        base::NumberToString(execute_tab_id_));
    return false;
  }

  // Content scripts declared in manifest.json can access frames at about:-URLs
  // if the extension has permission to access the frame's origin, so also allow
  // programmatic content scripts at about:-URLs for allowed origins.
  GURL effective_document_url(rfh->GetLastCommittedURL());
  bool is_about_url = effective_document_url.SchemeIs(url::kAboutScheme);
  if (is_about_url && details_->match_about_blank &&
      *details_->match_about_blank) {
    effective_document_url = GURL(rfh->GetLastCommittedOrigin().Serialize());
  }

  if (!effective_document_url.is_valid()) {
    // Unknown URL, e.g. because no load was committed yet. Allow for now, the
    // renderer will check again and fail the injection if needed.
    return true;
  }

  // NOTE: This can give the wrong answer due to race conditions, but it is OK,
  // we check again in the renderer.
  if (!extension()->permissions_data()->CanAccessPage(effective_document_url,
                                                      execute_tab_id_, error)) {
    if (is_about_url &&
        extension()->permissions_data()->active_permissions().HasAPIPermission(
            extensions::mojom::APIPermissionID::kTab)) {
      *error = ErrorUtils::FormatErrorMessage(
          manifest_errors::kCannotAccessAboutUrl,
          rfh->GetLastCommittedURL().spec(),
          rfh->GetLastCommittedOrigin().Serialize());
    }
    return false;
  }

  return true;
}

ScriptExecutor* ExecuteCodeInTabFunction::GetScriptExecutor(
    std::string* error) {
  auto* contents = electron::api::WebContents::FromID(execute_tab_id_);
  if (!contents)
    return nullptr;
  return contents->script_executor();
}

bool ExecuteCodeInTabFunction::IsWebView() const {
  return false;
}

const GURL& ExecuteCodeInTabFunction::GetWebViewSrc() const {
  return GURL::EmptyGURL();
}

bool TabsExecuteScriptFunction::ShouldInsertCSS() const {
  return false;
}

bool TabsExecuteScriptFunction::ShouldRemoveCSS() const {
  return false;
}

ExtensionFunction::ResponseAction TabsReloadFunction::Run() {
  std::optional<tabs::Reload::Params> params =
      tabs::Reload::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  bool bypass_cache = false;
  if (params->reload_properties && params->reload_properties->bypass_cache) {
    bypass_cache = *params->reload_properties->bypass_cache;
  }

  int tab_id = params->tab_id ? *params->tab_id : -1;
  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  contents->web_contents()->GetController().Reload(
      bypass_cache ? content::ReloadType::BYPASSING_CACHE
                   : content::ReloadType::NORMAL,
      true);

  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction TabsQueryFunction::Run() {
  std::optional<tabs::Query::Params> params =
      tabs::Query::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  URLPatternSet url_patterns;
  if (params->query_info.url) {
    std::vector<std::string> url_pattern_strings;
    if (params->query_info.url->as_string)
      url_pattern_strings.push_back(*params->query_info.url->as_string);
    else if (params->query_info.url->as_strings)
      url_pattern_strings.swap(*params->query_info.url->as_strings);
    // It is o.k. to use URLPattern::SCHEME_ALL here because this function does
    // not grant access to the content of the tabs, only to seeing their URLs
    // and meta data.
    std::string error;
    if (!url_patterns.Populate(url_pattern_strings, URLPattern::SCHEME_ALL,
                               true, &error)) {
      return RespondNow(Error(std::move(error)));
    }
  }

  std::string title = params->query_info.title.value_or(std::string());
  std::optional<bool> audible = params->query_info.audible;
  std::optional<bool> muted = params->query_info.muted;

  base::Value::List result;

  // Filter out webContents that don't belong to the current browser context.
  auto* bc = browser_context();
  auto all_contents = electron::api::WebContents::GetWebContentsList();
  all_contents.remove_if([&bc](electron::api::WebContents* wc) {
    return (bc != wc->web_contents()->GetBrowserContext());
  });

  for (auto* contents : all_contents) {
    if (!contents || !contents->web_contents())
      continue;

    auto* wc = contents->web_contents();

    // Match webContents audible value.
    if (!MatchesBool(audible, wc->IsCurrentlyAudible()))
      continue;

    // Match webContents muted value.
    if (!MatchesBool(muted, wc->IsAudioMuted()))
      continue;

    // Match webContents active status.
    if (!MatchesBool(params->query_info.active, contents->IsFocused()))
      continue;

    if (!title.empty() || !url_patterns.is_empty()) {
      // "title" and "url" properties are considered privileged data and can
      // only be checked if the extension has the "tabs" permission or it has
      // access to the WebContents's origin. Otherwise, this tab is considered
      // not matched.
      if (!extension()->permissions_data()->HasAPIPermissionForTab(
              contents->ID(), mojom::APIPermissionID::kTab) &&
          !extension()->permissions_data()->HasHostPermission(wc->GetURL())) {
        continue;
      }

      // Match webContents title.
      if (!title.empty() &&
          !base::MatchPattern(wc->GetTitle(), base::UTF8ToUTF16(title)))
        continue;

      // Match webContents url.
      if (!url_patterns.is_empty() && !url_patterns.MatchesURL(wc->GetURL()))
        continue;
    }

    tabs::Tab tab;
    tab.id = contents->ID();
    tab.title = base::UTF16ToUTF8(wc->GetTitle());
    tab.url = wc->GetLastCommittedURL().spec();
    tab.active = contents->IsFocused();
    tab.audible = contents->IsCurrentlyAudible();
    tab.muted_info = CreateMutedInfo(wc);

    result.Append(tab.ToValue());
  }

  return RespondNow(WithArguments(std::move(result)));
}

ExtensionFunction::ResponseAction TabsGetFunction::Run() {
  std::optional<tabs::Get::Params> params = tabs::Get::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  int tab_id = params->tab_id;

  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  tabs::Tab tab;
  tab.id = tab_id;

  // "title" and "url" properties are considered privileged data and can
  // only be checked if the extension has the "tabs" permission or it has
  // access to the WebContents's origin.
  auto* wc = contents->web_contents();
  if (extension()->permissions_data()->HasAPIPermissionForTab(
          contents->ID(), mojom::APIPermissionID::kTab) ||
      extension()->permissions_data()->HasHostPermission(wc->GetURL())) {
    tab.url = wc->GetLastCommittedURL().spec();
    tab.title = base::UTF16ToUTF8(wc->GetTitle());
  }

  tab.active = contents->IsFocused();
  tab.last_accessed = wc->GetLastActiveTime().InMillisecondsFSinceUnixEpoch();

  return RespondNow(ArgumentList(tabs::Get::Results::Create(std::move(tab))));
}

ExtensionFunction::ResponseAction TabsSetZoomFunction::Run() {
  std::optional<tabs::SetZoom::Params> params =
      tabs::SetZoom::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  int tab_id = params->tab_id ? *params->tab_id : -1;
  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  auto* web_contents = contents->web_contents();
  GURL url(web_contents->GetVisibleURL());
  std::string error;
  if (extension()->permissions_data()->IsRestrictedUrl(url, &error))
    return RespondNow(Error(error));

  auto* zoom_controller = contents->GetZoomController();
  double zoom_level = params->zoom_factor > 0
                          ? blink::ZoomFactorToZoomLevel(params->zoom_factor)
                          : blink::ZoomFactorToZoomLevel(
                                zoom_controller->default_zoom_factor());

  zoom_controller->SetZoomLevel(zoom_level);

  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction TabsGetZoomFunction::Run() {
  std::optional<tabs::GetZoomSettings::Params> params =
      tabs::GetZoomSettings::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  int tab_id = params->tab_id ? *params->tab_id : -1;
  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  double zoom_level = contents->GetZoomController()->GetZoomLevel();
  double zoom_factor = blink::ZoomLevelToZoomFactor(zoom_level);

  return RespondNow(ArgumentList(tabs::GetZoom::Results::Create(zoom_factor)));
}

ExtensionFunction::ResponseAction TabsGetZoomSettingsFunction::Run() {
  std::optional<tabs::GetZoomSettings::Params> params =
      tabs::GetZoomSettings::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  int tab_id = params->tab_id ? *params->tab_id : -1;
  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  auto* zoom_controller = contents->GetZoomController();
  WebContentsZoomController::ZoomMode zoom_mode =
      contents->GetZoomController()->zoom_mode();
  tabs::ZoomSettings zoom_settings;
  ZoomModeToZoomSettings(zoom_mode, &zoom_settings);
  zoom_settings.default_zoom_factor =
      blink::ZoomLevelToZoomFactor(zoom_controller->GetDefaultZoomLevel());

  return RespondNow(
      ArgumentList(tabs::GetZoomSettings::Results::Create(zoom_settings)));
}

ExtensionFunction::ResponseAction TabsSetZoomSettingsFunction::Run() {
  using tabs::ZoomSettings;

  std::optional<tabs::SetZoomSettings::Params> params =
      tabs::SetZoomSettings::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  int tab_id = params->tab_id ? *params->tab_id : -1;
  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  std::string error;
  GURL url(contents->web_contents()->GetVisibleURL());
  if (extension()->permissions_data()->IsRestrictedUrl(url, &error))
    return RespondNow(Error(error));

  // "per-origin" scope is only available in "automatic" mode.
  if (params->zoom_settings.scope == tabs::ZoomSettingsScope::kPerOrigin &&
      params->zoom_settings.mode != tabs::ZoomSettingsMode::kAutomatic &&
      params->zoom_settings.mode != tabs::ZoomSettingsMode::kNone) {
    return RespondNow(Error(kPerOriginOnlyInAutomaticError));
  }

  // Determine the correct internal zoom mode to set |web_contents| to from the
  // user-specified |zoom_settings|.
  WebContentsZoomController::ZoomMode zoom_mode =
      WebContentsZoomController::ZOOM_MODE_DEFAULT;
  switch (params->zoom_settings.mode) {
    case tabs::ZoomSettingsMode::kNone:
    case tabs::ZoomSettingsMode::kAutomatic:
      switch (params->zoom_settings.scope) {
        case tabs::ZoomSettingsScope::kNone:
        case tabs::ZoomSettingsScope::kPerOrigin:
          zoom_mode = WebContentsZoomController::ZOOM_MODE_DEFAULT;
          break;
        case tabs::ZoomSettingsScope::kPerTab:
          zoom_mode = WebContentsZoomController::ZOOM_MODE_ISOLATED;
      }
      break;
    case tabs::ZoomSettingsMode::kManual:
      zoom_mode = WebContentsZoomController::ZOOM_MODE_MANUAL;
      break;
    case tabs::ZoomSettingsMode::kDisabled:
      zoom_mode = WebContentsZoomController::ZOOM_MODE_DISABLED;
  }

  contents->GetZoomController()->SetZoomMode(zoom_mode);

  return RespondNow(NoArguments());
}

namespace {

bool IsKillURL(const GURL& url) {
#if DCHECK_IS_ON()
  // Caller should ensure that |url| is already "fixed up" by
  // url_formatter::FixupURL, which (among many other things) takes care
  // of rewriting about:kill into chrome://kill/.
  if (url.SchemeIs(url::kAboutScheme))
    DCHECK(url.IsAboutBlank() || url.IsAboutSrcdoc());
#endif

  // Disallow common renderer debug URLs.
  // Note: this would also disallow JavaScript URLs, but we already explicitly
  // check for those before calling into here from PrepareURLForNavigation.
  if (blink::IsRendererDebugURL(url)) {
    return true;
  }

  if (!url.SchemeIs(content::kChromeUIScheme)) {
    return false;
  }

  // Also disallow a few more hosts which are not covered by the check above.
  static const char* const kKillHosts[] = {
      chrome::kChromeUIDelayedHangUIHost, chrome::kChromeUIHangUIHost,
      chrome::kChromeUIQuitHost,          chrome::kChromeUIRestartHost,
      content::kChromeUIBrowserCrashHost, content::kChromeUIMemoryExhaustHost,
  };

  return base::Contains(kKillHosts, url.host_piece());
}

GURL ResolvePossiblyRelativeURL(const std::string& url_string,
                                const Extension* extension) {
  GURL url = GURL(url_string);
  if (!url.is_valid() && extension)
    url = extension->GetResourceURL(url_string);

  return url;
}

bool AllowFileAccess(const ExtensionId& extension_id,
                     content::BrowserContext* context) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kDisableExtensionsFileAccessCheck) ||
         ExtensionPrefs::Get(context)->AllowFileAccess(extension_id);
}

base::expected<GURL, std::string> PrepareURLForNavigation(
    const std::string& url_string,
    const Extension* extension,
    content::BrowserContext* browser_context) {
  GURL url = ResolvePossiblyRelativeURL(url_string, extension);

  // Ideally, the URL would only be "fixed" for user input (e.g. for URLs
  // entered into the Omnibox), but some extensions rely on the legacy behavior
  // where all navigations were subject to the "fixing".  See also
  // https://crbug.com/1145381.
  url = url_formatter::FixupURL(url.spec(), "" /* = desired_tld */);

  // Reject invalid URLs.
  if (!url.is_valid()) {
    const char kInvalidUrlError[] = "Invalid url: \"*\".";
    return base::unexpected(
        ErrorUtils::FormatErrorMessage(kInvalidUrlError, url_string));
  }

  // Don't let the extension use JavaScript URLs in API triggered navigations.
  if (url.SchemeIs(url::kJavaScriptScheme)) {
    const char kJavaScriptUrlsNotAllowedInExtensionNavigations[] =
        "JavaScript URLs are not allowed in API based extension navigations. "
        "Use "
        "chrome.scripting.executeScript instead.";
    return base::unexpected(kJavaScriptUrlsNotAllowedInExtensionNavigations);
  }

  // Don't let the extension crash the browser or renderers.
  if (IsKillURL(url)) {
    const char kNoCrashBrowserError[] =
        "I'm sorry. I'm afraid I can't do that.";
    return base::unexpected(kNoCrashBrowserError);
  }

  // Don't let the extension navigate directly to devtools scheme pages, unless
  // they have applicable permissions.
  if (url.SchemeIs(content::kChromeDevToolsScheme)) {
    bool has_permission =
        extension && (extension->permissions_data()->HasAPIPermission(
                          mojom::APIPermissionID::kDevtools) ||
                      extension->permissions_data()->HasAPIPermission(
                          mojom::APIPermissionID::kDebugger));
    if (!has_permission) {
      const char kCannotNavigateToDevtools[] =
          "Cannot navigate to a devtools:// page without either the devtools "
          "or "
          "debugger permission.";
      return base::unexpected(kCannotNavigateToDevtools);
    }
  }

  // Don't let the extension navigate directly to chrome-untrusted scheme pages.
  if (url.SchemeIs(content::kChromeUIUntrustedScheme)) {
    const char kCannotNavigateToChromeUntrusted[] =
        "Cannot navigate to a chrome-untrusted:// page.";
    return base::unexpected(kCannotNavigateToChromeUntrusted);
  }

  // Don't let the extension navigate directly to file scheme pages, unless
  // they have file access.
  if (url.SchemeIsFile() &&
      !AllowFileAccess(extension->id(), browser_context) &&
      base::FeatureList::IsEnabled(
          extensions_features::kRestrictFileURLNavigation)) {
    const char kFileUrlsNotAllowedInExtensionNavigations[] =
        "Cannot navigate to a file URL without local file access.";
    return base::unexpected(kFileUrlsNotAllowedInExtensionNavigations);
  }

  return url;
}

}  // namespace

TabsUpdateFunction::TabsUpdateFunction() : web_contents_(nullptr) {}

ExtensionFunction::ResponseAction TabsUpdateFunction::Run() {
  std::optional<tabs::Update::Params> params =
      tabs::Update::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  int tab_id = params->tab_id ? *params->tab_id : -1;
  auto* contents = electron::api::WebContents::FromID(tab_id);
  if (!contents)
    return RespondNow(Error("No such tab"));

  web_contents_ = contents->web_contents();

  // Navigate the tab to a new location if the url is different.
  std::string error;
  if (params->update_properties.url) {
    std::string updated_url = *params->update_properties.url;
    if (!UpdateURL(updated_url, tab_id, &error))
      return RespondNow(Error(std::move(error)));
  }

  if (params->update_properties.muted) {
    contents->SetAudioMuted(*params->update_properties.muted);
  }

  return RespondNow(GetResult());
}

bool TabsUpdateFunction::UpdateURL(const std::string& url_string,
                                   int tab_id,
                                   std::string* error) {
  auto url =
      PrepareURLForNavigation(url_string, extension(), browser_context());
  if (!url.has_value()) {
    *error = std::move(url.error());
    return false;
  }

  content::NavigationController::LoadURLParams load_params(*url);

  // Treat extension-initiated navigations as renderer-initiated so that the URL
  // does not show in the omnibox until it commits.  This avoids URL spoofs
  // since URLs can be opened on behalf of untrusted content.
  load_params.is_renderer_initiated = true;
  // All renderer-initiated navigations need to have an initiator origin.
  load_params.initiator_origin = extension()->origin();
  // |source_site_instance| needs to be set so that a renderer process
  // compatible with |initiator_origin| is picked by Site Isolation.
  load_params.source_site_instance = content::SiteInstance::CreateForURL(
      web_contents_->GetBrowserContext(),
      load_params.initiator_origin->GetURL());

  // Marking the navigation as initiated via an API means that the focus
  // will stay in the omnibox - see https://crbug.com/1085779.
  load_params.transition_type = ui::PAGE_TRANSITION_FROM_API;

  web_contents_->GetController().LoadURLWithParams(load_params);

  DCHECK_EQ(url,
            web_contents_->GetController().GetPendingEntry()->GetVirtualURL());

  return true;
}

ExtensionFunction::ResponseValue TabsUpdateFunction::GetResult() {
  if (!has_callback())
    return NoArguments();

  tabs::Tab tab;

  auto* api_web_contents = electron::api::WebContents::From(web_contents_);
  tab.id = (api_web_contents ? api_web_contents->ID() : -1);

  // "title" and "url" properties are considered privileged data and can
  // only be checked if the extension has the "tabs" permission or it has
  // access to the WebContents's origin.
  if (extension()->permissions_data()->HasAPIPermissionForTab(
          api_web_contents->ID(), mojom::APIPermissionID::kTab) ||
      extension()->permissions_data()->HasHostPermission(
          web_contents_->GetURL())) {
    tab.url = web_contents_->GetLastCommittedURL().spec();
    tab.title = base::UTF16ToUTF8(web_contents_->GetTitle());
  }

  if (api_web_contents)
    tab.active = api_web_contents->IsFocused();
  tab.muted_info = CreateMutedInfo(web_contents_);
  tab.audible = web_contents_->IsCurrentlyAudible();

  return ArgumentList(tabs::Get::Results::Create(std::move(tab)));
}

}  // namespace extensions
