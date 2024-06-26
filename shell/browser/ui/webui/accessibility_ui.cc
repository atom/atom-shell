// Copyright (c) 2020 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/webui/accessibility_ui.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/pattern.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/accessibility_resources.h"      // nogncheck
#include "chrome/grit/accessibility_resources_map.h"  // nogncheck
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/ax_inspect_factory.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_ui_data_source.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/native_window.h"
#include "shell/browser/window_list.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/ax_updates_and_events.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/accessibility/widget_ax_tree_id_map.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace {

static const char kTargetsDataFile[] = "targets-data.json";

static const char kAccessibilityModeField[] = "a11yMode";
static const char kBrowsersField[] = "browsers";
static const char kErrorField[] = "error";
static const char kFaviconUrlField[] = "faviconUrl";
static const char kNameField[] = "name";
static const char kPagesField[] = "pages";
static const char kPidField[] = "pid";
static const char kProcessIdField[] = "processId";
static const char kRequestTypeField[] = "requestType";
static const char kRoutingIdField[] = "routingId";
static const char kSessionIdField[] = "sessionId";
static const char kSupportedApiTypesField[] = "supportedApiTypes";
static const char kTreeField[] = "tree";
static const char kTypeField[] = "type";
static const char kUrlField[] = "url";
static const char kWidgetsField[] = "widgets";
static const char kApiTypeField[] = "apiType";

#if defined(USE_AURA)
static const char kWidgetIdField[] = "widgetId";
static const char kWidget[] = "widget";
#endif

// Global flags
static const char kBrowser[] = "browser";
static const char kCopyTree[] = "copyTree";
static const char kHTML[] = "html";
static const char kLocked[] = "locked";
static const char kNative[] = "native";
static const char kPage[] = "page";
static const char kPDFPrinting[] = "pdfPrinting";
static const char kScreenReader[] = "screenreader";
static const char kShowOrRefreshTree[] = "showOrRefreshTree";
static const char kText[] = "text";
static const char kViewsAccessibility[] = "viewsAccessibility";
static const char kWeb[] = "web";

// Possible global flag values
static const char kDisabled[] = "disabled";
static const char kOff[] = "off";
static const char kOn[] = "on";

base::Value::Dict BuildTargetDescriptor(
    const GURL& url,
    const std::string& name,
    const GURL& favicon_url,
    int process_id,
    int routing_id,
    ui::AXMode accessibility_mode,
    base::ProcessHandle handle = base::kNullProcessHandle) {
  base::Value::Dict target_data;
  target_data.Set(kProcessIdField, process_id);
  target_data.Set(kRoutingIdField, routing_id);
  target_data.Set(kUrlField, url.spec());
  target_data.Set(kNameField, base::EscapeForHTML(name));
  target_data.Set(kPidField, static_cast<int>(base::GetProcId(handle)));
  target_data.Set(kFaviconUrlField, favicon_url.spec());
  target_data.Set(kAccessibilityModeField,
                  static_cast<int>(accessibility_mode.flags()));
  target_data.Set(kTypeField, kPage);
  return target_data;
}

base::Value::Dict BuildTargetDescriptor(content::RenderViewHost* rvh) {
  content::WebContents* web_contents =
      content::WebContents::FromRenderViewHost(rvh);
  ui::AXMode accessibility_mode;

  std::string title;
  GURL url;
  GURL favicon_url;
  if (web_contents) {
    url = web_contents->GetURL();
    title = base::UTF16ToUTF8(web_contents->GetTitle());
    content::NavigationController& controller = web_contents->GetController();
    content::NavigationEntry* entry = controller.GetVisibleEntry();
    if (entry != nullptr && entry->GetURL().is_valid()) {
      gfx::Image favicon_image = entry->GetFavicon().image;
      if (!favicon_image.IsEmpty()) {
        const SkBitmap* favicon_bitmap = favicon_image.ToSkBitmap();
        favicon_url = GURL(webui::GetBitmapDataUrl(*favicon_bitmap));
      }
    }
    accessibility_mode = web_contents->GetAccessibilityMode();
  }

  return BuildTargetDescriptor(url, title, favicon_url,
                               rvh->GetProcess()->GetID(), rvh->GetRoutingID(),
                               accessibility_mode);
}

base::Value::Dict BuildTargetDescriptor(electron::NativeWindow* window) {
  base::Value::Dict target_data;
  target_data.Set(kSessionIdField, window->window_id());
  target_data.Set(kNameField, window->GetTitle());
  target_data.Set(kTypeField, kBrowser);
  return target_data;
}

#if defined(USE_AURA)
base::Value::Dict BuildTargetDescriptor(views::Widget* widget) {
  base::Value::Dict widget_data;
  widget_data.Set(kNameField, widget->widget_delegate()->GetWindowTitle());
  widget_data.Set(kTypeField, kWidget);

  // Use the Widget's root view ViewAccessibility's unique ID for lookup.
  int id = widget->GetRootView()->GetViewAccessibility().GetUniqueId().Get();
  widget_data.Set(kWidgetIdField, id);
  return widget_data;
}
#endif  // defined(USE_AURA)

bool ShouldHandleAccessibilityRequestCallback(const std::string& path) {
  return path == kTargetsDataFile;
}

void HandleAccessibilityRequestCallback(
    content::BrowserContext* current_context,
    const std::string& path,
    content::WebUIDataSource::GotDataCallback callback) {
  DCHECK(ShouldHandleAccessibilityRequestCallback(path));

  base::Value::Dict data;
  PrefService* pref =
      static_cast<electron::ElectronBrowserContext*>(current_context)->prefs();
  ui::AXMode mode =
      content::BrowserAccessibilityState::GetInstance()->GetAccessibilityMode();
  bool is_native_enabled = content::BrowserAccessibilityState::GetInstance()
                               ->IsRendererAccessibilityEnabled();
  bool native = mode.has_mode(ui::AXMode::kNativeAPIs);
  bool web = mode.has_mode(ui::AXMode::kWebContents);
  bool text = mode.has_mode(ui::AXMode::kInlineTextBoxes);
  bool screenreader = mode.has_mode(ui::AXMode::kScreenReader);
  bool html = mode.has_mode(ui::AXMode::kHTML);
  bool pdf_printing = mode.has_mode(ui::AXMode::kPDFPrinting);

  // The "native" and "web" flags are disabled if
  // --disable-renderer-accessibility is set.
  data.Set(kNative, is_native_enabled ? (native ? kOn : kOff) : kDisabled);
  data.Set(kWeb, is_native_enabled ? (web ? kOn : kOff) : kDisabled);

  // The "text", "screenreader" and "html" flags are only
  // meaningful if "web" is enabled.
  bool is_web_enabled = is_native_enabled && web;
  data.Set(kText, is_web_enabled ? (text ? kOn : kOff) : kDisabled);
  data.Set(kScreenReader,
           is_web_enabled ? (screenreader ? kOn : kOff) : kDisabled);
  data.Set(kHTML, is_web_enabled ? (html ? kOn : kOff) : kDisabled);

  // The "pdfPrinting" flag is independent of the others.
  data.Set(kPDFPrinting, pdf_printing ? kOn : kOff);

  // The "Top Level Widgets" section is only relevant if views accessibility is
  // enabled.
  data.Set(kViewsAccessibility, features::IsAccessibilityTreeForViewsEnabled());

  std::string pref_api_type =
      std::string(pref->GetString(prefs::kShownAccessibilityApiType));
  bool pref_api_type_supported = false;

  std::vector<ui::AXApiType::Type> supported_api_types =
      content::AXInspectFactory::SupportedApis();
  base::Value::List supported_api_list;
  supported_api_list.reserve(supported_api_types.size());
  for (ui::AXApiType::Type type : supported_api_types) {
    supported_api_list.Append(std::string_view(type));
    if (type == ui::AXApiType::From(pref_api_type)) {
      pref_api_type_supported = true;
    }
  }
  data.Set(kSupportedApiTypesField, std::move(supported_api_list));

  // If the saved API type is not supported, use the default platform formatter
  // API type.
  if (!pref_api_type_supported) {
    pref_api_type = std::string_view(
        content::AXInspectFactory::DefaultPlatformFormatterType());
  }
  data.Set(kApiTypeField, pref_api_type);

  bool is_mode_locked = !content::BrowserAccessibilityState::GetInstance()
                             ->IsAXModeChangeAllowed();
  data.Set(kLocked, is_mode_locked ? kOn : kOff);

  base::Value::List page_list;
  std::unique_ptr<content::RenderWidgetHostIterator> widget_iter(
      content::RenderWidgetHost::GetRenderWidgetHosts());

  while (content::RenderWidgetHost* widget = widget_iter->GetNextHost()) {
    // Ignore processes that don't have a connection, such as crashed tabs.
    if (!widget->GetProcess()->IsInitializedAndNotDead()) {
      continue;
    }
    content::RenderViewHost* rvh = content::RenderViewHost::From(widget);
    if (!rvh) {
      continue;
    }
    content::WebContents* web_contents =
        content::WebContents::FromRenderViewHost(rvh);
    content::WebContentsDelegate* delegate = web_contents->GetDelegate();
    if (!delegate) {
      continue;
    }
    if (web_contents->GetPrimaryMainFrame()->GetRenderViewHost() != rvh) {
      continue;
    }
    // Ignore views that are never user-visible, like background pages.
    if (delegate->IsNeverComposited(web_contents)) {
      continue;
    }
    content::BrowserContext* context = rvh->GetProcess()->GetBrowserContext();
    if (context != current_context) {
      continue;
    }

    base::Value::Dict descriptor = BuildTargetDescriptor(rvh);
    descriptor.Set(kNative, is_native_enabled);
    descriptor.Set(kScreenReader, is_web_enabled && screenreader);
    descriptor.Set(kWeb, is_web_enabled);
    page_list.Append(std::move(descriptor));
  }
  data.Set(kPagesField, std::move(page_list));

  base::Value::List window_list;
  for (auto* window : electron::WindowList::GetWindows()) {
    window_list.Append(BuildTargetDescriptor(window));
  }
  data.Set(kBrowsersField, std::move(window_list));

  base::Value::List widgets_list;
#if defined(USE_AURA)
  if (features::IsAccessibilityTreeForViewsEnabled()) {
    views::WidgetAXTreeIDMap& manager_map =
        views::WidgetAXTreeIDMap::GetInstance();
    const std::vector<views::Widget*> widgets = manager_map.GetWidgets();
    for (views::Widget* widget : widgets) {
      widgets_list.Append(BuildTargetDescriptor(widget));
    }
  }
#endif  // defined(USE_AURA)
  data.Set(kWidgetsField, std::move(widgets_list));

  std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(
      base::WriteJson(data).value_or("")));
}

std::string RecursiveDumpAXPlatformNodeAsString(
    ui::AXPlatformNode* node,
    int indent,
    const std::vector<ui::AXPropertyFilter>& property_filters) {
  if (!node) {
    return "";
  }
  std::string str(2 * indent, '+');
  std::string line = node->GetDelegate()->GetData().ToString();
  std::vector<std::string> attributes = base::SplitString(
      line, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (std::string attribute : attributes) {
    if (ui::AXTreeFormatter::MatchesPropertyFilters(property_filters, attribute,
                                                    false)) {
      str += attribute + " ";
    }
  }
  str += "\n";
  for (size_t i = 0; i < node->GetDelegate()->GetChildCount(); i++) {
    gfx::NativeViewAccessible child = node->GetDelegate()->ChildAtIndex(i);
    ui::AXPlatformNode* child_node =
        ui::AXPlatformNode::FromNativeViewAccessible(child);
    str += RecursiveDumpAXPlatformNodeAsString(child_node, indent + 1,
                                               property_filters);
  }
  return str;
}

// Add property filters to the property_filters vector for the given property
// filter type. The attributes are passed in as a string with each attribute
// separated by a space.
void AddPropertyFilters(std::vector<ui::AXPropertyFilter>& property_filters,
                        const std::string& attributes,
                        ui::AXPropertyFilter::Type type) {
  for (const std::string& attribute : base::SplitString(
           attributes, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    property_filters.emplace_back(attribute, type);
  }
}

bool IsValidJSValue(const std::string* str) {
  return str && str->length() < 5000U;
}

const std::string& CheckJSValue(const std::string* str) {
  CHECK(IsValidJSValue(str));
  return *str;
}

}  // namespace

ElectronAccessibilityUI::ElectronAccessibilityUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  auto* const browser_context = web_ui->GetWebContents()->GetBrowserContext();
  // Set up the chrome://accessibility source.
  content::WebUIDataSource* html_source =
      content::WebUIDataSource::CreateAndAdd(
          browser_context, chrome::kChromeUIAccessibilityHost);

  // Add required resources.
  html_source->UseStringsJs();
  html_source->AddResourcePaths(
      base::make_span(kAccessibilityResources, kAccessibilityResourcesSize));
  html_source->SetDefaultResource(IDR_ACCESSIBILITY_ACCESSIBILITY_HTML);
  html_source->SetRequestFilter(
      base::BindRepeating(&ShouldHandleAccessibilityRequestCallback),
      base::BindRepeating(&HandleAccessibilityRequestCallback,
                          browser_context));
  html_source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types parse-html-subset sanitize-inner-html;");

  web_ui->AddMessageHandler(
      std::make_unique<ElectronAccessibilityUIMessageHandler>());
}

ElectronAccessibilityUI::~ElectronAccessibilityUI() = default;

ElectronAccessibilityUIMessageHandler::ElectronAccessibilityUIMessageHandler() =
    default;

void ElectronAccessibilityUIMessageHandler::GetRequestTypeAndFilters(
    const base::Value::Dict& data,
    std::string& request_type,
    std::string& allow,
    std::string& allow_empty,
    std::string& deny) {
  request_type = CheckJSValue(data.FindString(kRequestTypeField));
  CHECK(request_type == kShowOrRefreshTree || request_type == kCopyTree);
  allow = CheckJSValue(data.FindStringByDottedPath("filters.allow"));
  allow_empty = CheckJSValue(data.FindStringByDottedPath("filters.allowEmpty"));
  deny = CheckJSValue(data.FindStringByDottedPath("filters.deny"));
}

void ElectronAccessibilityUIMessageHandler::RequestNativeUITree(
    const base::Value::List& args) {
  const base::Value::Dict& data = args.front().GetDict();

  std::string request_type, allow, allow_empty, deny;
  GetRequestTypeAndFilters(data, request_type, allow, allow_empty, deny);

  int window_id = *data.FindInt(kSessionIdField);

  AllowJavascript();

  std::vector<ui::AXPropertyFilter> property_filters;
  AddPropertyFilters(property_filters, allow, ui::AXPropertyFilter::ALLOW);
  AddPropertyFilters(property_filters, allow_empty,
                     ui::AXPropertyFilter::ALLOW_EMPTY);
  AddPropertyFilters(property_filters, deny, ui::AXPropertyFilter::DENY);

  for (auto* window : electron::WindowList::GetWindows()) {
    if (window->window_id() == window_id) {
      base::Value::Dict result = BuildTargetDescriptor(window);
      gfx::NativeWindow native_window = window->GetNativeWindow();
      ui::AXPlatformNode* node =
          ui::AXPlatformNode::FromNativeWindow(native_window);
      result.Set(kTreeField, RecursiveDumpAXPlatformNodeAsString(
                                 node, 0, property_filters));
      FireWebUIListener(request_type, result);
      return;
    }
  }

  // No browser with the specified |id| was found.
  base::Value::Dict result;
  result.Set(kSessionIdField, window_id);
  result.Set(kTypeField, kBrowser);
  result.Set(kErrorField, "Window no longer exists.");
  FireWebUIListener(request_type, result);
}

void ElectronAccessibilityUIMessageHandler::RegisterMessages() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  web_ui()->RegisterMessageCallback(
      "toggleAccessibility",
      base::BindRepeating(
          &AccessibilityUIMessageHandler::ToggleAccessibilityForWebContents,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setGlobalFlag",
      base::BindRepeating(&AccessibilityUIMessageHandler::SetGlobalFlag,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setGlobalString",
      base::BindRepeating(&AccessibilityUIMessageHandler::SetGlobalString,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestWebContentsTree",
      base::BindRepeating(
          &AccessibilityUIMessageHandler::RequestWebContentsTree,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestNativeUITree",
      base::BindRepeating(
          &ElectronAccessibilityUIMessageHandler::RequestNativeUITree,
          base::Unretained(this)));
#if defined(USE_AURA)
  web_ui()->RegisterMessageCallback(
      "requestWidgetsTree",
      base::BindRepeating(&AccessibilityUIMessageHandler::RequestWidgetsTree,
                          base::Unretained(this)));
#endif
  web_ui()->RegisterMessageCallback(
      "requestAccessibilityEvents",
      base::BindRepeating(
          &AccessibilityUIMessageHandler::RequestAccessibilityEvents,
          base::Unretained(this)));
}

// static
void ElectronAccessibilityUIMessageHandler::RegisterPrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  const std::string_view default_api_type =
      std::string_view(ui::AXApiType::Type(ui::AXApiType::kBlink));
  registry->RegisterStringPref(prefs::kShownAccessibilityApiType,
                               std::string(default_api_type));
}
