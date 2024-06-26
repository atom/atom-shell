// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_web_contents.h"

#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/contains.h"
#include "base/containers/fixed_flat_map.h"
#include "base/containers/id_map.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/current_thread.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/views/eye_dropper/eye_dropper.h"
#include "chrome/common/pref_names.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/security_state/content/content_utils.h"
#include "components/security_state/core/security_state.h"
#include "content/browser/renderer_host/frame_tree_node.h"  // nogncheck
#include "content/browser/renderer_host/navigation_controller_impl.h"  // nogncheck
#include "content/browser/renderer_host/render_frame_host_manager.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_impl.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_view_base.h"  // nogncheck
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/browser/desktop_streams_registry.h"
#include "content/public/browser/download_request_utils.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer_type_converters.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/webplugininfo.h"
#include "electron/buildflags/buildflags.h"
#include "electron/shell/common/api/api.mojom.h"
#include "gin/arguments.h"
#include "gin/data_object_builder.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "gin/wrappable.h"
#include "media/base/mime_util.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "ppapi/buildflags/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/memory_instrumentation.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "shell/browser/api/electron_api_browser_window.h"
#include "shell/browser/api/electron_api_debugger.h"
#include "shell/browser/api/electron_api_session.h"
#include "shell/browser/api/electron_api_web_frame_main.h"
#include "shell/browser/api/message_port.h"
#include "shell/browser/browser.h"
#include "shell/browser/child_web_contents_tracker.h"
#include "shell/browser/electron_autofill_driver_factory.h"
#include "shell/browser/electron_browser_client.h"
#include "shell/browser/electron_browser_context.h"
#include "shell/browser/electron_browser_main_parts.h"
#include "shell/browser/electron_navigation_throttle.h"
#include "shell/browser/file_select_helper.h"
#include "shell/browser/native_window.h"
#include "shell/browser/osr/osr_render_widget_host_view.h"
#include "shell/browser/osr/osr_web_contents_view.h"
#include "shell/browser/session_preferences.h"
#include "shell/browser/ui/drag_util.h"
#include "shell/browser/ui/file_dialog.h"
#include "shell/browser/ui/inspectable_web_contents.h"
#include "shell/browser/ui/inspectable_web_contents_view.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/browser/web_contents_preferences.h"
#include "shell/browser/web_contents_zoom_controller.h"
#include "shell/browser/web_view_guest_delegate.h"
#include "shell/browser/web_view_manager.h"
#include "shell/common/api/electron_api_native_image.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/color_util.h"
#include "shell/common/electron_constants.h"
#include "shell/common/gin_converters/base_converter.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/content_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_converters/frame_converter.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_converters/net_converter.h"
#include "shell/common/gin_converters/optional_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/language_util.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "shell/common/process_util.h"
#include "shell/common/thread_restrictions.h"
#include "shell/common/v8_value_serializer.h"
#include "storage/browser/file_system/isolated_context.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/messaging/transferable_message_mojom_traits.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/mojom/frame/find_in_page.mojom.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "third_party/blink/public/mojom/messaging/transferable_message.mojom.h"
#include "third_party/blink/public/mojom/renderer_preferences.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"

#if BUILDFLAG(IS_WIN)
#include "shell/browser/native_window_views.h"
#endif

#if !BUILDFLAG(IS_MAC)
#include "ui/aura/window.h"
#else
#include "ui/base/cocoa/defaults_utils.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "ui/linux/linux_ui.h"
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
#include "ui/gfx/font_render_params.h"
#endif

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "extensions/browser/script_executor.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "shell/browser/extensions/electron_extension_web_contents_observer.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "chrome/browser/printing/print_view_manager_base.h"
#include "components/printing/browser/print_manager_utils.h"
#include "components/printing/browser/print_to_pdf/pdf_print_result.h"
#include "components/printing/browser/print_to_pdf/pdf_print_utils.h"
#include "printing/mojom/print.mojom.h"  // nogncheck
#include "printing/page_range.h"
#include "shell/browser/printing/print_view_manager_electron.h"
#include "shell/browser/printing/printing_utils.h"

#if BUILDFLAG(IS_WIN)
#include "printing/backend/win_helper.h"
#endif
#endif  // BUILDFLAG(ENABLE_PRINTING)

#if BUILDFLAG(ENABLE_PDF_VIEWER)
#include "components/pdf/browser/pdf_document_helper.h"  // nogncheck
#include "shell/browser/electron_pdf_document_helper_client.h"
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
#include "content/public/browser/plugin_service.h"
#endif

#if !IS_MAS_BUILD()
#include "chrome/browser/hang_monitor/hang_crash_dump.h"  // nogncheck
#endif

namespace gin {

#if BUILDFLAG(ENABLE_PRINTING)
template <>
struct Converter<printing::mojom::MarginType> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     printing::mojom::MarginType* out) {
    using Val = printing::mojom::MarginType;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"custom", Val::kCustomMargins},
            {"default", Val::kDefaultMargins},
            {"none", Val::kNoMargins},
            {"printableArea", Val::kPrintableAreaMargins},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

template <>
struct Converter<printing::mojom::DuplexMode> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     printing::mojom::DuplexMode* out) {
    using Val = printing::mojom::DuplexMode;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"longEdge", Val::kLongEdge},
            {"shortEdge", Val::kShortEdge},
            {"simplex", Val::kSimplex},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

#endif

template <>
struct Converter<WindowOpenDisposition> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   WindowOpenDisposition val) {
    std::string disposition = "other";
    switch (val) {
      case WindowOpenDisposition::CURRENT_TAB:
        disposition = "default";
        break;
      case WindowOpenDisposition::NEW_FOREGROUND_TAB:
        disposition = "foreground-tab";
        break;
      case WindowOpenDisposition::NEW_BACKGROUND_TAB:
        disposition = "background-tab";
        break;
      case WindowOpenDisposition::NEW_POPUP:
      case WindowOpenDisposition::NEW_WINDOW:
        disposition = "new-window";
        break;
      case WindowOpenDisposition::SAVE_TO_DISK:
        disposition = "save-to-disk";
        break;
      default:
        break;
    }
    return gin::ConvertToV8(isolate, disposition);
  }
};

template <>
struct Converter<content::JavaScriptDialogType> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   content::JavaScriptDialogType val) {
    switch (val) {
      case content::JAVASCRIPT_DIALOG_TYPE_ALERT:
        return gin::ConvertToV8(isolate, "alert");
      case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:
        return gin::ConvertToV8(isolate, "confirm");
      case content::JAVASCRIPT_DIALOG_TYPE_PROMPT:
        return gin::ConvertToV8(isolate, "prompt");
    }
  }
};

template <>
struct Converter<content::SavePageType> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     content::SavePageType* out) {
    using Val = content::SavePageType;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"htmlcomplete", Val::SAVE_PAGE_TYPE_AS_COMPLETE_HTML},
            {"htmlonly", Val::SAVE_PAGE_TYPE_AS_ONLY_HTML},
            {"mhtml", Val::SAVE_PAGE_TYPE_AS_MHTML},
        });
    return FromV8WithLowerLookup(isolate, val, Lookup, out);
  }
};

template <>
struct Converter<electron::api::WebContents::Type> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   electron::api::WebContents::Type val) {
    using Type = electron::api::WebContents::Type;
    std::string type;
    switch (val) {
      case Type::kBackgroundPage:
        type = "backgroundPage";
        break;
      case Type::kBrowserWindow:
        type = "window";
        break;
      case Type::kBrowserView:
        type = "browserView";
        break;
      case Type::kRemote:
        type = "remote";
        break;
      case Type::kWebView:
        type = "webview";
        break;
      case Type::kOffScreen:
        type = "offscreen";
        break;
      default:
        break;
    }
    return gin::ConvertToV8(isolate, type);
  }

  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     electron::api::WebContents::Type* out) {
    using Val = electron::api::WebContents::Type;
    static constexpr auto Lookup =
        base::MakeFixedFlatMap<std::string_view, Val>({
            {"backgroundPage", Val::kBackgroundPage},
            {"browserView", Val::kBrowserView},
            {"offscreen", Val::kOffScreen},
            {"webview", Val::kWebView},
        });
    return FromV8WithLookup(isolate, val, Lookup, out);
  }
};

template <>
struct Converter<scoped_refptr<content::DevToolsAgentHost>> {
  static v8::Local<v8::Value> ToV8(
      v8::Isolate* isolate,
      const scoped_refptr<content::DevToolsAgentHost>& val) {
    gin_helper::Dictionary dict(isolate, v8::Object::New(isolate));
    dict.Set("id", val->GetId());
    dict.Set("url", val->GetURL().spec());
    return dict.GetHandle();
  }
};

template <>
struct Converter<content::NavigationEntry*> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   content::NavigationEntry* entry) {
    if (!entry) {
      return v8::Null(isolate);
    }
    gin_helper::Dictionary dict(isolate, v8::Object::New(isolate));
    dict.Set("url", entry->GetURL().spec());
    dict.Set("title", entry->GetTitleForDisplay());
    return dict.GetHandle();
  }
};

}  // namespace gin

namespace electron::api {

namespace {

constexpr std::string_view CursorTypeToString(
    ui::mojom::CursorType cursor_type) {
  switch (cursor_type) {
    case ui::mojom::CursorType::kPointer:
      return "pointer";
    case ui::mojom::CursorType::kCross:
      return "crosshair";
    case ui::mojom::CursorType::kHand:
      return "hand";
    case ui::mojom::CursorType::kIBeam:
      return "text";
    case ui::mojom::CursorType::kWait:
      return "wait";
    case ui::mojom::CursorType::kHelp:
      return "help";
    case ui::mojom::CursorType::kEastResize:
      return "e-resize";
    case ui::mojom::CursorType::kNorthResize:
      return "n-resize";
    case ui::mojom::CursorType::kNorthEastResize:
      return "ne-resize";
    case ui::mojom::CursorType::kNorthWestResize:
      return "nw-resize";
    case ui::mojom::CursorType::kSouthResize:
      return "s-resize";
    case ui::mojom::CursorType::kSouthEastResize:
      return "se-resize";
    case ui::mojom::CursorType::kSouthWestResize:
      return "sw-resize";
    case ui::mojom::CursorType::kWestResize:
      return "w-resize";
    case ui::mojom::CursorType::kNorthSouthResize:
      return "ns-resize";
    case ui::mojom::CursorType::kEastWestResize:
      return "ew-resize";
    case ui::mojom::CursorType::kNorthEastSouthWestResize:
      return "nesw-resize";
    case ui::mojom::CursorType::kNorthWestSouthEastResize:
      return "nwse-resize";
    case ui::mojom::CursorType::kColumnResize:
      return "col-resize";
    case ui::mojom::CursorType::kRowResize:
      return "row-resize";
    case ui::mojom::CursorType::kMiddlePanning:
      return "m-panning";
    case ui::mojom::CursorType::kMiddlePanningVertical:
      return "m-panning-vertical";
    case ui::mojom::CursorType::kMiddlePanningHorizontal:
      return "m-panning-horizontal";
    case ui::mojom::CursorType::kEastPanning:
      return "e-panning";
    case ui::mojom::CursorType::kNorthPanning:
      return "n-panning";
    case ui::mojom::CursorType::kNorthEastPanning:
      return "ne-panning";
    case ui::mojom::CursorType::kNorthWestPanning:
      return "nw-panning";
    case ui::mojom::CursorType::kSouthPanning:
      return "s-panning";
    case ui::mojom::CursorType::kSouthEastPanning:
      return "se-panning";
    case ui::mojom::CursorType::kSouthWestPanning:
      return "sw-panning";
    case ui::mojom::CursorType::kWestPanning:
      return "w-panning";
    case ui::mojom::CursorType::kMove:
      return "move";
    case ui::mojom::CursorType::kVerticalText:
      return "vertical-text";
    case ui::mojom::CursorType::kCell:
      return "cell";
    case ui::mojom::CursorType::kContextMenu:
      return "context-menu";
    case ui::mojom::CursorType::kAlias:
      return "alias";
    case ui::mojom::CursorType::kProgress:
      return "progress";
    case ui::mojom::CursorType::kNoDrop:
      return "nodrop";
    case ui::mojom::CursorType::kCopy:
      return "copy";
    case ui::mojom::CursorType::kNone:
      return "none";
    case ui::mojom::CursorType::kNotAllowed:
      return "not-allowed";
    case ui::mojom::CursorType::kZoomIn:
      return "zoom-in";
    case ui::mojom::CursorType::kZoomOut:
      return "zoom-out";
    case ui::mojom::CursorType::kGrab:
      return "grab";
    case ui::mojom::CursorType::kGrabbing:
      return "grabbing";
    case ui::mojom::CursorType::kCustom:
      return "custom";
    case ui::mojom::CursorType::kNull:
      return "null";
    case ui::mojom::CursorType::kDndNone:
      return "drag-drop-none";
    case ui::mojom::CursorType::kDndMove:
      return "drag-drop-move";
    case ui::mojom::CursorType::kDndCopy:
      return "drag-drop-copy";
    case ui::mojom::CursorType::kDndLink:
      return "drag-drop-link";
    case ui::mojom::CursorType::kNorthSouthNoResize:
      return "ns-no-resize";
    case ui::mojom::CursorType::kEastWestNoResize:
      return "ew-no-resize";
    case ui::mojom::CursorType::kNorthEastSouthWestNoResize:
      return "nesw-no-resize";
    case ui::mojom::CursorType::kNorthWestSouthEastNoResize:
      return "nwse-no-resize";
    default:
      return "default";
  }
}

base::IDMap<WebContents*>& GetAllWebContents() {
  static base::NoDestructor<base::IDMap<WebContents*>> s_all_web_contents;
  return *s_all_web_contents;
}

void OnCapturePageDone(gin_helper::Promise<gfx::Image> promise,
                       base::ScopedClosureRunner capture_handle,
                       const SkBitmap& bitmap) {
  auto ui_task_runner = content::GetUIThreadTaskRunner({});
  if (!ui_task_runner->RunsTasksInCurrentSequence()) {
    ui_task_runner->PostTask(
        FROM_HERE, base::BindOnce(&OnCapturePageDone, std::move(promise),
                                  std::move(capture_handle), bitmap));
    return;
  }

  // Hack to enable transparency in captured image
  promise.Resolve(gfx::Image::CreateFrom1xBitmap(bitmap));
  capture_handle.RunAndReset();
}

std::optional<base::TimeDelta> GetCursorBlinkInterval() {
#if BUILDFLAG(IS_MAC)
  std::optional<base::TimeDelta> system_value(
      ui::TextInsertionCaretBlinkPeriodFromDefaults());
  if (system_value)
    return *system_value;
#elif BUILDFLAG(IS_LINUX)
  if (auto* linux_ui = ui::LinuxUi::instance())
    return linux_ui->GetCursorBlinkInterval();
#elif BUILDFLAG(IS_WIN)
  const auto system_msec = ::GetCaretBlinkTime();
  if (system_msec != 0) {
    return (system_msec == INFINITE) ? base::TimeDelta()
                                     : base::Milliseconds(system_msec);
  }
#endif
  return std::nullopt;
}

struct UserDataLink : public base::SupportsUserData::Data {
  explicit UserDataLink(base::WeakPtr<WebContents> contents)
      : web_contents(contents) {}

  base::WeakPtr<WebContents> web_contents;
};
const void* kElectronApiWebContentsKey = &kElectronApiWebContentsKey;

const char kRootName[] = "<root>";

struct FileSystem {
  FileSystem() = default;
  FileSystem(const std::string& type,
             const std::string& file_system_name,
             const std::string& root_url,
             const std::string& file_system_path)
      : type(type),
        file_system_name(file_system_name),
        root_url(root_url),
        file_system_path(file_system_path) {}

  std::string type;
  std::string file_system_name;
  std::string root_url;
  std::string file_system_path;
};

std::string RegisterFileSystem(content::WebContents* web_contents,
                               const base::FilePath& path) {
  auto* isolated_context = storage::IsolatedContext::GetInstance();
  std::string root_name(kRootName);
  storage::IsolatedContext::ScopedFSHandle file_system =
      isolated_context->RegisterFileSystemForPath(
          storage::kFileSystemTypeLocal, std::string(), path, &root_name);

  content::ChildProcessSecurityPolicy* policy =
      content::ChildProcessSecurityPolicy::GetInstance();
  content::RenderViewHost* render_view_host = web_contents->GetRenderViewHost();
  int renderer_id = render_view_host->GetProcess()->GetID();
  policy->GrantReadFileSystem(renderer_id, file_system.id());
  policy->GrantWriteFileSystem(renderer_id, file_system.id());
  policy->GrantCreateFileForFileSystem(renderer_id, file_system.id());
  policy->GrantDeleteFromFileSystem(renderer_id, file_system.id());

  if (!policy->CanReadFile(renderer_id, path))
    policy->GrantReadFile(renderer_id, path);

  return file_system.id();
}

FileSystem CreateFileSystemStruct(content::WebContents* web_contents,
                                  const std::string& file_system_id,
                                  const std::string& file_system_path,
                                  const std::string& type) {
  const GURL origin = web_contents->GetURL().DeprecatedGetOriginAsURL();
  std::string file_system_name =
      storage::GetIsolatedFileSystemName(origin, file_system_id);
  std::string root_url = storage::GetIsolatedFileSystemRootURIString(
      origin, file_system_id, kRootName);
  return FileSystem(type, file_system_name, root_url, file_system_path);
}

base::Value::Dict CreateFileSystemValue(const FileSystem& file_system) {
  base::Value::Dict value;
  value.Set("type", file_system.type);
  value.Set("fileSystemName", file_system.file_system_name);
  value.Set("rootURL", file_system.root_url);
  value.Set("fileSystemPath", file_system.file_system_path);
  return value;
}

void WriteToFile(const base::FilePath& path,
                 const std::string& content,
                 bool is_base64) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::WILL_BLOCK);
  DCHECK(!path.empty());

  if (!is_base64) {
    base::WriteFile(path, content);
    return;
  }

  const std::optional<std::vector<uint8_t>> decoded_content =
      base::Base64Decode(content);
  if (decoded_content) {
    base::WriteFile(path, decoded_content.value());
  } else {
    LOG(ERROR) << "Invalid base64. Not writing " << path;
  }
}

void AppendToFile(const base::FilePath& path, const std::string& content) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::WILL_BLOCK);
  DCHECK(!path.empty());

  base::AppendToFile(path, content);
}

PrefService* GetPrefService(content::WebContents* web_contents) {
  auto* context = web_contents->GetBrowserContext();
  return static_cast<electron::ElectronBrowserContext*>(context)->prefs();
}

std::map<std::string, std::string> GetAddedFileSystemPaths(
    content::WebContents* web_contents) {
  auto* pref_service = GetPrefService(web_contents);
  const base::Value::Dict& file_system_paths =
      pref_service->GetDict(prefs::kDevToolsFileSystemPaths);
  std::map<std::string, std::string> result;
  for (auto it : file_system_paths) {
    std::string type =
        it.second.is_string() ? it.second.GetString() : std::string();
    result[it.first] = type;
  }
  return result;
}

bool IsDevToolsFileSystemAdded(content::WebContents* web_contents,
                               const std::string& file_system_path) {
  return base::Contains(GetAddedFileSystemPaths(web_contents),
                        file_system_path);
}

content::RenderFrameHost* GetRenderFrameHost(
    content::NavigationHandle* navigation_handle) {
  int frame_tree_node_id = navigation_handle->GetFrameTreeNodeId();
  content::FrameTreeNode* frame_tree_node =
      content::FrameTreeNode::GloballyFindByID(frame_tree_node_id);
  content::RenderFrameHostManager* render_manager =
      frame_tree_node->render_manager();
  content::RenderFrameHost* frame_host = nullptr;
  if (render_manager) {
    frame_host = render_manager->speculative_frame_host();
    if (!frame_host)
      frame_host = render_manager->current_frame_host();
  }

  return frame_host;
}
}  // namespace

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)

WebContents::Type GetTypeFromViewType(extensions::mojom::ViewType view_type) {
  switch (view_type) {
    case extensions::mojom::ViewType::kExtensionBackgroundPage:
      return WebContents::Type::kBackgroundPage;

    case extensions::mojom::ViewType::kAppWindow:
    case extensions::mojom::ViewType::kComponent:
    case extensions::mojom::ViewType::kExtensionPopup:
    case extensions::mojom::ViewType::kBackgroundContents:
    case extensions::mojom::ViewType::kExtensionGuest:
    case extensions::mojom::ViewType::kTabContents:
    case extensions::mojom::ViewType::kOffscreenDocument:
    case extensions::mojom::ViewType::kExtensionSidePanel:
    case extensions::mojom::ViewType::kInvalid:
      return WebContents::Type::kRemote;
  }
}

#endif

WebContents::WebContents(v8::Isolate* isolate,
                         content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      type_(Type::kRemote),
      id_(GetAllWebContents().Add(this))
#if BUILDFLAG(ENABLE_PRINTING)
      ,
      print_task_runner_(CreatePrinterHandlerTaskRunner())
#endif
{
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // WebContents created by extension host will have valid ViewType set.
  extensions::mojom::ViewType view_type = extensions::GetViewType(web_contents);
  if (view_type != extensions::mojom::ViewType::kInvalid) {
    InitWithExtensionView(isolate, web_contents, view_type);
  }

  extensions::ElectronExtensionWebContentsObserver::CreateForWebContents(
      web_contents);
  script_executor_ = std::make_unique<extensions::ScriptExecutor>(web_contents);
#endif

  auto session = Session::CreateFrom(isolate, GetBrowserContext());
  session_.Reset(isolate, session.ToV8());

  SetUserAgent(GetBrowserContext()->GetUserAgent());

  web_contents->SetUserData(kElectronApiWebContentsKey,
                            std::make_unique<UserDataLink>(GetWeakPtr()));
  InitZoomController(web_contents, gin::Dictionary::CreateEmpty(isolate));
}

WebContents::WebContents(v8::Isolate* isolate,
                         std::unique_ptr<content::WebContents> web_contents,
                         Type type)
    : content::WebContentsObserver(web_contents.get()),
      type_(type),
      id_(GetAllWebContents().Add(this))
#if BUILDFLAG(ENABLE_PRINTING)
      ,
      print_task_runner_(CreatePrinterHandlerTaskRunner())
#endif
{
  DCHECK(type != Type::kRemote)
      << "Can't take ownership of a remote WebContents";
  auto session = Session::CreateFrom(isolate, GetBrowserContext());
  session_.Reset(isolate, session.ToV8());
  InitWithSessionAndOptions(isolate, std::move(web_contents), session,
                            gin::Dictionary::CreateEmpty(isolate));
}

WebContents::WebContents(v8::Isolate* isolate,
                         const gin_helper::Dictionary& options)
    : id_(GetAllWebContents().Add(this))
#if BUILDFLAG(ENABLE_PRINTING)
      ,
      print_task_runner_(CreatePrinterHandlerTaskRunner())
#endif
{
  // Read options.
  options.Get("backgroundThrottling", &background_throttling_);

  // Get type
  options.Get("type", &type_);

  // Get transparent for guest view
  options.Get("transparent", &guest_transparent_);

  bool b = false;
  if (options.Get(options::kOffscreen, &b) && b)
    type_ = Type::kOffScreen;

  // Init embedder earlier
  options.Get("embedder", &embedder_);

  // Whether to enable DevTools.
  options.Get("devTools", &enable_devtools_);

  bool initially_shown = true;
  options.Get(options::kShow, &initially_shown);

  // Obtain the session.
  std::string partition;
  gin::Handle<api::Session> session;
  if (options.Get("session", &session) && !session.IsEmpty()) {
  } else if (options.Get("partition", &partition)) {
    session = Session::FromPartition(isolate, partition);
  } else {
    // Use the default session if not specified.
    session = Session::FromPartition(isolate, "");
  }
  session_.Reset(isolate, session.ToV8());

  std::unique_ptr<content::WebContents> web_contents;
  if (is_guest()) {
    scoped_refptr<content::SiteInstance> site_instance =
        content::SiteInstance::CreateForURL(session->browser_context(),
                                            GURL("chrome-guest://fake-host"));
    content::WebContents::CreateParams params(session->browser_context(),
                                              site_instance);
    guest_delegate_ =
        std::make_unique<WebViewGuestDelegate>(embedder_->web_contents(), this);
    params.guest_delegate = guest_delegate_.get();

    if (embedder_ && embedder_->IsOffScreen()) {
      auto* view = new OffScreenWebContentsView(
          false,
          base::BindRepeating(&WebContents::OnPaint, base::Unretained(this)));
      params.view = view;
      params.delegate_view = view;

      web_contents = content::WebContents::Create(params);
      view->SetWebContents(web_contents.get());
    } else {
      web_contents = content::WebContents::Create(params);
    }
  } else if (IsOffScreen()) {
    // webPreferences does not have a transparent option, so if the window needs
    // to be transparent, that will be set at electron_api_browser_window.cc#L57
    // and we then need to pull it back out and check it here.
    std::string background_color;
    options.GetHidden(options::kBackgroundColor, &background_color);
    bool transparent = ParseCSSColor(background_color) == SK_ColorTRANSPARENT;

    content::WebContents::CreateParams params(session->browser_context());
    auto* view = new OffScreenWebContentsView(
        transparent,
        base::BindRepeating(&WebContents::OnPaint, base::Unretained(this)));
    params.view = view;
    params.delegate_view = view;

    web_contents = content::WebContents::Create(params);
    view->SetWebContents(web_contents.get());
  } else {
    content::WebContents::CreateParams params(session->browser_context());
    params.initially_hidden = !initially_shown;
    web_contents = content::WebContents::Create(params);
  }

  InitWithSessionAndOptions(isolate, std::move(web_contents), session, options);
}

void WebContents::InitZoomController(content::WebContents* web_contents,
                                     const gin_helper::Dictionary& options) {
  WebContentsZoomController::CreateForWebContents(web_contents);
  zoom_controller_ = WebContentsZoomController::FromWebContents(web_contents);
  double zoom_factor;
  if (options.Get(options::kZoomFactor, &zoom_factor))
    zoom_controller_->SetDefaultZoomFactor(zoom_factor);

  // Nothing to do with ZoomController, but this function gets called in all
  // init cases!
  content::RenderViewHost* host = web_contents->GetRenderViewHost();
  if (host)
    host->GetWidget()->AddInputEventObserver(this);
}

void WebContents::InitWithSessionAndOptions(
    v8::Isolate* isolate,
    std::unique_ptr<content::WebContents> owned_web_contents,
    gin::Handle<api::Session> session,
    const gin_helper::Dictionary& options) {
  Observe(owned_web_contents.get());
  InitWithWebContents(std::move(owned_web_contents), session->browser_context(),
                      is_guest());

  inspectable_web_contents_->GetView()->SetDelegate(this);

  auto* prefs = web_contents()->GetMutableRendererPrefs();

  // Collect preferred languages from OS and browser process. accept_languages
  // effects HTTP header, navigator.languages, and CJK fallback font selection.
  //
  // Note that an application locale set to the browser process might be
  // different with the one set to the preference list.
  // (e.g. overridden with --lang)
  std::string accept_languages =
      g_browser_process->GetApplicationLocale() + ",";
  for (auto const& language : electron::GetPreferredLanguages()) {
    if (language == g_browser_process->GetApplicationLocale())
      continue;
    accept_languages += language + ",";
  }
  accept_languages.pop_back();
  prefs->accept_languages = accept_languages;

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
  // Update font settings.
  static const gfx::FontRenderParams params(
      gfx::GetFontRenderParams(gfx::FontRenderParamsQuery(), nullptr));
  prefs->should_antialias_text = params.antialiasing;
  prefs->use_subpixel_positioning = params.subpixel_positioning;
  prefs->hinting = params.hinting;
  prefs->use_autohinter = params.autohinter;
  prefs->use_bitmaps = params.use_bitmaps;
  prefs->subpixel_rendering = params.subpixel_rendering;
#endif

  // Honor the system's cursor blink rate settings
  if (auto interval = GetCursorBlinkInterval())
    prefs->caret_blink_interval = *interval;

  // Save the preferences in C++.
  // If there's already a WebContentsPreferences object, we created it as part
  // of the webContents.setWindowOpenHandler path, so don't overwrite it.
  if (!WebContentsPreferences::From(web_contents())) {
    new WebContentsPreferences(web_contents(), options);
  }
  // Trigger re-calculation of webkit prefs.
  web_contents()->NotifyPreferencesChanged();

  WebContentsPermissionHelper::CreateForWebContents(web_contents());
  InitZoomController(web_contents(), options);
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  extensions::ElectronExtensionWebContentsObserver::CreateForWebContents(
      web_contents());
  script_executor_ =
      std::make_unique<extensions::ScriptExecutor>(web_contents());
#endif

  AutofillDriverFactory::CreateForWebContents(web_contents());

  SetUserAgent(GetBrowserContext()->GetUserAgent());

  if (is_guest()) {
    NativeWindow* owner_window = nullptr;
    if (embedder_) {
      // New WebContents's owner_window is the embedder's owner_window.
      auto* relay =
          NativeWindowRelay::FromWebContents(embedder_->web_contents());
      if (relay)
        owner_window = relay->GetNativeWindow();
    }
    if (owner_window)
      SetOwnerWindow(owner_window);
  }

  web_contents()->SetUserData(kElectronApiWebContentsKey,
                              std::make_unique<UserDataLink>(GetWeakPtr()));
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
void WebContents::InitWithExtensionView(v8::Isolate* isolate,
                                        content::WebContents* web_contents,
                                        extensions::mojom::ViewType view_type) {
  // Must reassign type prior to calling `Init`.
  type_ = GetTypeFromViewType(view_type);
  if (type_ == Type::kRemote)
    return;
  if (type_ == Type::kBackgroundPage)
    // non-background-page WebContents are retained by other classes. We need
    // to pin here to prevent background-page WebContents from being GC'd.
    // The background page api::WebContents will live until the underlying
    // content::WebContents is destroyed.
    Pin(isolate);

  // Allow toggling DevTools for background pages
  Observe(web_contents);
  InitWithWebContents(std::unique_ptr<content::WebContents>(web_contents),
                      GetBrowserContext(), is_guest());
  inspectable_web_contents_->GetView()->SetDelegate(this);
}
#endif

void WebContents::InitWithWebContents(
    std::unique_ptr<content::WebContents> web_contents,
    ElectronBrowserContext* browser_context,
    bool is_guest) {
  browser_context_ = browser_context;
  web_contents->SetDelegate(this);

#if BUILDFLAG(ENABLE_PRINTING)
  PrintViewManagerElectron::CreateForWebContents(web_contents.get());
#endif

  // Determine whether the WebContents is offscreen.
  auto* web_preferences = WebContentsPreferences::From(web_contents.get());
  offscreen_ = web_preferences && web_preferences->IsOffscreen();

  // Create InspectableWebContents.
  inspectable_web_contents_ = std::make_unique<InspectableWebContents>(
      std::move(web_contents), browser_context->prefs(), is_guest);
  inspectable_web_contents_->SetDelegate(this);
}

WebContents::~WebContents() {
  if (owner_window_) {
    owner_window_->RemoveBackgroundThrottlingSource(this);
  }
  if (web_contents()) {
    content::RenderViewHost* host = web_contents()->GetRenderViewHost();
    if (host)
      host->GetWidget()->RemoveInputEventObserver(this);
  }

  if (!inspectable_web_contents_) {
    WebContentsDestroyed();
    return;
  }

  inspectable_web_contents_->GetView()->SetDelegate(nullptr);

  // This event is only for internal use, which is emitted when WebContents is
  // being destroyed.
  Emit("will-destroy");

  // For guest view based on OOPIF, the WebContents is released by the embedder
  // frame, and we need to clear the reference to the memory.
  bool not_owned_by_this = is_guest() && attached_;
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // And background pages are owned by extensions::ExtensionHost.
  if (type_ == Type::kBackgroundPage)
    not_owned_by_this = true;
#endif
  if (not_owned_by_this) {
    inspectable_web_contents_->ReleaseWebContents();
    WebContentsDestroyed();
  }

  // InspectableWebContents will be automatically destroyed.
}

void WebContents::DeleteThisIfAlive() {
  // It is possible that the FirstWeakCallback has been called but the
  // SecondWeakCallback has not, in this case the garbage collection of
  // WebContents has already started and we should not |delete this|.
  // Calling |GetWrapper| can detect this corner case.
  auto* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper))
    return;
  delete this;
}

void WebContents::Destroy() {
  // The content::WebContents should be destroyed asynchronously when possible
  // as user may choose to destroy WebContents during an event of it.
  if (Browser::Get()->is_shutting_down() || is_guest()) {
    DeleteThisIfAlive();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&WebContents::DeleteThisIfAlive, GetWeakPtr()));
  }
}

void WebContents::Close(std::optional<gin_helper::Dictionary> options) {
  bool dispatch_beforeunload = false;
  if (options)
    options->Get("waitForBeforeUnload", &dispatch_beforeunload);
  if (dispatch_beforeunload &&
      web_contents()->NeedToFireBeforeUnloadOrUnloadEvents()) {
    NotifyUserActivation();
    web_contents()->DispatchBeforeUnload(false /* auto_cancel */);
  } else {
    web_contents()->Close();
  }
}

bool WebContents::DidAddMessageToConsole(
    content::WebContents* source,
    blink::mojom::ConsoleMessageLevel level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id) {
  return Emit("console-message", static_cast<int32_t>(level), message, line_no,
              source_id);
}

void WebContents::OnCreateWindow(
    const GURL& target_url,
    const content::Referrer& referrer,
    const std::string& frame_name,
    WindowOpenDisposition disposition,
    const std::string& features,
    const scoped_refptr<network::ResourceRequestBody>& body) {
  Emit("-new-window", target_url, frame_name, disposition, features, referrer,
       body);
}

void WebContents::WebContentsCreatedWithFullParams(
    content::WebContents* source_contents,
    int opener_render_process_id,
    int opener_render_frame_id,
    const content::mojom::CreateNewWindowParams& params,
    content::WebContents* new_contents) {
  ChildWebContentsTracker::CreateForWebContents(new_contents);
  auto* tracker = ChildWebContentsTracker::FromWebContents(new_contents);
  tracker->url = params.target_url;
  tracker->frame_name = params.frame_name;
  tracker->referrer = params.referrer.To<content::Referrer>();
  tracker->raw_features = params.raw_features;
  tracker->body = params.body;

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);

  gin_helper::Dictionary dict;
  gin::ConvertFromV8(isolate, pending_child_web_preferences_.Get(isolate),
                     &dict);
  pending_child_web_preferences_.Reset();

  // Associate the preferences passed in via `setWindowOpenHandler` with the
  // content::WebContents that was just created for the child window. These
  // preferences will be picked up by the RenderWidgetHost via its call to the
  // delegate's OverrideWebkitPrefs.
  new WebContentsPreferences(new_contents, dict);
}

bool WebContents::IsWebContentsCreationOverridden(
    content::SiteInstance* source_site_instance,
    content::mojom::WindowContainerType window_container_type,
    const GURL& opener_url,
    const content::mojom::CreateNewWindowParams& params) {
  bool default_prevented = Emit(
      "-will-add-new-contents", params.target_url, params.frame_name,
      params.raw_features, params.disposition, *params.referrer, params.body);
  // If the app prevented the default, redirect to CreateCustomWebContents,
  // which always returns nullptr, which will result in the window open being
  // prevented (window.open() will return null in the renderer).
  return default_prevented;
}

void WebContents::SetNextChildWebPreferences(
    const gin_helper::Dictionary preferences) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  // Store these prefs for when Chrome calls WebContentsCreatedWithFullParams
  // with the new child contents.
  pending_child_web_preferences_.Reset(isolate, preferences.GetHandle());
}

content::WebContents* WebContents::CreateCustomWebContents(
    content::RenderFrameHost* opener,
    content::SiteInstance* source_site_instance,
    bool is_new_browsing_instance,
    const GURL& opener_url,
    const std::string& frame_name,
    const GURL& target_url,
    const content::StoragePartitionConfig& partition_config,
    content::SessionStorageNamespace* session_storage_namespace) {
  return nullptr;
}

void WebContents::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  auto* tracker = ChildWebContentsTracker::FromWebContents(new_contents.get());
  DCHECK(tracker);

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();

  v8::HandleScope handle_scope(isolate);
  auto api_web_contents =
      CreateAndTake(isolate, std::move(new_contents), Type::kBrowserWindow);

  // We call RenderFrameCreated here as at this point the empty "about:blank"
  // render frame has already been created.  If the window never navigates again
  // RenderFrameCreated won't be called and certain prefs like
  // "kBackgroundColor" will not be applied.
  auto* frame = api_web_contents->MainFrame();
  if (frame) {
    api_web_contents->HandleNewRenderFrame(frame);
  }

  if (Emit("-add-new-contents", api_web_contents, disposition, user_gesture,
           window_features.bounds.x(), window_features.bounds.y(),
           window_features.bounds.width(), window_features.bounds.height(),
           tracker->url, tracker->frame_name, tracker->referrer,
           tracker->raw_features, tracker->body)) {
    api_web_contents->Destroy();
  }
}

content::WebContents* WebContents::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  auto weak_this = GetWeakPtr();
  if (params.disposition != WindowOpenDisposition::CURRENT_TAB) {
    Emit("-new-window", params.url, "", params.disposition, "", params.referrer,
         params.post_data);
    return nullptr;
  }

  if (!weak_this || !web_contents())
    return nullptr;

  content::NavigationController::LoadURLParams load_url_params(params.url);
  load_url_params.referrer = params.referrer;
  load_url_params.transition_type = params.transition;
  load_url_params.extra_headers = params.extra_headers;
  load_url_params.should_replace_current_entry =
      params.should_replace_current_entry;
  load_url_params.is_renderer_initiated = params.is_renderer_initiated;
  load_url_params.started_from_context_menu = params.started_from_context_menu;
  load_url_params.initiator_origin = params.initiator_origin;
  load_url_params.source_site_instance = params.source_site_instance;
  load_url_params.frame_tree_node_id = params.frame_tree_node_id;
  load_url_params.redirect_chain = params.redirect_chain;
  load_url_params.has_user_gesture = params.user_gesture;
  load_url_params.blob_url_loader_factory = params.blob_url_loader_factory;
  load_url_params.href_translate = params.href_translate;
  load_url_params.reload_type = params.reload_type;

  if (params.post_data) {
    load_url_params.load_type =
        content::NavigationController::LOAD_TYPE_HTTP_POST;
    load_url_params.post_data = params.post_data;
  }

  source->GetController().LoadURLWithParams(load_url_params);
  return source;
}

void WebContents::BeforeUnloadFired(content::WebContents* tab,
                                    bool proceed,
                                    bool* proceed_to_fire_unload) {
  // Note that Chromium does not emit this for navigations.

  // Emit returns true if preventDefault() was called, so !Emit will be true if
  // the event should proceed.
  *proceed_to_fire_unload = !Emit("-before-unload-fired", proceed);
}

void WebContents::SetContentsBounds(content::WebContents* source,
                                    const gfx::Rect& rect) {
  if (!Emit("content-bounds-updated", rect))
    for (ExtendedWebContentsObserver& observer : observers_)
      observer.OnSetContentBounds(rect);
}

void WebContents::CloseContents(content::WebContents* source) {
  Emit("close");

  auto* autofill_driver_factory =
      AutofillDriverFactory::FromWebContents(web_contents());
  if (autofill_driver_factory) {
    autofill_driver_factory->CloseAllPopups();
  }

  Destroy();
}

void WebContents::ActivateContents(content::WebContents* source) {
  for (ExtendedWebContentsObserver& observer : observers_)
    observer.OnActivateContents();
}

void WebContents::UpdateTargetURL(content::WebContents* source,
                                  const GURL& url) {
  Emit("update-target-url", url);
}

bool WebContents::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (type_ == Type::kWebView && embedder_) {
    // Send the unhandled keyboard events back to the embedder.
    return embedder_->HandleKeyboardEvent(source, event);
  } else {
    return PlatformHandleKeyboardEvent(source, event);
  }
}

#if !BUILDFLAG(IS_MAC)
// NOTE: The macOS version of this function is found in
// electron_api_web_contents_mac.mm, as it requires calling into objective-C
// code.
bool WebContents::PlatformHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  // Check if the webContents has preferences and to ignore shortcuts
  auto* web_preferences = WebContentsPreferences::From(source);
  if (web_preferences && web_preferences->ShouldIgnoreMenuShortcuts())
    return false;

  // Let the NativeWindow handle other parts.
  if (owner_window()) {
    owner_window()->HandleKeyboardEvent(source, event);
    return true;
  }

  return false;
}
#endif

content::KeyboardEventProcessingResult WebContents::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (exclusive_access_manager_.HandleUserKeyEvent(event))
    return content::KeyboardEventProcessingResult::HANDLED;

  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown ||
      event.GetType() == blink::WebInputEvent::Type::kKeyUp) {
    // For backwards compatibility, pretend that `kRawKeyDown` events are
    // actually `kKeyDown`.
    input::NativeWebKeyboardEvent tweaked_event(event);
    if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown)
      tweaked_event.SetType(blink::WebInputEvent::Type::kKeyDown);
    bool prevent_default = Emit("before-input-event", tweaked_event);
    if (prevent_default) {
      return content::KeyboardEventProcessingResult::HANDLED;
    }
  }

  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

void WebContents::ContentsZoomChange(bool zoom_in) {
  Emit("zoom-changed", zoom_in ? "in" : "out");
}

Profile* WebContents::GetProfile() {
  return nullptr;
}

bool WebContents::IsFullscreen() const {
  if (!owner_window())
    return false;

  return owner_window()->IsFullscreen() || is_html_fullscreen();
}

void WebContents::EnterFullscreen(const GURL& url,
                                  ExclusiveAccessBubbleType bubble_type,
                                  const int64_t display_id) {}

void WebContents::ExitFullscreen() {}

void WebContents::UpdateExclusiveAccessBubble(
    const ExclusiveAccessBubbleParams& params,
    ExclusiveAccessBubbleHideCallback bubble_first_hide_callback) {}

void WebContents::OnExclusiveAccessUserInput() {}

content::WebContents* WebContents::GetActiveWebContents() {
  return web_contents();
}

bool WebContents::CanUserExitFullscreen() const {
  return true;
}

bool WebContents::IsExclusiveAccessBubbleDisplayed() const {
  return false;
}

void WebContents::EnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  auto* source = content::WebContents::FromRenderFrameHost(requesting_frame);
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(source);
  auto callback =
      base::BindRepeating(&WebContents::OnEnterFullscreenModeForTab,
                          base::Unretained(this), requesting_frame, options);
  permission_helper->RequestFullscreenPermission(requesting_frame, callback);
}

void WebContents::OnEnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options,
    bool allowed) {
  if (!allowed || !owner_window())
    return;

  auto* source = content::WebContents::FromRenderFrameHost(requesting_frame);
  if (IsFullscreenForTabOrPending(source)) {
    DCHECK_EQ(fullscreen_frame_, source->GetFocusedFrame());
    return;
  }

  owner_window()->set_fullscreen_transition_type(
      NativeWindow::FullScreenTransitionType::kHTML);
  exclusive_access_manager_.fullscreen_controller()->EnterFullscreenModeForTab(
      requesting_frame, options.display_id);

  SetHtmlApiFullscreen(true);

  if (native_fullscreen_) {
    // Explicitly trigger a view resize, as the size is not actually changing if
    // the browser is fullscreened, too.
    source->GetRenderViewHost()->GetWidget()->SynchronizeVisualProperties();
  }
}

void WebContents::ExitFullscreenModeForTab(content::WebContents* source) {
  if (!owner_window())
    return;

  // This needs to be called before we exit fullscreen on the native window,
  // or the controller will incorrectly think we weren't fullscreen and bail.
  exclusive_access_manager_.fullscreen_controller()->ExitFullscreenModeForTab(
      source);

  SetHtmlApiFullscreen(false);

  if (native_fullscreen_) {
    // Explicitly trigger a view resize, as the size is not actually changing if
    // the browser is fullscreened, too. Chrome does this indirectly from
    // `chrome/browser/ui/exclusive_access/fullscreen_controller.cc`.
    source->GetRenderViewHost()->GetWidget()->SynchronizeVisualProperties();
  }
}

void WebContents::RendererUnresponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host,
    base::RepeatingClosure hang_monitor_restarter) {
  Emit("unresponsive");
}

void WebContents::RendererResponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host) {
  Emit("responsive");
}

bool WebContents::HandleContextMenu(content::RenderFrameHost& render_frame_host,
                                    const content::ContextMenuParams& params) {
  Emit("context-menu", std::make_pair(params, &render_frame_host));

  return true;
}

void WebContents::FindReply(content::WebContents* web_contents,
                            int request_id,
                            int number_of_matches,
                            const gfx::Rect& selection_rect,
                            int active_match_ordinal,
                            bool final_update) {
  if (!final_update)
    return;

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto result = gin_helper::Dictionary::CreateEmpty(isolate);
  result.Set("requestId", request_id);
  result.Set("matches", number_of_matches);
  result.Set("selectionArea", selection_rect);
  result.Set("activeMatchOrdinal", active_match_ordinal);
  result.Set("finalUpdate", final_update);  // Deprecate after 2.0
  Emit("found-in-page", result.GetHandle());
}

void WebContents::OnRequestPointerLock(content::WebContents* web_contents,
                                       bool user_gesture,
                                       bool last_unlocked_by_target,
                                       bool allowed) {
  if (allowed) {
    exclusive_access_manager_.pointer_lock_controller()->RequestToLockPointer(
        web_contents, user_gesture, last_unlocked_by_target);
  } else {
    web_contents->GotResponseToPointerLockRequest(
        blink::mojom::PointerLockResult::kPermissionDenied);
  }
}

void WebContents::RequestPointerLock(content::WebContents* web_contents,
                                     bool user_gesture,
                                     bool last_unlocked_by_target) {
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  permission_helper->RequestPointerLockPermission(
      user_gesture, last_unlocked_by_target,
      base::BindOnce(&WebContents::OnRequestPointerLock,
                     base::Unretained(this)));
}

void WebContents::LostPointerLock() {
  exclusive_access_manager_.pointer_lock_controller()
      ->ExitExclusiveAccessToPreviousState();
}

void WebContents::OnRequestKeyboardLock(content::WebContents* web_contents,
                                        bool esc_key_locked,
                                        bool allowed) {
  if (allowed) {
    exclusive_access_manager_.keyboard_lock_controller()->RequestKeyboardLock(
        web_contents, esc_key_locked);
  } else {
    web_contents->GotResponseToKeyboardLockRequest(false);
  }
}

void WebContents::RequestKeyboardLock(content::WebContents* web_contents,
                                      bool esc_key_locked) {
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  permission_helper->RequestKeyboardLockPermission(
      esc_key_locked, base::BindOnce(&WebContents::OnRequestKeyboardLock,
                                     base::Unretained(this)));
}

void WebContents::CancelKeyboardLockRequest(
    content::WebContents* web_contents) {
  exclusive_access_manager_.keyboard_lock_controller()
      ->CancelKeyboardLockRequest(web_contents);
}

bool WebContents::CheckMediaAccessPermission(
    content::RenderFrameHost* render_frame_host,
    const url::Origin& security_origin,
    blink::mojom::MediaStreamType type) {
  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  return permission_helper->CheckMediaAccessPermission(security_origin, type);
}

void WebContents::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  auto* permission_helper =
      WebContentsPermissionHelper::FromWebContents(web_contents);
  permission_helper->RequestMediaAccessPermission(request, std::move(callback));
}

const void* const kJavaScriptDialogManagerKey = &kJavaScriptDialogManagerKey;

content::JavaScriptDialogManager* WebContents::GetJavaScriptDialogManager(
    content::WebContents* source) {
  // Indirect these delegate methods through a helper object whose lifetime is
  // bound to that of the content::WebContents. This prevents the
  // content::WebContents from calling methods on the Electron WebContents in
  // the event that the Electron one is destroyed before the content one, as
  // happens sometimes during shutdown or when webviews are involved.
  class JSDialogManagerHelper : public content::JavaScriptDialogManager,
                                public base::SupportsUserData::Data {
   public:
    void RunJavaScriptDialog(content::WebContents* web_contents,
                             content::RenderFrameHost* rfh,
                             content::JavaScriptDialogType dialog_type,
                             const std::u16string& message_text,
                             const std::u16string& default_prompt_text,
                             DialogClosedCallback callback,
                             bool* did_suppress_message) override {
      auto* wc = WebContents::From(web_contents);
      if (wc)
        wc->RunJavaScriptDialog(web_contents, rfh, dialog_type, message_text,
                                default_prompt_text, std::move(callback),
                                did_suppress_message);
    }
    void RunBeforeUnloadDialog(content::WebContents* web_contents,
                               content::RenderFrameHost* rfh,
                               bool is_reload,
                               DialogClosedCallback callback) override {
      auto* wc = WebContents::From(web_contents);
      if (wc)
        wc->RunBeforeUnloadDialog(web_contents, rfh, is_reload,
                                  std::move(callback));
    }
    void CancelDialogs(content::WebContents* web_contents,
                       bool reset_state) override {
      auto* wc = WebContents::From(web_contents);
      if (wc)
        wc->CancelDialogs(web_contents, reset_state);
    }
  };
  if (!source->GetUserData(kJavaScriptDialogManagerKey))
    source->SetUserData(kJavaScriptDialogManagerKey,
                        std::make_unique<JSDialogManagerHelper>());

  return static_cast<JSDialogManagerHelper*>(
      source->GetUserData(kJavaScriptDialogManagerKey));
}

void WebContents::OnAudioStateChanged(bool audible) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  gin::Handle<gin_helper::internal::Event> event =
      gin_helper::internal::Event::New(isolate);
  v8::Local<v8::Object> event_object = event.ToV8().As<v8::Object>();
  gin::Dictionary dict(isolate, event_object);
  dict.Set("audible", audible);
  EmitWithoutEvent("audio-state-changed", event);
}

void WebContents::BeforeUnloadFired(bool proceed) {
  // Do nothing, we override this method just to avoid compilation error since
  // there are two virtual functions named BeforeUnloadFired.
}

void WebContents::HandleNewRenderFrame(
    content::RenderFrameHost* render_frame_host) {
  auto* rwhv = render_frame_host->GetView();
  if (!rwhv)
    return;

  // Set the background color of RenderWidgetHostView.
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  if (web_preferences)
    SetBackgroundColor(web_preferences->GetBackgroundColor());

  if (!background_throttling_)
    render_frame_host->GetRenderViewHost()->SetSchedulerThrottling(false);

  auto* rwh_impl =
      static_cast<content::RenderWidgetHostImpl*>(rwhv->GetRenderWidgetHost());
  if (rwh_impl)
    rwh_impl->disable_hidden_ = !background_throttling_;

  auto* web_frame = WebFrameMain::FromRenderFrameHost(render_frame_host);
  if (web_frame)
    web_frame->MaybeSetupMojoConnection();
}

void WebContents::OnBackgroundColorChanged() {
  std::optional<SkColor> color = web_contents()->GetBackgroundColor();
  if (color.has_value()) {
    auto* const view = web_contents()->GetRenderWidgetHostView();
    static_cast<content::RenderWidgetHostViewBase*>(view)
        ->SetContentBackgroundColor(color.value());
  }
}

void WebContents::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  HandleNewRenderFrame(render_frame_host);

  // RenderFrameCreated is called for speculative frames which may not be
  // used in certain cross-origin navigations. Invoking
  // RenderFrameHost::GetLifecycleState currently crashes when called for
  // speculative frames so we need to filter it out for now. Check
  // https://crbug.com/1183639 for details on when this can be removed.
  auto* rfh_impl =
      static_cast<content::RenderFrameHostImpl*>(render_frame_host);
  if (rfh_impl->lifecycle_state() ==
      content::RenderFrameHostImpl::LifecycleStateImpl::kSpeculative) {
    return;
  }

  content::RenderFrameHost::LifecycleState lifecycle_state =
      render_frame_host->GetLifecycleState();
  if (lifecycle_state == content::RenderFrameHost::LifecycleState::kActive) {
    v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
    v8::HandleScope handle_scope(isolate);
    auto details = gin_helper::Dictionary::CreateEmpty(isolate);
    details.SetGetter("frame", render_frame_host);
    Emit("frame-created", details);
  }
}

void WebContents::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  // A RenderFrameHost can be deleted when:
  // - A WebContents is removed and its containing frames are disposed.
  // - An <iframe> is removed from the DOM.
  // - Cross-origin navigation creates a new RFH in a separate process which
  //   is swapped by content::RenderFrameHostManager.
  //

  // WebFrameMain::FromRenderFrameHost(rfh) will use the RFH's FrameTreeNode ID
  // to find an existing instance of WebFrameMain. During a cross-origin
  // navigation, the deleted RFH will be the old host which was swapped out. In
  // this special case, we need to also ensure that WebFrameMain's internal RFH
  // matches before marking it as disposed.
  auto* web_frame = WebFrameMain::FromRenderFrameHost(render_frame_host);
  if (web_frame && web_frame->render_frame_host() == render_frame_host)
    web_frame->MarkRenderFrameDisposed();
}

void WebContents::RenderFrameHostChanged(content::RenderFrameHost* old_host,
                                         content::RenderFrameHost* new_host) {
  if (new_host->IsInPrimaryMainFrame()) {
    if (old_host)
      old_host->GetRenderWidgetHost()->RemoveInputEventObserver(this);
    if (new_host)
      new_host->GetRenderWidgetHost()->AddInputEventObserver(this);
  }

  // During cross-origin navigation, a FrameTreeNode will swap out its RFH.
  // If an instance of WebFrameMain exists, it will need to have its RFH
  // swapped as well.
  //
  // |old_host| can be a nullptr so we use |new_host| for looking up the
  // WebFrameMain instance.
  auto* web_frame = WebFrameMain::FromRenderFrameHost(new_host);
  if (web_frame) {
    web_frame->UpdateRenderFrameHost(new_host);
  }
}

void WebContents::FrameDeleted(int frame_tree_node_id) {
  auto* web_frame = WebFrameMain::FromFrameTreeNodeId(frame_tree_node_id);
  if (web_frame)
    web_frame->Destroyed();
}

void WebContents::RenderViewDeleted(content::RenderViewHost* render_view_host) {
  // This event is necessary for tracking any states with respect to
  // intermediate render view hosts aka speculative render view hosts. Currently
  // used by object-registry.js to ref count remote objects.
  Emit("render-view-deleted", render_view_host->GetProcess()->GetID());

  if (web_contents()->GetRenderViewHost() == render_view_host) {
    // When the RVH that has been deleted is the current RVH it means that the
    // the web contents are being closed. This is communicated by this event.
    // Currently tracked by guest-window-manager.ts to destroy the
    // BrowserWindow.
    Emit("current-render-view-deleted",
         render_view_host->GetProcess()->GetID());
  }
}

void WebContents::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto details = gin_helper::Dictionary::CreateEmpty(isolate);
  details.Set("reason", status);
  details.Set("exitCode", web_contents()->GetCrashedErrorCode());
  Emit("render-process-gone", details);
}

void WebContents::PluginCrashed(const base::FilePath& plugin_path,
                                base::ProcessId plugin_pid) {
#if BUILDFLAG(ENABLE_PLUGINS)
  content::WebPluginInfo info;
  auto* plugin_service = content::PluginService::GetInstance();
  plugin_service->GetPluginInfoByPath(plugin_path, &info);
  Emit("plugin-crashed", info.name, info.version);
#endif  // BUILDFLAG(ENABLE_PLUGINS)
}

void WebContents::MediaStartedPlaying(const MediaPlayerInfo& video_type,
                                      const content::MediaPlayerId& id) {
  Emit("media-started-playing");
}

void WebContents::MediaStoppedPlaying(
    const MediaPlayerInfo& video_type,
    const content::MediaPlayerId& id,
    content::WebContentsObserver::MediaStoppedReason reason) {
  Emit("media-paused");
}

void WebContents::DidChangeThemeColor() {
  auto theme_color = web_contents()->GetThemeColor();
  if (theme_color) {
    Emit("did-change-theme-color", electron::ToRGBHex(theme_color.value()));
  } else {
    Emit("did-change-theme-color", nullptr);
  }
}

void WebContents::DidAcquireFullscreen(content::RenderFrameHost* rfh) {
  set_fullscreen_frame(rfh);
}

void WebContents::OnWebContentsFocused(
    content::RenderWidgetHost* render_widget_host) {
  Emit("focus");
}

void WebContents::OnWebContentsLostFocus(
    content::RenderWidgetHost* render_widget_host) {
  Emit("blur");
}

void WebContents::DOMContentLoaded(
    content::RenderFrameHost* render_frame_host) {
  auto* web_frame = WebFrameMain::FromRenderFrameHost(render_frame_host);
  if (web_frame)
    web_frame->DOMContentLoaded();

  if (!render_frame_host->GetParent())
    Emit("dom-ready");
}

void WebContents::DidFinishLoad(content::RenderFrameHost* render_frame_host,
                                const GURL& validated_url) {
  bool is_main_frame = !render_frame_host->GetParent();
  int frame_process_id = render_frame_host->GetProcess()->GetID();
  int frame_routing_id = render_frame_host->GetRoutingID();
  auto weak_this = GetWeakPtr();
  Emit("did-frame-finish-load", is_main_frame, frame_process_id,
       frame_routing_id);

  // ⚠️WARNING!⚠️
  // Emit() triggers JS which can call destroy() on |this|. It's not safe to
  // assume that |this| points to valid memory at this point.
  if (is_main_frame && weak_this && web_contents())
    Emit("did-finish-load");
}

void WebContents::DidFailLoad(content::RenderFrameHost* render_frame_host,
                              const GURL& url,
                              int error_code) {
  // See DocumentLoader::StartLoadingResponse() - when we navigate to a media
  // resource the original request for the media resource, which resulted in a
  // committed navigation, is simply discarded. The media element created
  // inside the MediaDocument then makes *another new* request for the same
  // media resource.
  bool is_media_document =
      media::IsSupportedMediaMimeType(web_contents()->GetContentsMimeType());
  if (error_code == net::ERR_ABORTED && is_media_document)
    return;

  bool is_main_frame = !render_frame_host->GetParent();
  int frame_process_id = render_frame_host->GetProcess()->GetID();
  int frame_routing_id = render_frame_host->GetRoutingID();
  Emit("did-fail-load", error_code, "", url, is_main_frame, frame_process_id,
       frame_routing_id);
}

void WebContents::DidStartLoading() {
  Emit("did-start-loading");
}

void WebContents::DidStopLoading() {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  if (web_preferences && web_preferences->ShouldUsePreferredSizeMode())
    web_contents()->GetRenderViewHost()->EnablePreferredSizeMode();

  Emit("did-stop-loading");
}

bool WebContents::EmitNavigationEvent(
    const std::string& event_name,
    content::NavigationHandle* navigation_handle) {
  bool is_main_frame = navigation_handle->IsInMainFrame();
  int frame_process_id = -1, frame_routing_id = -1;
  content::RenderFrameHost* frame_host = GetRenderFrameHost(navigation_handle);
  if (frame_host) {
    frame_process_id = frame_host->GetProcess()->GetID();
    frame_routing_id = frame_host->GetRoutingID();
  }
  bool is_same_document = navigation_handle->IsSameDocument();
  auto url = navigation_handle->GetURL();
  content::RenderFrameHost* initiator_frame_host =
      navigation_handle->GetInitiatorFrameToken().has_value()
          ? content::RenderFrameHost::FromFrameToken(
                content::GlobalRenderFrameHostToken(
                    navigation_handle->GetInitiatorProcessId(),
                    navigation_handle->GetInitiatorFrameToken().value()))
          : nullptr;

  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);

  gin::Handle<gin_helper::internal::Event> event =
      gin_helper::internal::Event::New(isolate);
  v8::Local<v8::Object> event_object = event.ToV8().As<v8::Object>();

  gin_helper::Dictionary dict(isolate, event_object);
  dict.Set("url", url);
  dict.Set("isSameDocument", is_same_document);
  dict.Set("isMainFrame", is_main_frame);
  dict.SetGetter("frame", frame_host);
  dict.SetGetter("initiator", initiator_frame_host);

  EmitWithoutEvent(event_name, event, url, is_same_document, is_main_frame,
                   frame_process_id, frame_routing_id);
  return event->GetDefaultPrevented();
}

void WebContents::Message(bool internal,
                          const std::string& channel,
                          blink::CloneableMessage arguments,
                          content::RenderFrameHost* render_frame_host) {
  TRACE_EVENT1("electron", "WebContents::Message", "channel", channel);
  // webContents.emit('-ipc-message', new Event(), internal, channel,
  // arguments);
  EmitWithSender("-ipc-message", render_frame_host,
                 electron::mojom::ElectronApiIPC::InvokeCallback(), internal,
                 channel, std::move(arguments));
}

void WebContents::Invoke(
    bool internal,
    const std::string& channel,
    blink::CloneableMessage arguments,
    electron::mojom::ElectronApiIPC::InvokeCallback callback,
    content::RenderFrameHost* render_frame_host) {
  TRACE_EVENT1("electron", "WebContents::Invoke", "channel", channel);
  // webContents.emit('-ipc-invoke', new Event(), internal, channel, arguments);
  EmitWithSender("-ipc-invoke", render_frame_host, std::move(callback),
                 internal, channel, std::move(arguments));
}

void WebContents::OnFirstNonEmptyLayout(
    content::RenderFrameHost* render_frame_host) {
  if (render_frame_host == web_contents()->GetPrimaryMainFrame()) {
    Emit("ready-to-show");
  }
}

namespace {

// This object wraps the InvokeCallback so that if it gets GC'd by V8, we can
// still call the callback and send an error. Not doing so causes a Mojo DCHECK,
// since Mojo requires callbacks to be called before they are destroyed.
class ReplyChannel : public gin::Wrappable<ReplyChannel> {
 public:
  using InvokeCallback = electron::mojom::ElectronApiIPC::InvokeCallback;
  static gin::Handle<ReplyChannel> Create(v8::Isolate* isolate,
                                          InvokeCallback callback) {
    return gin::CreateHandle(isolate, new ReplyChannel(std::move(callback)));
  }

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override {
    return gin::Wrappable<ReplyChannel>::GetObjectTemplateBuilder(isolate)
        .SetMethod("sendReply", &ReplyChannel::SendReply);
  }
  const char* GetTypeName() override { return "ReplyChannel"; }

  void SendError(const std::string& msg) {
    v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
    // If there's no current context, it means we're shutting down, so we
    // don't need to send an event.
    if (!isolate->GetCurrentContext().IsEmpty()) {
      v8::HandleScope scope(isolate);
      auto message = gin::DataObjectBuilder(isolate).Set("error", msg).Build();
      SendReply(isolate, message);
    }
  }

 private:
  explicit ReplyChannel(InvokeCallback callback)
      : callback_(std::move(callback)) {}
  ~ReplyChannel() override {
    if (callback_)
      SendError("reply was never sent");
  }

  bool SendReply(v8::Isolate* isolate, v8::Local<v8::Value> arg) {
    if (!callback_)
      return false;
    blink::CloneableMessage message;
    if (!gin::ConvertFromV8(isolate, arg, &message)) {
      return false;
    }

    std::move(callback_).Run(std::move(message));
    return true;
  }

  InvokeCallback callback_;
};

gin::WrapperInfo ReplyChannel::kWrapperInfo = {gin::kEmbedderNativeGin};

}  // namespace

gin::Handle<gin_helper::internal::Event> WebContents::MakeEventWithSender(
    v8::Isolate* isolate,
    content::RenderFrameHost* frame,
    electron::mojom::ElectronApiIPC::InvokeCallback callback) {
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    if (callback) {
      // We must always invoke the callback if present.
      ReplyChannel::Create(isolate, std::move(callback))
          ->SendError("WebContents was destroyed");
    }
    return gin::Handle<gin_helper::internal::Event>();
  }
  gin::Handle<gin_helper::internal::Event> event =
      gin_helper::internal::Event::New(isolate);
  gin_helper::Dictionary dict(isolate, event.ToV8().As<v8::Object>());
  if (callback)
    dict.Set("_replyChannel",
             ReplyChannel::Create(isolate, std::move(callback)));
  if (frame) {
    dict.Set("frameId", frame->GetRoutingID());
    dict.Set("processId", frame->GetProcess()->GetID());
  }
  return event;
}

void WebContents::ReceivePostMessage(
    const std::string& channel,
    blink::TransferableMessage message,
    content::RenderFrameHost* render_frame_host) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  auto wrapped_ports =
      MessagePort::EntanglePorts(isolate, std::move(message.ports));
  v8::Local<v8::Value> message_value =
      electron::DeserializeV8Value(isolate, message);
  EmitWithSender("-ipc-ports", render_frame_host,
                 electron::mojom::ElectronApiIPC::InvokeCallback(), false,
                 channel, message_value, std::move(wrapped_ports));
}

void WebContents::MessageSync(
    bool internal,
    const std::string& channel,
    blink::CloneableMessage arguments,
    electron::mojom::ElectronApiIPC::MessageSyncCallback callback,
    content::RenderFrameHost* render_frame_host) {
  TRACE_EVENT1("electron", "WebContents::MessageSync", "channel", channel);
  // webContents.emit('-ipc-message-sync', new Event(sender, message), internal,
  // channel, arguments);
  EmitWithSender("-ipc-message-sync", render_frame_host, std::move(callback),
                 internal, channel, std::move(arguments));
}

void WebContents::MessageHost(const std::string& channel,
                              blink::CloneableMessage arguments,
                              content::RenderFrameHost* render_frame_host) {
  TRACE_EVENT1("electron", "WebContents::MessageHost", "channel", channel);
  // webContents.emit('ipc-message-host', new Event(), channel, args);
  EmitWithSender("ipc-message-host", render_frame_host,
                 electron::mojom::ElectronApiIPC::InvokeCallback(), channel,
                 std::move(arguments));
}

void WebContents::DraggableRegionsChanged(
    const std::vector<blink::mojom::DraggableRegionPtr>& regions,
    content::WebContents* contents) {
  if (owner_window() && owner_window()->has_frame()) {
    return;
  }

  draggable_region_ = DraggableRegionsToSkRegion(regions);
}

void WebContents::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  EmitNavigationEvent("did-start-navigation", navigation_handle);
}

void WebContents::DidRedirectNavigation(
    content::NavigationHandle* navigation_handle) {
  EmitNavigationEvent("did-redirect-navigation", navigation_handle);
}

void WebContents::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  // Don't focus content in an inactive window.
  if (!owner_window())
    return;
#if BUILDFLAG(IS_MAC)
  if (!owner_window()->IsActive())
    return;
#else
  if (!owner_window()->widget()->IsActive())
    return;
#endif
  // Don't focus content after subframe navigations.
  if (!navigation_handle->IsInMainFrame())
    return;
  // Only focus for top-level contents.
  if (type_ != Type::kBrowserWindow)
    return;
  web_contents()->SetInitialFocus();
}

void WebContents::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (owner_window_) {
    owner_window_->NotifyLayoutWindowControlsOverlay();
  }

  if (!navigation_handle->HasCommitted())
    return;
  bool is_main_frame = navigation_handle->IsInMainFrame();
  content::RenderFrameHost* frame_host =
      navigation_handle->GetRenderFrameHost();
  int frame_process_id = -1, frame_routing_id = -1;
  if (frame_host) {
    frame_process_id = frame_host->GetProcess()->GetID();
    frame_routing_id = frame_host->GetRoutingID();
  }
  if (!navigation_handle->IsErrorPage()) {
    // FIXME: All the Emit() calls below could potentially result in |this|
    // being destroyed (by JS listening for the event and calling
    // webContents.destroy()).
    auto url = navigation_handle->GetURL();
    bool is_same_document = navigation_handle->IsSameDocument();
    if (is_same_document) {
      Emit("did-navigate-in-page", url, is_main_frame, frame_process_id,
           frame_routing_id);
    } else {
      const net::HttpResponseHeaders* http_response =
          navigation_handle->GetResponseHeaders();
      std::string http_status_text;
      int http_response_code = -1;
      if (http_response) {
        http_status_text = http_response->GetStatusText();
        http_response_code = http_response->response_code();
      }
      Emit("did-frame-navigate", url, http_response_code, http_status_text,
           is_main_frame, frame_process_id, frame_routing_id);
      if (is_main_frame) {
        Emit("did-navigate", url, http_response_code, http_status_text);
      }
    }
    if (is_guest())
      Emit("load-commit", url, is_main_frame);
  } else {
    auto url = navigation_handle->GetURL();
    int code = navigation_handle->GetNetErrorCode();
    auto description = net::ErrorToShortString(code);
    Emit("did-fail-provisional-load", code, description, url, is_main_frame,
         frame_process_id, frame_routing_id);

    // Do not emit "did-fail-load" for canceled requests.
    if (code != net::ERR_ABORTED) {
      EmitWarning(
          node::Environment::GetCurrent(JavascriptEnvironment::GetIsolate()),
          "Failed to load URL: " + url.possibly_invalid_spec() +
              " with error: " + description,
          "electron");
      Emit("did-fail-load", code, description, url, is_main_frame,
           frame_process_id, frame_routing_id);
    }
  }
  content::NavigationEntry* entry = navigation_handle->GetNavigationEntry();

  // This check is needed due to an issue in Chromium
  // Check the Chromium issue to keep updated:
  // https://bugs.chromium.org/p/chromium/issues/detail?id=1178663
  // If a history entry has been made and the forward/back call has been made,
  // proceed with setting the new title
  if (entry && (entry->GetTransitionType() & ui::PAGE_TRANSITION_FORWARD_BACK))
    WebContents::TitleWasSet(entry);
}

void WebContents::TitleWasSet(content::NavigationEntry* entry) {
  std::u16string final_title;
  bool explicit_set = true;
  if (entry) {
    auto title = entry->GetTitle();
    auto url = entry->GetURL();
    if (url.SchemeIsFile() && title.empty()) {
      final_title = base::UTF8ToUTF16(url.ExtractFileName());
      explicit_set = false;
    } else {
      final_title = title;
    }
  } else {
    final_title = web_contents()->GetTitle();
  }
  for (ExtendedWebContentsObserver& observer : observers_)
    observer.OnPageTitleUpdated(final_title, explicit_set);
  Emit("page-title-updated", final_title, explicit_set);
}

void WebContents::DidUpdateFaviconURL(
    content::RenderFrameHost* render_frame_host,
    const std::vector<blink::mojom::FaviconURLPtr>& urls) {
  std::set<GURL> unique_urls;
  for (const auto& iter : urls) {
    if (iter->icon_type != blink::mojom::FaviconIconType::kFavicon)
      continue;
    const GURL& url = iter->icon_url;
    if (url.is_valid())
      unique_urls.insert(url);
  }
  Emit("page-favicon-updated", unique_urls);
}

void WebContents::DevToolsReloadPage() {
  Emit("devtools-reload-page");
}

void WebContents::DevToolsFocused() {
  Emit("devtools-focused");
}

void WebContents::DevToolsOpened() {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  DCHECK(inspectable_web_contents_);
  DCHECK(inspectable_web_contents_->GetDevToolsWebContents());
  auto handle = FromOrCreate(
      isolate, inspectable_web_contents_->GetDevToolsWebContents());
  devtools_web_contents_.Reset(isolate, handle.ToV8());

  // Set inspected tabID.
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "setInspectedTabId", base::Value(ID()));

  // Inherit owner window in devtools when it doesn't have one.
  auto* devtools = inspectable_web_contents_->GetDevToolsWebContents();
  bool has_window = devtools->GetUserData(NativeWindowRelay::UserDataKey());
  if (owner_window() && !has_window)
    handle->SetOwnerWindow(devtools, owner_window());

  Emit("devtools-opened");
}

void WebContents::DevToolsClosed() {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope handle_scope(isolate);
  devtools_web_contents_.Reset();

  Emit("devtools-closed");
}

void WebContents::DevToolsResized() {
  for (ExtendedWebContentsObserver& observer : observers_)
    observer.OnDevToolsResized();
}

void WebContents::SetOwnerWindow(NativeWindow* owner_window) {
  SetOwnerWindow(GetWebContents(), owner_window);
}

void WebContents::SetOwnerBaseWindow(std::optional<BaseWindow*> owner_window) {
  SetOwnerWindow(GetWebContents(),
                 owner_window ? (*owner_window)->window() : nullptr);
}

void WebContents::SetOwnerWindow(content::WebContents* web_contents,
                                 NativeWindow* owner_window) {
  if (owner_window_) {
    owner_window_->RemoveBackgroundThrottlingSource(this);
  }

  if (owner_window) {
    owner_window_ = owner_window->GetWeakPtr();
    NativeWindowRelay::CreateForWebContents(web_contents,
                                            owner_window->GetWeakPtr());
    owner_window_->AddBackgroundThrottlingSource(this);
  } else {
    owner_window_ = nullptr;
    web_contents->RemoveUserData(NativeWindowRelay::UserDataKey());
  }
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetNativeWindow(owner_window);
}

content::WebContents* WebContents::GetWebContents() const {
  if (!inspectable_web_contents_)
    return nullptr;
  return inspectable_web_contents_->GetWebContents();
}

content::WebContents* WebContents::GetDevToolsWebContents() const {
  if (!inspectable_web_contents_)
    return nullptr;
  return inspectable_web_contents_->GetDevToolsWebContents();
}

void WebContents::WebContentsDestroyed() {
  // Clear the pointer stored in wrapper.
  if (GetAllWebContents().Lookup(id_))
    GetAllWebContents().Remove(id_);
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper))
    return;
  wrapper->SetAlignedPointerInInternalField(0, nullptr);

  // Tell WebViewGuestDelegate that the WebContents has been destroyed.
  if (guest_delegate_)
    guest_delegate_->WillDestroy();

  Observe(nullptr);
  Emit("destroyed");
}

void WebContents::NavigationEntryCommitted(
    const content::LoadCommittedDetails& details) {
  Emit("navigation-entry-committed", details.entry->GetURL(),
       details.is_same_document, details.did_replace_entry);
}

bool WebContents::GetBackgroundThrottling() const {
  return background_throttling_;
}

void WebContents::SetBackgroundThrottling(bool allowed) {
  background_throttling_ = allowed;

  if (owner_window_) {
    owner_window_->UpdateBackgroundThrottlingState();
  }

  auto* rfh = web_contents()->GetPrimaryMainFrame();
  if (!rfh)
    return;

  auto* rwhv = rfh->GetView();
  if (!rwhv)
    return;

  auto* rwh_impl =
      static_cast<content::RenderWidgetHostImpl*>(rwhv->GetRenderWidgetHost());
  if (!rwh_impl)
    return;

  rwh_impl->disable_hidden_ = !background_throttling_;
  web_contents()->GetRenderViewHost()->SetSchedulerThrottling(allowed);

  if (rwh_impl->is_hidden()) {
    rwh_impl->WasShown({});
  }
}

int WebContents::GetProcessID() const {
  return web_contents()->GetPrimaryMainFrame()->GetProcess()->GetID();
}

base::ProcessId WebContents::GetOSProcessID() const {
  base::ProcessHandle process_handle = web_contents()
                                           ->GetPrimaryMainFrame()
                                           ->GetProcess()
                                           ->GetProcess()
                                           .Handle();
  return base::GetProcId(process_handle);
}

bool WebContents::Equal(const WebContents* web_contents) const {
  return ID() == web_contents->ID();
}

GURL WebContents::GetURL() const {
  return web_contents()->GetLastCommittedURL();
}

void WebContents::LoadURL(const GURL& url,
                          const gin_helper::Dictionary& options) {
  if (!url.is_valid() || url.spec().size() > url::kMaxURLChars) {
    Emit("did-fail-load", static_cast<int>(net::ERR_INVALID_URL),
         net::ErrorToShortString(net::ERR_INVALID_URL),
         url.possibly_invalid_spec(), true);
    return;
  }

  content::NavigationController::LoadURLParams params(url);

  if (!options.Get("httpReferrer", &params.referrer)) {
    GURL http_referrer;
    if (options.Get("httpReferrer", &http_referrer))
      params.referrer =
          content::Referrer(http_referrer.GetAsReferrer(),
                            network::mojom::ReferrerPolicy::kDefault);
  }

  std::string user_agent;
  if (options.Get("userAgent", &user_agent))
    SetUserAgent(user_agent);

  std::string extra_headers;
  if (options.Get("extraHeaders", &extra_headers))
    params.extra_headers = extra_headers;

  scoped_refptr<network::ResourceRequestBody> body;
  if (options.Get("postData", &body)) {
    params.post_data = body;
    params.load_type = content::NavigationController::LOAD_TYPE_HTTP_POST;
  }

  GURL base_url_for_data_url;
  if (options.Get("baseURLForDataURL", &base_url_for_data_url)) {
    params.base_url_for_data_url = base_url_for_data_url;
    params.load_type = content::NavigationController::LOAD_TYPE_DATA;
  }

  bool reload_ignoring_cache = false;
  if (options.Get("reloadIgnoringCache", &reload_ignoring_cache) &&
      reload_ignoring_cache) {
    params.reload_type = content::ReloadType::BYPASSING_CACHE;
  }

  // Calling LoadURLWithParams() can trigger JS which destroys |this|.
  auto weak_this = GetWeakPtr();

  params.transition_type = ui::PageTransitionFromInt(
      ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
  params.override_user_agent = content::NavigationController::UA_OVERRIDE_TRUE;

  // It's not safe to start a new navigation or otherwise discard the current
  // one while the call that started it is still on the stack. See
  // http://crbug.com/347742.
  auto& ctrl_impl = static_cast<content::NavigationControllerImpl&>(
      web_contents()->GetController());
  if (ctrl_impl.in_navigate_to_pending_entry()) {
    Emit("did-fail-load", static_cast<int>(net::ERR_FAILED),
         net::ErrorToShortString(net::ERR_FAILED), url.possibly_invalid_spec(),
         true);
    return;
  }

  // Discard non-committed entries to ensure we don't re-use a pending entry.
  web_contents()->GetController().DiscardNonCommittedEntries();
  web_contents()->GetController().LoadURLWithParams(params);

  // ⚠️WARNING!⚠️
  // LoadURLWithParams() triggers JS events which can call destroy() on |this|.
  // It's not safe to assume that |this| points to valid memory at this point.
  if (!weak_this || !web_contents())
    return;

  // Required to make beforeunload handler work.
  NotifyUserActivation();
}

// TODO(MarshallOfSound): Figure out what we need to do with post data here, I
// believe the default behavior when we pass "true" is to phone out to the
// delegate and then the controller expects this method to be called again with
// "false" if the user approves the reload.  For now this would result in
// ".reload()" calls on POST data domains failing silently.  Passing false would
// result in them succeeding, but reposting which although more correct could be
// considering a breaking change.
void WebContents::Reload() {
  web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                         /* check_for_repost */ true);
}

void WebContents::ReloadIgnoringCache() {
  web_contents()->GetController().Reload(content::ReloadType::BYPASSING_CACHE,
                                         /* check_for_repost */ true);
}

void WebContents::DownloadURL(const GURL& url, gin::Arguments* args) {
  std::map<std::string, std::string> headers;
  gin_helper::Dictionary options;
  if (args->GetNext(&options)) {
    if (options.Has("headers") && !options.Get("headers", &headers)) {
      args->ThrowTypeError("Invalid value for headers - must be an object");
      return;
    }
  }

  std::unique_ptr<download::DownloadUrlParameters> download_params(
      content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(
          web_contents(), url, MISSING_TRAFFIC_ANNOTATION));
  for (const auto& [name, value] : headers) {
    download_params->add_request_header(name, value);
  }

  auto* download_manager =
      web_contents()->GetBrowserContext()->GetDownloadManager();
  download_manager->DownloadUrl(std::move(download_params));
}

std::u16string WebContents::GetTitle() const {
  return web_contents()->GetTitle();
}

bool WebContents::IsLoading() const {
  return web_contents()->IsLoading();
}

bool WebContents::IsLoadingMainFrame() const {
  return web_contents()->ShouldShowLoadingUI();
}

bool WebContents::IsWaitingForResponse() const {
  return web_contents()->IsWaitingForResponse();
}

void WebContents::Stop() {
  web_contents()->Stop();
}

bool WebContents::CanGoBack() const {
  return web_contents()->GetController().CanGoBack();
}

void WebContents::GoBack() {
  if (CanGoBack())
    web_contents()->GetController().GoBack();
}

bool WebContents::CanGoForward() const {
  return web_contents()->GetController().CanGoForward();
}

void WebContents::GoForward() {
  if (CanGoForward())
    web_contents()->GetController().GoForward();
}

bool WebContents::CanGoToOffset(int offset) const {
  return web_contents()->GetController().CanGoToOffset(offset);
}

void WebContents::GoToOffset(int offset) {
  if (CanGoToOffset(offset))
    web_contents()->GetController().GoToOffset(offset);
}

bool WebContents::CanGoToIndex(int index) const {
  return index >= 0 && index < GetHistoryLength();
}

void WebContents::GoToIndex(int index) {
  if (CanGoToIndex(index))
    web_contents()->GetController().GoToIndex(index);
}

int WebContents::GetActiveIndex() const {
  return web_contents()->GetController().GetCurrentEntryIndex();
}

content::NavigationEntry* WebContents::GetNavigationEntryAtIndex(
    int index) const {
  return web_contents()->GetController().GetEntryAtIndex(index);
}

void WebContents::ClearHistory() {
  // In some rare cases (normally while there is no real history) we are in a
  // state where we can't prune navigation entries
  if (web_contents()->GetController().CanPruneAllButLastCommitted()) {
    web_contents()->GetController().PruneAllButLastCommitted();
  }
}

int WebContents::GetHistoryLength() const {
  return web_contents()->GetController().GetEntryCount();
}

const std::string WebContents::GetWebRTCIPHandlingPolicy() const {
  return web_contents()->GetMutableRendererPrefs()->webrtc_ip_handling_policy;
}

void WebContents::SetWebRTCIPHandlingPolicy(
    const std::string& webrtc_ip_handling_policy) {
  if (GetWebRTCIPHandlingPolicy() == webrtc_ip_handling_policy)
    return;
  web_contents()->GetMutableRendererPrefs()->webrtc_ip_handling_policy =
      webrtc_ip_handling_policy;

  web_contents()->SyncRendererPrefs();
}

v8::Local<v8::Value> WebContents::GetWebRTCUDPPortRange(
    v8::Isolate* isolate) const {
  auto* prefs = web_contents()->GetMutableRendererPrefs();

  auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("min", static_cast<uint32_t>(prefs->webrtc_udp_min_port));
  dict.Set("max", static_cast<uint32_t>(prefs->webrtc_udp_max_port));
  return dict.GetHandle();
}

void WebContents::SetWebRTCUDPPortRange(gin::Arguments* args) {
  uint32_t min = 0, max = 0;
  gin_helper::Dictionary range;

  if (!args->GetNext(&range) || !range.Get("min", &min) ||
      !range.Get("max", &max)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("'min' and 'max' are both required");
    return;
  }

  if ((0 == min && 0 != max) || max > UINT16_MAX) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError(
            "'min' and 'max' must be in the (0, 65535] range or [0, 0]");
    return;
  }
  if (min > max) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("'max' must be greater than or equal to 'min'");
    return;
  }

  auto* prefs = web_contents()->GetMutableRendererPrefs();

  if (prefs->webrtc_udp_min_port == static_cast<uint16_t>(min) &&
      prefs->webrtc_udp_max_port == static_cast<uint16_t>(max)) {
    return;
  }

  prefs->webrtc_udp_min_port = min;
  prefs->webrtc_udp_max_port = max;

  web_contents()->SyncRendererPrefs();
}

std::string WebContents::GetMediaSourceID(
    content::WebContents* request_web_contents) {
  auto* frame_host = web_contents()->GetPrimaryMainFrame();
  if (!frame_host)
    return std::string();

  content::DesktopMediaID media_id(
      content::DesktopMediaID::TYPE_WEB_CONTENTS,
      content::DesktopMediaID::kNullId,
      content::WebContentsMediaCaptureId(frame_host->GetProcess()->GetID(),
                                         frame_host->GetRoutingID()));

  auto* request_frame_host = request_web_contents->GetPrimaryMainFrame();
  if (!request_frame_host)
    return std::string();

  std::string id =
      content::DesktopStreamsRegistry::GetInstance()->RegisterStream(
          request_frame_host->GetProcess()->GetID(),
          request_frame_host->GetRoutingID(),
          url::Origin::Create(request_frame_host->GetLastCommittedURL()
                                  .DeprecatedGetOriginAsURL()),
          media_id, content::kRegistryStreamTypeTab);

  return id;
}

bool WebContents::IsCrashed() const {
  return web_contents()->IsCrashed();
}

void WebContents::ForcefullyCrashRenderer() {
  content::RenderWidgetHostView* view =
      web_contents()->GetRenderWidgetHostView();
  if (!view)
    return;

  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh)
    return;

  content::RenderProcessHost* rph = rwh->GetProcess();
  if (rph) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    // A generic |CrashDumpHungChildProcess()| is not implemented for Linux.
    // Instead we send an explicit IPC to crash on the renderer's IO thread.
    rph->ForceCrash();
#else
    // Try to generate a crash report for the hung process.
#if !IS_MAS_BUILD()
    CrashDumpHungChildProcess(rph->GetProcess().Handle());
#endif
    rph->Shutdown(content::RESULT_CODE_HUNG);
#endif
  }
}

void WebContents::SetUserAgent(const std::string& user_agent) {
  blink::UserAgentOverride ua_override;
  ua_override.ua_string_override = user_agent;
  if (!user_agent.empty())
    ua_override.ua_metadata_override = embedder_support::GetUserAgentMetadata();

  web_contents()->SetUserAgentOverride(ua_override, false);
}

std::string WebContents::GetUserAgent() {
  return web_contents()->GetUserAgentOverride().ua_string_override;
}

v8::Local<v8::Promise> WebContents::SavePage(
    const base::FilePath& full_file_path,
    const content::SavePageType& save_type) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  gin_helper::Promise<void> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  if (!full_file_path.IsAbsolute()) {
    promise.RejectWithErrorMessage("Path must be absolute");
    return handle;
  }

  auto* handler = new SavePageHandler(web_contents(), std::move(promise));
  handler->Handle(full_file_path, save_type);

  return handle;
}

void WebContents::OpenDevTools(gin::Arguments* args) {
  if (type_ == Type::kRemote)
    return;

  if (!enable_devtools_)
    return;

  std::string state;
  if (type_ == Type::kWebView || type_ == Type::kBackgroundPage ||
      !owner_window()) {
    state = "detach";
  }

  bool activate = true;
  std::string title;
  if (args && args->Length() == 1) {
    gin_helper::Dictionary options;
    if (args->GetNext(&options)) {
      options.Get("mode", &state);
      options.Get("activate", &activate);
      options.Get("title", &title);
    }
  }

  DCHECK(inspectable_web_contents_);
  inspectable_web_contents_->SetDockState(state);
  inspectable_web_contents_->SetDevToolsTitle(base::UTF8ToUTF16(title));
  inspectable_web_contents_->ShowDevTools(activate);
}

void WebContents::CloseDevTools() {
  if (type_ == Type::kRemote)
    return;

  DCHECK(inspectable_web_contents_);
  inspectable_web_contents_->CloseDevTools();
}

bool WebContents::IsDevToolsOpened() {
  if (type_ == Type::kRemote)
    return false;

  DCHECK(inspectable_web_contents_);
  return inspectable_web_contents_->IsDevToolsViewShowing();
}

std::u16string WebContents::GetDevToolsTitle() {
  if (type_ == Type::kRemote)
    return std::u16string();

  DCHECK(inspectable_web_contents_);
  return inspectable_web_contents_->GetDevToolsTitle();
}

void WebContents::SetDevToolsTitle(const std::u16string& title) {
  inspectable_web_contents_->SetDevToolsTitle(title);
}

bool WebContents::IsDevToolsFocused() {
  if (type_ == Type::kRemote)
    return false;

  DCHECK(inspectable_web_contents_);
  return inspectable_web_contents_->GetView()->IsDevToolsViewFocused();
}

void WebContents::EnableDeviceEmulation(
    const blink::DeviceEmulationParams& params) {
  if (type_ == Type::kRemote)
    return;

  DCHECK(web_contents());
  auto* frame_host = web_contents()->GetPrimaryMainFrame();
  if (frame_host) {
    auto* widget_host_impl = static_cast<content::RenderWidgetHostImpl*>(
        frame_host->GetView()->GetRenderWidgetHost());
    if (widget_host_impl) {
      auto& frame_widget = widget_host_impl->GetAssociatedFrameWidget();
      frame_widget->EnableDeviceEmulation(params);
    }
  }
}

void WebContents::DisableDeviceEmulation() {
  if (type_ == Type::kRemote)
    return;

  DCHECK(web_contents());
  auto* frame_host = web_contents()->GetPrimaryMainFrame();
  if (frame_host) {
    auto* widget_host_impl = static_cast<content::RenderWidgetHostImpl*>(
        frame_host->GetView()->GetRenderWidgetHost());
    if (widget_host_impl) {
      auto& frame_widget = widget_host_impl->GetAssociatedFrameWidget();
      frame_widget->DisableDeviceEmulation();
    }
  }
}

void WebContents::ToggleDevTools() {
  if (IsDevToolsOpened())
    CloseDevTools();
  else
    OpenDevTools(nullptr);
}

void WebContents::InspectElement(int x, int y) {
  if (type_ == Type::kRemote)
    return;

  if (!enable_devtools_)
    return;

  DCHECK(inspectable_web_contents_);
  if (!inspectable_web_contents_->GetDevToolsWebContents())
    OpenDevTools(nullptr);
  inspectable_web_contents_->InspectElement(x, y);
}

void WebContents::InspectSharedWorkerById(const std::string& workerId) {
  if (type_ == Type::kRemote)
    return;

  if (!enable_devtools_)
    return;

  for (const auto& agent_host : content::DevToolsAgentHost::GetOrCreateAll()) {
    if (agent_host->GetType() ==
        content::DevToolsAgentHost::kTypeSharedWorker) {
      if (agent_host->GetId() == workerId) {
        OpenDevTools(nullptr);
        inspectable_web_contents_->AttachTo(agent_host);
        break;
      }
    }
  }
}

std::vector<scoped_refptr<content::DevToolsAgentHost>>
WebContents::GetAllSharedWorkers() {
  std::vector<scoped_refptr<content::DevToolsAgentHost>> shared_workers;

  if (type_ == Type::kRemote)
    return shared_workers;

  if (!enable_devtools_)
    return shared_workers;

  for (const auto& agent_host : content::DevToolsAgentHost::GetOrCreateAll()) {
    if (agent_host->GetType() ==
        content::DevToolsAgentHost::kTypeSharedWorker) {
      shared_workers.push_back(agent_host);
    }
  }
  return shared_workers;
}

void WebContents::InspectSharedWorker() {
  if (type_ == Type::kRemote)
    return;

  if (!enable_devtools_)
    return;

  for (const auto& agent_host : content::DevToolsAgentHost::GetOrCreateAll()) {
    if (agent_host->GetType() ==
        content::DevToolsAgentHost::kTypeSharedWorker) {
      OpenDevTools(nullptr);
      inspectable_web_contents_->AttachTo(agent_host);
      break;
    }
  }
}

void WebContents::InspectServiceWorker() {
  if (type_ == Type::kRemote)
    return;

  if (!enable_devtools_)
    return;

  for (const auto& agent_host : content::DevToolsAgentHost::GetOrCreateAll()) {
    if (agent_host->GetType() ==
        content::DevToolsAgentHost::kTypeServiceWorker) {
      OpenDevTools(nullptr);
      inspectable_web_contents_->AttachTo(agent_host);
      break;
    }
  }
}

void WebContents::SetIgnoreMenuShortcuts(bool ignore) {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  DCHECK(web_preferences);
  web_preferences->SetIgnoreMenuShortcuts(ignore);
}

void WebContents::SetAudioMuted(bool muted) {
  web_contents()->SetAudioMuted(muted);
}

bool WebContents::IsAudioMuted() {
  return web_contents()->IsAudioMuted();
}

bool WebContents::IsCurrentlyAudible() {
  return web_contents()->IsCurrentlyAudible();
}

#if BUILDFLAG(ENABLE_PRINTING)
void WebContents::OnGetDeviceNameToUse(
    base::Value::Dict print_settings,
    printing::CompletionCallback print_callback,
    // <error, device_name>
    std::pair<std::string, std::u16string> info) {
  // The content::WebContents might be already deleted at this point, and the
  // PrintViewManagerElectron class does not do null check.
  if (!web_contents()) {
    if (print_callback)
      std::move(print_callback).Run(false, "failed");
    return;
  }

  if (!info.first.empty()) {
    if (print_callback)
      std::move(print_callback).Run(false, info.first);
    return;
  }

  // If the user has passed a deviceName use it, otherwise use default printer.
  print_settings.Set(printing::kSettingDeviceName, info.second);

  if (!print_settings.FindInt(printing::kSettingDpiHorizontal)) {
    gfx::Size dpi = GetDefaultPrinterDPI(info.second);
    print_settings.Set(printing::kSettingDpiHorizontal, dpi.width());
    print_settings.Set(printing::kSettingDpiVertical, dpi.height());
  }

  auto* print_view_manager =
      PrintViewManagerElectron::FromWebContents(web_contents());
  if (!print_view_manager)
    return;

  auto* focused_frame = web_contents()->GetFocusedFrame();
  auto* rfh = focused_frame && focused_frame->HasSelection()
                  ? focused_frame
                  : web_contents()->GetPrimaryMainFrame();

  print_view_manager->PrintNow(rfh, std::move(print_settings),
                               std::move(print_callback));
}

void WebContents::Print(gin::Arguments* args) {
  auto options = gin_helper::Dictionary::CreateEmpty(args->isolate());
  base::Value::Dict settings;

  if (args->Length() >= 1 && !args->GetNext(&options)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("webContents.print(): Invalid print settings specified.");
    return;
  }

  printing::CompletionCallback callback;
  if (args->Length() == 2 && !args->GetNext(&callback)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("webContents.print(): Invalid optional callback provided.");
    return;
  }

  // Set optional silent printing.
  bool silent = false;
  options.Get("silent", &silent);
  settings.Set("silent", silent);

  bool print_background = false;
  options.Get("printBackground", &print_background);
  settings.Set(printing::kSettingShouldPrintBackgrounds, print_background);

  // Set custom margin settings
  auto margins = gin_helper::Dictionary::CreateEmpty(args->isolate());
  if (options.Get("margins", &margins)) {
    printing::mojom::MarginType margin_type =
        printing::mojom::MarginType::kDefaultMargins;
    margins.Get("marginType", &margin_type);
    settings.Set(printing::kSettingMarginsType, static_cast<int>(margin_type));

    if (margin_type == printing::mojom::MarginType::kCustomMargins) {
      base::Value::Dict custom_margins;
      int top = 0;
      margins.Get("top", &top);
      custom_margins.Set(printing::kSettingMarginTop, top);
      int bottom = 0;
      margins.Get("bottom", &bottom);
      custom_margins.Set(printing::kSettingMarginBottom, bottom);
      int left = 0;
      margins.Get("left", &left);
      custom_margins.Set(printing::kSettingMarginLeft, left);
      int right = 0;
      margins.Get("right", &right);
      custom_margins.Set(printing::kSettingMarginRight, right);
      settings.Set(printing::kSettingMarginsCustom, std::move(custom_margins));
    }
  } else {
    settings.Set(
        printing::kSettingMarginsType,
        static_cast<int>(printing::mojom::MarginType::kDefaultMargins));
  }

  // Set whether to print color or greyscale
  bool print_color = true;
  options.Get("color", &print_color);
  auto const color_model = print_color ? printing::mojom::ColorModel::kColor
                                       : printing::mojom::ColorModel::kGray;
  settings.Set(printing::kSettingColor, static_cast<int>(color_model));

  // Is the orientation landscape or portrait.
  bool landscape = false;
  options.Get("landscape", &landscape);
  settings.Set(printing::kSettingLandscape, landscape);

  // We set the default to the system's default printer and only update
  // if at the Chromium level if the user overrides.
  // Printer device name as opened by the OS.
  std::u16string device_name;
  options.Get("deviceName", &device_name);

  int scale_factor = 100;
  options.Get("scaleFactor", &scale_factor);
  settings.Set(printing::kSettingScaleFactor, scale_factor);

  int pages_per_sheet = 1;
  options.Get("pagesPerSheet", &pages_per_sheet);
  settings.Set(printing::kSettingPagesPerSheet, pages_per_sheet);

  // True if the user wants to print with collate.
  bool collate = true;
  options.Get("collate", &collate);
  settings.Set(printing::kSettingCollate, collate);

  // The number of individual copies to print
  int copies = 1;
  options.Get("copies", &copies);
  settings.Set(printing::kSettingCopies, copies);

  // Strings to be printed as headers and footers if requested by the user.
  std::string header;
  options.Get("header", &header);
  std::string footer;
  options.Get("footer", &footer);

  if (!(header.empty() && footer.empty())) {
    settings.Set(printing::kSettingHeaderFooterEnabled, true);

    settings.Set(printing::kSettingHeaderFooterTitle, header);
    settings.Set(printing::kSettingHeaderFooterURL, footer);
  } else {
    settings.Set(printing::kSettingHeaderFooterEnabled, false);
  }

  // We don't want to allow the user to enable these settings
  // but we need to set them or a CHECK is hit.
  settings.Set(printing::kSettingPrinterType,
               static_cast<int>(printing::mojom::PrinterType::kLocal));
  settings.Set(printing::kSettingShouldPrintSelectionOnly, false);
  settings.Set(printing::kSettingRasterizePdf, false);

  // Set custom page ranges to print
  std::vector<gin_helper::Dictionary> page_ranges;
  if (options.Get("pageRanges", &page_ranges)) {
    base::Value::List page_range_list;
    for (auto& range : page_ranges) {
      int from, to;
      if (range.Get("from", &from) && range.Get("to", &to)) {
        base::Value::Dict range_dict;
        // Chromium uses 1-based page ranges, so increment each by 1.
        range_dict.Set(printing::kSettingPageRangeFrom, from + 1);
        range_dict.Set(printing::kSettingPageRangeTo, to + 1);
        page_range_list.Append(std::move(range_dict));
      } else {
        continue;
      }
    }
    if (!page_range_list.empty())
      settings.Set(printing::kSettingPageRange, std::move(page_range_list));
  }

  // Duplex type user wants to use.
  printing::mojom::DuplexMode duplex_mode =
      printing::mojom::DuplexMode::kSimplex;
  options.Get("duplexMode", &duplex_mode);
  settings.Set(printing::kSettingDuplexMode, static_cast<int>(duplex_mode));

  // We've already done necessary parameter sanitization at the
  // JS level, so we can simply pass this through.
  base::Value media_size(base::Value::Type::DICT);
  if (options.Get("mediaSize", &media_size))
    settings.Set(printing::kSettingMediaSize, std::move(media_size));

  // Set custom dots per inch (dpi)
  gin_helper::Dictionary dpi_settings;
  if (options.Get("dpi", &dpi_settings)) {
    int horizontal = 72;
    dpi_settings.Get("horizontal", &horizontal);
    settings.Set(printing::kSettingDpiHorizontal, horizontal);
    int vertical = 72;
    dpi_settings.Get("vertical", &vertical);
    settings.Set(printing::kSettingDpiVertical, vertical);
  }

  print_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&GetDeviceNameToUse, device_name),
      base::BindOnce(&WebContents::OnGetDeviceNameToUse,
                     weak_factory_.GetWeakPtr(), std::move(settings),
                     std::move(callback)));
}

// Partially duplicated and modified from
// headless/lib/browser/protocol/page_handler.cc;l=41
v8::Local<v8::Promise> WebContents::PrintToPDF(const base::Value& settings) {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  gin_helper::Promise<v8::Local<v8::Value>> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // This allows us to track headless printing calls.
  auto unique_id = settings.GetDict().FindInt(printing::kPreviewRequestID);
  auto landscape = settings.GetDict().FindBool("landscape");
  auto display_header_footer =
      settings.GetDict().FindBool("displayHeaderFooter");
  auto print_background = settings.GetDict().FindBool("printBackground");
  auto scale = settings.GetDict().FindDouble("scale");
  auto paper_width = settings.GetDict().FindDouble("paperWidth");
  auto paper_height = settings.GetDict().FindDouble("paperHeight");
  auto margin_top = settings.GetDict().FindDouble("marginTop");
  auto margin_bottom = settings.GetDict().FindDouble("marginBottom");
  auto margin_left = settings.GetDict().FindDouble("marginLeft");
  auto margin_right = settings.GetDict().FindDouble("marginRight");
  auto page_ranges = *settings.GetDict().FindString("pageRanges");
  auto header_template = *settings.GetDict().FindString("headerTemplate");
  auto footer_template = *settings.GetDict().FindString("footerTemplate");
  auto prefer_css_page_size = settings.GetDict().FindBool("preferCSSPageSize");
  auto generate_tagged_pdf = settings.GetDict().FindBool("generateTaggedPDF");
  auto generate_document_outline =
      settings.GetDict().FindBool("generateDocumentOutline");

  absl::variant<printing::mojom::PrintPagesParamsPtr, std::string>
      print_pages_params = print_to_pdf::GetPrintPagesParams(
          web_contents()->GetPrimaryMainFrame()->GetLastCommittedURL(),
          landscape, display_header_footer, print_background, scale,
          paper_width, paper_height, margin_top, margin_bottom, margin_left,
          margin_right, std::make_optional(header_template),
          std::make_optional(footer_template), prefer_css_page_size,
          generate_tagged_pdf, generate_document_outline);

  if (absl::holds_alternative<std::string>(print_pages_params)) {
    auto error = absl::get<std::string>(print_pages_params);
    promise.RejectWithErrorMessage("Invalid print parameters: " + error);
    return handle;
  }

  auto* manager = PrintViewManagerElectron::FromWebContents(web_contents());
  if (!manager) {
    promise.RejectWithErrorMessage("Failed to find print manager");
    return handle;
  }

  auto params = std::move(
      absl::get<printing::mojom::PrintPagesParamsPtr>(print_pages_params));
  params->params->document_cookie = unique_id.value_or(0);

  manager->PrintToPdf(web_contents()->GetPrimaryMainFrame(), page_ranges,
                      std::move(params),
                      base::BindOnce(&WebContents::OnPDFCreated, GetWeakPtr(),
                                     std::move(promise)));

  return handle;
}

void WebContents::OnPDFCreated(
    gin_helper::Promise<v8::Local<v8::Value>> promise,
    print_to_pdf::PdfPrintResult print_result,
    scoped_refptr<base::RefCountedMemory> data) {
  if (print_result != print_to_pdf::PdfPrintResult::kPrintSuccess) {
    promise.RejectWithErrorMessage(
        "Failed to generate PDF: " +
        print_to_pdf::PdfPrintResultToString(print_result));
    return;
  }

  v8::Isolate* isolate = promise.isolate();
  gin_helper::Locker locker(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(
      v8::Local<v8::Context>::New(isolate, promise.GetContext()));

  v8::Local<v8::Value> buffer =
      node::Buffer::Copy(isolate, reinterpret_cast<const char*>(data->front()),
                         data->size())
          .ToLocalChecked();

  promise.Resolve(buffer);
}
#endif

void WebContents::AddWorkSpace(gin::Arguments* args,
                               const base::FilePath& path) {
  if (path.empty()) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("path cannot be empty");
    return;
  }
  DevToolsAddFileSystem(std::string(), path);
}

void WebContents::RemoveWorkSpace(gin::Arguments* args,
                                  const base::FilePath& path) {
  if (path.empty()) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("path cannot be empty");
    return;
  }
  DevToolsRemoveFileSystem(path);
}

void WebContents::Undo() {
  web_contents()->Undo();
}

void WebContents::Redo() {
  web_contents()->Redo();
}

void WebContents::Cut() {
  web_contents()->Cut();
}

void WebContents::Copy() {
  web_contents()->Copy();
}

void WebContents::CenterSelection() {
  web_contents()->CenterSelection();
}

void WebContents::Paste() {
  web_contents()->Paste();
}

void WebContents::PasteAndMatchStyle() {
  web_contents()->PasteAndMatchStyle();
}

void WebContents::Delete() {
  web_contents()->Delete();
}

void WebContents::SelectAll() {
  web_contents()->SelectAll();
}

void WebContents::Unselect() {
  web_contents()->CollapseSelection();
}

void WebContents::ScrollToTopOfDocument() {
  web_contents()->ScrollToTopOfDocument();
}

void WebContents::ScrollToBottomOfDocument() {
  web_contents()->ScrollToBottomOfDocument();
}

void WebContents::AdjustSelectionByCharacterOffset(gin::Arguments* args) {
  int start_adjust = 0;
  int end_adjust = 0;

  gin_helper::Dictionary dict;
  if (args->GetNext(&dict)) {
    dict.Get("start", &start_adjust);
    dict.Get("matchCase", &end_adjust);
  }

  // The selection menu is a Chrome-specific piece of UI.
  // TODO(codebytere): maybe surface as an event in the future?
  web_contents()->AdjustSelectionByCharacterOffset(
      start_adjust, end_adjust, false /* show_selection_menu */);
}

void WebContents::Replace(const std::u16string& word) {
  web_contents()->Replace(word);
}

void WebContents::ReplaceMisspelling(const std::u16string& word) {
  web_contents()->ReplaceMisspelling(word);
}

uint32_t WebContents::FindInPage(gin::Arguments* args) {
  std::u16string search_text;
  if (!args->GetNext(&search_text) || search_text.empty()) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("Must provide a non-empty search content");
    return 0;
  }

  uint32_t request_id = ++find_in_page_request_id_;
  gin_helper::Dictionary dict;
  auto options = blink::mojom::FindOptions::New();
  if (args->GetNext(&dict)) {
    dict.Get("forward", &options->forward);
    dict.Get("matchCase", &options->match_case);
    dict.Get("findNext", &options->new_session);
  }

  web_contents()->Find(request_id, search_text, std::move(options));
  return request_id;
}

void WebContents::StopFindInPage(content::StopFindAction action) {
  web_contents()->StopFinding(action);
}

void WebContents::ShowDefinitionForSelection() {
#if BUILDFLAG(IS_MAC)
  auto* const view = web_contents()->GetRenderWidgetHostView();
  if (view)
    view->ShowDefinitionForSelection();
#endif
}

void WebContents::CopyImageAt(int x, int y) {
  auto* const host = web_contents()->GetPrimaryMainFrame();
  if (host)
    host->CopyImageAt(x, y);
}

void WebContents::Focus() {
  // Focusing on WebContents does not automatically focus the window on macOS
  // and Linux, do it manually to match the behavior on Windows.
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  if (owner_window())
    owner_window()->Focus(true);
#endif
  web_contents()->Focus();
}

#if !BUILDFLAG(IS_MAC)
bool WebContents::IsFocused() const {
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!view)
    return false;

  if (type() != Type::kBackgroundPage) {
    auto* window = web_contents()->GetNativeView()->GetToplevelWindow();
    if (window && !window->IsVisible())
      return false;
  }

  return view->HasFocus();
}
#endif

void WebContents::SendInputEvent(v8::Isolate* isolate,
                                 v8::Local<v8::Value> input_event) {
  content::RenderWidgetHostView* view =
      web_contents()->GetRenderWidgetHostView();
  if (!view)
    return;

  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  blink::WebInputEvent::Type type =
      gin::GetWebInputEventType(isolate, input_event);
  if (blink::WebInputEvent::IsMouseEventType(type)) {
    blink::WebMouseEvent mouse_event;
    if (gin::ConvertFromV8(isolate, input_event, &mouse_event)) {
      if (IsOffScreen()) {
        GetOffScreenRenderWidgetHostView()->SendMouseEvent(mouse_event);
      } else {
        rwh->ForwardMouseEvent(mouse_event);
      }
      return;
    }
  } else if (blink::WebInputEvent::IsKeyboardEventType(type)) {
    input::NativeWebKeyboardEvent keyboard_event(
        blink::WebKeyboardEvent::Type::kRawKeyDown,
        blink::WebInputEvent::Modifiers::kNoModifiers, ui::EventTimeForNow());
    if (gin::ConvertFromV8(isolate, input_event, &keyboard_event)) {
      // For backwards compatibility, convert `kKeyDown` to `kRawKeyDown`.
      if (keyboard_event.GetType() == blink::WebKeyboardEvent::Type::kKeyDown)
        keyboard_event.SetType(blink::WebKeyboardEvent::Type::kRawKeyDown);
      rwh->ForwardKeyboardEvent(keyboard_event);
      return;
    }
  } else if (type == blink::WebInputEvent::Type::kMouseWheel) {
    blink::WebMouseWheelEvent mouse_wheel_event;
    if (gin::ConvertFromV8(isolate, input_event, &mouse_wheel_event)) {
      if (IsOffScreen()) {
        GetOffScreenRenderWidgetHostView()->SendMouseWheelEvent(
            mouse_wheel_event);
      } else {
        // Chromium expects phase info in wheel events (and applies a
        // DCHECK to verify it). See: https://crbug.com/756524.
        mouse_wheel_event.phase = blink::WebMouseWheelEvent::kPhaseBegan;
        mouse_wheel_event.dispatch_type =
            blink::WebInputEvent::DispatchType::kBlocking;
        rwh->ForwardWheelEvent(mouse_wheel_event);

        // Send a synthetic wheel event with phaseEnded to finish scrolling.
        mouse_wheel_event.has_synthetic_phase = true;
        mouse_wheel_event.delta_x = 0;
        mouse_wheel_event.delta_y = 0;
        mouse_wheel_event.phase = blink::WebMouseWheelEvent::kPhaseEnded;
        mouse_wheel_event.dispatch_type =
            blink::WebInputEvent::DispatchType::kEventNonBlocking;
        rwh->ForwardWheelEvent(mouse_wheel_event);
      }
      return;
    }
  }

  isolate->ThrowException(
      v8::Exception::Error(gin::StringToV8(isolate, "Invalid event object")));
}

void WebContents::BeginFrameSubscription(gin::Arguments* args) {
  bool only_dirty = false;
  FrameSubscriber::FrameCaptureCallback callback;

  if (args->Length() > 1) {
    if (!args->GetNext(&only_dirty)) {
      args->ThrowError();
      return;
    }
  }
  if (!args->GetNext(&callback)) {
    args->ThrowError();
    return;
  }

  frame_subscriber_ =
      std::make_unique<FrameSubscriber>(web_contents(), callback, only_dirty);
}

void WebContents::EndFrameSubscription() {
  frame_subscriber_.reset();
}

void WebContents::StartDrag(const gin_helper::Dictionary& item,
                            gin::Arguments* args) {
  base::FilePath file;
  std::vector<base::FilePath> files;
  if (!item.Get("files", &files) && item.Get("file", &file)) {
    files.push_back(file);
  }

  v8::Local<v8::Value> icon_value;
  if (!item.Get("icon", &icon_value)) {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("'icon' parameter is required");
    return;
  }

  NativeImage* icon = nullptr;
  if (!NativeImage::TryConvertNativeImage(args->isolate(), icon_value, &icon) ||
      icon->image().IsEmpty()) {
    return;
  }

  // Start dragging.
  if (!files.empty()) {
    base::CurrentThread::ScopedAllowApplicationTasksInNativeNestedLoop allow;
    DragFileItems(files, icon->image(), web_contents()->GetNativeView());
  } else {
    gin_helper::ErrorThrower(args->isolate())
        .ThrowError("Must specify either 'file' or 'files' option");
  }
}

v8::Local<v8::Promise> WebContents::CapturePage(gin::Arguments* args) {
  gin_helper::Promise<gfx::Image> promise(args->isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  gfx::Rect rect;
  args->GetNext(&rect);

  bool stay_hidden = false;
  bool stay_awake = false;
  gfx::Size bitmap_size;
  if (args && args->Length() == 2) {
    gin_helper::Dictionary options;
    if (args->GetNext(&options)) {
      options.Get("stayHidden", &stay_hidden);
      options.Get("stayAwake", &stay_awake);
      options.Get("outputSize", &bitmap_size);
    }
  }

  auto* const view = web_contents()->GetRenderWidgetHostView();
  if (!view || view->GetViewBounds().size().IsEmpty()) {
    promise.Resolve(gfx::Image());
    return handle;
  }

  if (!view->IsSurfaceAvailableForCopy()) {
    promise.RejectWithErrorMessage(
        "Current display surface not available for capture");
    return handle;
  }

  auto capture_handle = web_contents()->IncrementCapturerCount(
      rect.size(), stay_hidden, stay_awake);

  // Capture full page if user doesn't specify a |rect|.
  const gfx::Size view_size =
      rect.IsEmpty() ? view->GetViewBounds().size() : rect.size();

  if (bitmap_size.IsEmpty()) {
    // By default, the requested bitmap size is the view size in screen
    // coordinates.  However, if there's more pixel detail available on the
    // current system, increase the requested bitmap size to capture it all.
    const gfx::NativeView native_view = view->GetNativeView();
    const float scale = display::Screen::GetScreen()
                            ->GetDisplayNearestView(native_view)
                            .device_scale_factor();
    if (scale > 1.0f)
      bitmap_size = gfx::ScaleToCeiledSize(view_size, scale);
  }

  view->CopyFromSurface(gfx::Rect(rect.origin(), view_size), bitmap_size,
                        base::BindOnce(&OnCapturePageDone, std::move(promise),
                                       std::move(capture_handle)));
  return handle;
}

bool WebContents::IsBeingCaptured() {
  return web_contents()->IsBeingCaptured();
}

void WebContents::OnCursorChanged(const ui::Cursor& cursor) {
  if (cursor.type() == ui::mojom::CursorType::kCustom) {
    Emit("cursor-changed", CursorTypeToString(cursor.type()),
         gfx::Image::CreateFrom1xBitmap(cursor.custom_bitmap()),
         cursor.image_scale_factor(),
         gfx::Size(cursor.custom_bitmap().width(),
                   cursor.custom_bitmap().height()),
         cursor.custom_hotspot());
  } else {
    Emit("cursor-changed", CursorTypeToString(cursor.type()));
  }
}

void WebContents::AttachToIframe(content::WebContents* embedder_web_contents,
                                 int embedder_frame_id) {
  attached_ = true;
  if (guest_delegate_)
    guest_delegate_->AttachToIframe(embedder_web_contents, embedder_frame_id);
}

bool WebContents::IsOffScreen() const {
  return type_ == Type::kOffScreen;
}

void WebContents::OnPaint(const gfx::Rect& dirty_rect, const SkBitmap& bitmap) {
  Emit("paint", dirty_rect, gfx::Image::CreateFrom1xBitmap(bitmap));
}

void WebContents::StartPainting() {
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetPainting(true);
}

void WebContents::StopPainting() {
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetPainting(false);
}

bool WebContents::IsPainting() const {
  auto* osr_wcv = GetOffScreenWebContentsView();
  return osr_wcv && osr_wcv->IsPainting();
}

void WebContents::SetFrameRate(int frame_rate) {
  auto* osr_wcv = GetOffScreenWebContentsView();
  if (osr_wcv)
    osr_wcv->SetFrameRate(frame_rate);
}

int WebContents::GetFrameRate() const {
  auto* osr_wcv = GetOffScreenWebContentsView();
  return osr_wcv ? osr_wcv->GetFrameRate() : 0;
}

void WebContents::Invalidate() {
  if (IsOffScreen()) {
    auto* osr_rwhv = GetOffScreenRenderWidgetHostView();
    if (osr_rwhv)
      osr_rwhv->Invalidate();
  } else {
    auto* const window = owner_window();
    if (window)
      window->Invalidate();
  }
}

gfx::Size WebContents::GetSizeForNewRenderView(content::WebContents* wc) {
  if (IsOffScreen() && wc == web_contents()) {
    auto* relay = NativeWindowRelay::FromWebContents(web_contents());
    if (relay) {
      auto* owner_window = relay->GetNativeWindow();
      return owner_window ? owner_window->GetSize() : gfx::Size();
    }
  }

  return gfx::Size();
}

void WebContents::SetZoomLevel(double level) {
  zoom_controller_->SetZoomLevel(level);
}

double WebContents::GetZoomLevel() const {
  return zoom_controller_->GetZoomLevel();
}

void WebContents::SetZoomFactor(gin_helper::ErrorThrower thrower,
                                double factor) {
  if (factor < std::numeric_limits<double>::epsilon()) {
    thrower.ThrowError("'zoomFactor' must be a double greater than 0.0");
    return;
  }

  auto level = blink::PageZoomFactorToZoomLevel(factor);
  SetZoomLevel(level);
}

double WebContents::GetZoomFactor() const {
  auto level = GetZoomLevel();
  return blink::PageZoomLevelToZoomFactor(level);
}

void WebContents::SetTemporaryZoomLevel(double level) {
  zoom_controller_->SetTemporaryZoomLevel(level);
}

void WebContents::DoGetZoomLevel(
    electron::mojom::ElectronWebContentsUtility::DoGetZoomLevelCallback
        callback) {
  std::move(callback).Run(GetZoomLevel());
}

std::vector<base::FilePath> WebContents::GetPreloadPaths() const {
  auto result = SessionPreferences::GetValidPreloads(GetBrowserContext());

  if (auto* web_preferences = WebContentsPreferences::From(web_contents())) {
    base::FilePath preload;
    if (web_preferences->GetPreloadPath(&preload)) {
      result.emplace_back(preload);
    }
  }

  return result;
}

v8::Local<v8::Value> WebContents::GetLastWebPreferences(
    v8::Isolate* isolate) const {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  if (!web_preferences)
    return v8::Null(isolate);
  return gin::ConvertToV8(isolate, *web_preferences->last_preference());
}

v8::Local<v8::Value> WebContents::GetOwnerBrowserWindow(
    v8::Isolate* isolate) const {
  if (owner_window())
    return BrowserWindow::From(isolate, owner_window());
  else
    return v8::Null(isolate);
}

v8::Local<v8::Value> WebContents::Session(v8::Isolate* isolate) {
  return v8::Local<v8::Value>::New(isolate, session_);
}

content::WebContents* WebContents::HostWebContents() const {
  if (!embedder_)
    return nullptr;
  return embedder_->web_contents();
}

void WebContents::SetEmbedder(const WebContents* embedder) {
  if (embedder) {
    NativeWindow* owner_window = nullptr;
    auto* relay = NativeWindowRelay::FromWebContents(embedder->web_contents());
    if (relay) {
      owner_window = relay->GetNativeWindow();
    }
    if (owner_window)
      SetOwnerWindow(owner_window);

    content::RenderWidgetHostView* rwhv =
        web_contents()->GetRenderWidgetHostView();
    if (rwhv) {
      rwhv->Hide();
      rwhv->Show();
    }
  }
}

void WebContents::SetDevToolsWebContents(const WebContents* devtools) {
  if (inspectable_web_contents_)
    inspectable_web_contents_->SetDevToolsWebContents(devtools->web_contents());
}

v8::Local<v8::Value> WebContents::GetNativeView(v8::Isolate* isolate) const {
  gfx::NativeView ptr = web_contents()->GetNativeView();
  auto buffer = node::Buffer::Copy(isolate, reinterpret_cast<char*>(&ptr),
                                   sizeof(gfx::NativeView));
  if (buffer.IsEmpty())
    return v8::Null(isolate);
  else
    return buffer.ToLocalChecked();
}

v8::Local<v8::Value> WebContents::DevToolsWebContents(v8::Isolate* isolate) {
  if (devtools_web_contents_.IsEmpty())
    return v8::Null(isolate);
  else
    return v8::Local<v8::Value>::New(isolate, devtools_web_contents_);
}

v8::Local<v8::Value> WebContents::Debugger(v8::Isolate* isolate) {
  if (debugger_.IsEmpty()) {
    auto handle = electron::api::Debugger::Create(isolate, web_contents());
    debugger_.Reset(isolate, handle.ToV8());
  }
  return v8::Local<v8::Value>::New(isolate, debugger_);
}

content::RenderFrameHost* WebContents::MainFrame() {
  return web_contents()->GetPrimaryMainFrame();
}

content::RenderFrameHost* WebContents::Opener() {
  return web_contents()->GetOpener();
}

void WebContents::NotifyUserActivation() {
  content::RenderFrameHost* frame = web_contents()->GetPrimaryMainFrame();
  if (frame)
    frame->NotifyUserActivation(
        blink::mojom::UserActivationNotificationType::kInteraction);
}

void WebContents::SetImageAnimationPolicy(const std::string& new_policy) {
  auto* web_preferences = WebContentsPreferences::From(web_contents());
  web_preferences->SetImageAnimationPolicy(new_policy);
  web_contents()->OnWebPreferencesChanged();
}

void WebContents::SetBackgroundColor(std::optional<SkColor> maybe_color) {
  SkColor color = maybe_color.value_or((is_guest() && guest_transparent_) ||
                                               type_ == Type::kBrowserView
                                           ? SK_ColorTRANSPARENT
                                           : SK_ColorWHITE);
  web_contents()->SetPageBaseBackgroundColor(color);

  content::RenderFrameHost* rfh = web_contents()->GetPrimaryMainFrame();
  if (!rfh)
    return;
  content::RenderWidgetHostView* rwhv = rfh->GetView();
  if (rwhv) {
    rwhv->SetBackgroundColor(color);
    static_cast<content::RenderWidgetHostViewBase*>(rwhv)
        ->SetContentBackgroundColor(color);
  }
}

void WebContents::OnInputEvent(const blink::WebInputEvent& event) {
  Emit("input-event", event);
}

void WebContents::RunJavaScriptDialog(content::WebContents* web_contents,
                                      content::RenderFrameHost* rfh,
                                      content::JavaScriptDialogType dialog_type,
                                      const std::u16string& message_text,
                                      const std::u16string& default_prompt_text,
                                      DialogClosedCallback callback,
                                      bool* did_suppress_message) {
  CHECK_EQ(web_contents, this->web_contents());

  auto* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  auto info = gin::DataObjectBuilder(isolate)
                  .Set("frame", rfh)
                  .Set("dialogType", dialog_type)
                  .Set("messageText", message_text)
                  .Set("defaultPromptText", default_prompt_text)
                  .Build();

  EmitWithoutEvent("-run-dialog", info, std::move(callback));
}

void WebContents::RunBeforeUnloadDialog(content::WebContents* web_contents,
                                        content::RenderFrameHost* rfh,
                                        bool is_reload,
                                        DialogClosedCallback callback) {
  // TODO: asyncify?
  bool default_prevented = Emit("will-prevent-unload");
  std::move(callback).Run(default_prevented, std::u16string());
}

void WebContents::CancelDialogs(content::WebContents* web_contents,
                                bool reset_state) {
  auto* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  EmitWithoutEvent(
      "-cancel-dialogs",
      gin::DataObjectBuilder(isolate).Set("resetState", reset_state).Build());
}

v8::Local<v8::Promise> WebContents::GetProcessMemoryInfo(v8::Isolate* isolate) {
  gin_helper::Promise<gin_helper::Dictionary> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  auto* frame_host = web_contents()->GetPrimaryMainFrame();
  if (!frame_host) {
    promise.RejectWithErrorMessage("Failed to create memory dump");
    return handle;
  }

  auto pid = frame_host->GetProcess()->GetProcess().Pid();
  v8::Global<v8::Context> context(isolate, isolate->GetCurrentContext());
  memory_instrumentation::MemoryInstrumentation::GetInstance()
      ->RequestGlobalDumpForPid(
          pid, std::vector<std::string>(),
          base::BindOnce(&ElectronBindings::DidReceiveMemoryDump,
                         std::move(context), std::move(promise), pid));
  return handle;
}

v8::Local<v8::Promise> WebContents::TakeHeapSnapshot(
    v8::Isolate* isolate,
    const base::FilePath& file_path) {
  gin_helper::Promise<void> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  ScopedAllowBlockingForElectron allow_blocking;
  uint32_t flags = base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE;
  // The snapshot file is passed to an untrusted process.
  flags = base::File::AddFlagsForPassingToUntrustedProcess(flags);
  base::File file(file_path, flags);
  if (!file.IsValid()) {
    promise.RejectWithErrorMessage(
        "Failed to take heap snapshot with invalid file path " +
#if BUILDFLAG(IS_WIN)
        base::WideToUTF8(file_path.value()));
#else
        file_path.value());
#endif
    return handle;
  }

  auto* frame_host = web_contents()->GetPrimaryMainFrame();
  if (!frame_host) {
    promise.RejectWithErrorMessage(
        "Failed to take heap snapshot with invalid webContents main frame");
    return handle;
  }

  if (!frame_host->IsRenderFrameLive()) {
    promise.RejectWithErrorMessage(
        "Failed to take heap snapshot with nonexistent render frame");
    return handle;
  }

  // This dance with `base::Owned` is to ensure that the interface stays alive
  // until the callback is called. Otherwise it would be closed at the end of
  // this function.
  auto electron_renderer =
      std::make_unique<mojo::Remote<mojom::ElectronRenderer>>();
  frame_host->GetRemoteInterfaces()->GetInterface(
      electron_renderer->BindNewPipeAndPassReceiver());
  auto* raw_ptr = electron_renderer.get();
  (*raw_ptr)->TakeHeapSnapshot(
      mojo::WrapPlatformFile(base::ScopedPlatformFile(file.TakePlatformFile())),
      base::BindOnce(
          [](mojo::Remote<mojom::ElectronRenderer>* ep,
             gin_helper::Promise<void> promise, bool success) {
            if (success) {
              promise.Resolve();
            } else {
              promise.RejectWithErrorMessage("Failed to take heap snapshot");
            }
          },
          base::Owned(std::move(electron_renderer)), std::move(promise)));
  return handle;
}

void WebContents::UpdatePreferredSize(content::WebContents* web_contents,
                                      const gfx::Size& pref_size) {
  Emit("preferred-size-changed", pref_size);
}

bool WebContents::CanOverscrollContent() {
  return false;
}

std::unique_ptr<content::EyeDropper> WebContents::OpenEyeDropper(
    content::RenderFrameHost* frame,
    content::EyeDropperListener* listener) {
  return ShowEyeDropper(frame, listener);
}

void WebContents::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    scoped_refptr<content::FileSelectListener> listener,
    const blink::mojom::FileChooserParams& params) {
  FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener),
                                   params);
}

void WebContents::EnumerateDirectory(
    content::WebContents* web_contents,
    scoped_refptr<content::FileSelectListener> listener,
    const base::FilePath& path) {
  FileSelectHelper::EnumerateDirectory(web_contents, std::move(listener), path);
}

bool WebContents::IsFullscreenForTabOrPending(
    const content::WebContents* source) {
  if (!owner_window())
    return is_html_fullscreen();

  bool in_transition = owner_window()->fullscreen_transition_state() !=
                       NativeWindow::FullScreenTransitionState::kNone;
  bool is_html_transition = owner_window()->fullscreen_transition_type() ==
                            NativeWindow::FullScreenTransitionType::kHTML;

  return is_html_fullscreen() || (in_transition && is_html_transition);
}

content::FullscreenState WebContents::GetFullscreenState(
    const content::WebContents* source) const {
  // `const_cast` here because EAM does not have const getters
  return const_cast<ExclusiveAccessManager*>(&exclusive_access_manager_)
      ->fullscreen_controller()
      ->GetFullscreenState(source);
}

bool WebContents::TakeFocus(content::WebContents* source, bool reverse) {
  if (source && source->GetOutermostWebContents() == source) {
    // If this is the outermost web contents and the user has tabbed or
    // shift + tabbed through all the elements, reset the focus back to
    // the first or last element so that it doesn't stay in the body.
    source->FocusThroughTabTraversal(reverse);
    return true;
  }

  return false;
}

content::PictureInPictureResult WebContents::EnterPictureInPicture(
    content::WebContents* web_contents) {
  return PictureInPictureWindowManager::GetInstance()
      ->EnterVideoPictureInPicture(web_contents);
}

void WebContents::ExitPictureInPicture() {
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
}

void WebContents::DevToolsSaveToFile(const std::string& url,
                                     const std::string& content,
                                     bool save_as,
                                     bool is_base64) {
  base::FilePath path;
  auto it = saved_files_.find(url);
  if (it != saved_files_.end() && !save_as) {
    path = it->second;
  } else {
    file_dialog::DialogSettings settings;
    settings.parent_window = owner_window();
    settings.force_detached = offscreen_;
    settings.title = url;
    settings.default_path = base::FilePath::FromUTF8Unsafe(url);
    if (!file_dialog::ShowSaveDialogSync(settings, &path)) {
      inspectable_web_contents_->CallClientFunction(
          "DevToolsAPI", "canceledSaveURL", base::Value(url));
      return;
    }
  }

  saved_files_[url] = path;
  // Notify DevTools.
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "savedURL", base::Value(url),
      base::Value(path.AsUTF8Unsafe()));
  file_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&WriteToFile, path, content, is_base64));
}

void WebContents::DevToolsAppendToFile(const std::string& url,
                                       const std::string& content) {
  auto it = saved_files_.find(url);
  if (it == saved_files_.end())
    return;

  // Notify DevTools.
  inspectable_web_contents_->CallClientFunction("DevToolsAPI", "appendedToURL",
                                                base::Value(url));
  file_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&AppendToFile, it->second, content));
}

void WebContents::DevToolsRequestFileSystems() {
  auto file_system_paths = GetAddedFileSystemPaths(GetDevToolsWebContents());
  if (file_system_paths.empty()) {
    inspectable_web_contents_->CallClientFunction(
        "DevToolsAPI", "fileSystemsLoaded", base::Value(base::Value::List()));
    return;
  }

  std::vector<FileSystem> file_systems;
  for (const auto& file_system_path : file_system_paths) {
    base::FilePath path =
        base::FilePath::FromUTF8Unsafe(file_system_path.first);
    std::string file_system_id =
        RegisterFileSystem(GetDevToolsWebContents(), path);
    FileSystem file_system =
        CreateFileSystemStruct(GetDevToolsWebContents(), file_system_id,
                               file_system_path.first, file_system_path.second);
    file_systems.push_back(file_system);
  }

  base::Value::List file_system_value;
  for (const auto& file_system : file_systems)
    file_system_value.Append(CreateFileSystemValue(file_system));
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "fileSystemsLoaded",
      base::Value(std::move(file_system_value)));
}

void WebContents::DevToolsAddFileSystem(
    const std::string& type,
    const base::FilePath& file_system_path) {
  base::FilePath path = file_system_path;
  if (path.empty()) {
    std::vector<base::FilePath> paths;
    file_dialog::DialogSettings settings;
    settings.parent_window = owner_window();
    settings.force_detached = offscreen_;
    settings.properties = file_dialog::OPEN_DIALOG_OPEN_DIRECTORY;
    if (!file_dialog::ShowOpenDialogSync(settings, &paths))
      return;

    path = paths[0];
  }

  std::string file_system_id =
      RegisterFileSystem(GetDevToolsWebContents(), path);
  if (IsDevToolsFileSystemAdded(GetDevToolsWebContents(), path.AsUTF8Unsafe()))
    return;

  FileSystem file_system = CreateFileSystemStruct(
      GetDevToolsWebContents(), file_system_id, path.AsUTF8Unsafe(), type);
  base::Value::Dict file_system_value = CreateFileSystemValue(file_system);

  auto* pref_service = GetPrefService(GetDevToolsWebContents());
  ScopedDictPrefUpdate update(pref_service, prefs::kDevToolsFileSystemPaths);
  update->Set(path.AsUTF8Unsafe(), type);
  std::string error = "";  // No error
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "fileSystemAdded", base::Value(error),
      base::Value(std::move(file_system_value)));
}

void WebContents::DevToolsRemoveFileSystem(
    const base::FilePath& file_system_path) {
  if (!inspectable_web_contents_)
    return;

  std::string path = file_system_path.AsUTF8Unsafe();
  storage::IsolatedContext::GetInstance()->RevokeFileSystemByPath(
      file_system_path);

  auto* pref_service = GetPrefService(GetDevToolsWebContents());
  ScopedDictPrefUpdate update(pref_service, prefs::kDevToolsFileSystemPaths);
  update->Remove(path);

  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "fileSystemRemoved", base::Value(path));
}

void WebContents::DevToolsIndexPath(
    int request_id,
    const std::string& file_system_path,
    const std::string& excluded_folders_message) {
  if (!IsDevToolsFileSystemAdded(GetDevToolsWebContents(), file_system_path)) {
    OnDevToolsIndexingDone(request_id, file_system_path);
    return;
  }
  if (devtools_indexing_jobs_.count(request_id) != 0)
    return;
  std::vector<std::string> excluded_folders;
  std::optional<base::Value> parsed_excluded_folders =
      base::JSONReader::Read(excluded_folders_message);
  if (parsed_excluded_folders && parsed_excluded_folders->is_list()) {
    for (const base::Value& folder_path : parsed_excluded_folders->GetList()) {
      if (folder_path.is_string())
        excluded_folders.push_back(folder_path.GetString());
    }
  }
  devtools_indexing_jobs_[request_id] =
      scoped_refptr<DevToolsFileSystemIndexer::FileSystemIndexingJob>(
          devtools_file_system_indexer_->IndexPath(
              file_system_path, excluded_folders,
              base::BindRepeating(
                  &WebContents::OnDevToolsIndexingWorkCalculated,
                  weak_factory_.GetWeakPtr(), request_id, file_system_path),
              base::BindRepeating(&WebContents::OnDevToolsIndexingWorked,
                                  weak_factory_.GetWeakPtr(), request_id,
                                  file_system_path),
              base::BindRepeating(&WebContents::OnDevToolsIndexingDone,
                                  weak_factory_.GetWeakPtr(), request_id,
                                  file_system_path)));
}

void WebContents::DevToolsStopIndexing(int request_id) {
  auto it = devtools_indexing_jobs_.find(request_id);
  if (it == devtools_indexing_jobs_.end())
    return;
  it->second->Stop();
  devtools_indexing_jobs_.erase(it);
}

void WebContents::DevToolsOpenInNewTab(const std::string& url) {
  Emit("devtools-open-url", url);
}

void WebContents::DevToolsOpenSearchResultsInNewTab(const std::string& query) {
  Emit("devtools-search-query", query);
}

void WebContents::DevToolsSearchInPath(int request_id,
                                       const std::string& file_system_path,
                                       const std::string& query) {
  if (!IsDevToolsFileSystemAdded(GetDevToolsWebContents(), file_system_path)) {
    OnDevToolsSearchCompleted(request_id, file_system_path,
                              std::vector<std::string>());
    return;
  }
  devtools_file_system_indexer_->SearchInPath(
      file_system_path, query,
      base::BindRepeating(&WebContents::OnDevToolsSearchCompleted,
                          weak_factory_.GetWeakPtr(), request_id,
                          file_system_path));
}

void WebContents::DevToolsSetEyeDropperActive(bool active) {
  auto* web_contents = GetWebContents();
  if (!web_contents)
    return;

  if (active) {
    eye_dropper_ = std::make_unique<DevToolsEyeDropper>(
        web_contents, base::BindRepeating(&WebContents::ColorPickedInEyeDropper,
                                          base::Unretained(this)));
  } else {
    eye_dropper_.reset();
  }
}

void WebContents::ColorPickedInEyeDropper(int r, int g, int b, int a) {
  base::Value::Dict color;
  color.Set("r", r);
  color.Set("g", g);
  color.Set("b", b);
  color.Set("a", a);
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "eyeDropperPickedColor", base::Value(std::move(color)));
}

#if defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_MAC)
ui::ImageModel WebContents::GetDevToolsWindowIcon() {
  return owner_window() ? owner_window()->GetWindowAppIcon() : ui::ImageModel{};
}
#endif

#if BUILDFLAG(IS_LINUX)
void WebContents::GetDevToolsWindowWMClass(std::string* name,
                                           std::string* class_name) {
  *class_name = Browser::Get()->GetName();
  *name = base::ToLowerASCII(*class_name);
}
#endif

void WebContents::OnDevToolsIndexingWorkCalculated(
    int request_id,
    const std::string& file_system_path,
    int total_work) {
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "indexingTotalWorkCalculated", base::Value(request_id),
      base::Value(file_system_path), base::Value(total_work));
}

void WebContents::OnDevToolsIndexingWorked(int request_id,
                                           const std::string& file_system_path,
                                           int worked) {
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "indexingWorked", base::Value(request_id),
      base::Value(file_system_path), base::Value(worked));
}

void WebContents::OnDevToolsIndexingDone(int request_id,
                                         const std::string& file_system_path) {
  devtools_indexing_jobs_.erase(request_id);
  inspectable_web_contents_->CallClientFunction("DevToolsAPI", "indexingDone",
                                                base::Value(request_id),
                                                base::Value(file_system_path));
}

void WebContents::OnDevToolsSearchCompleted(
    int request_id,
    const std::string& file_system_path,
    const std::vector<std::string>& file_paths) {
  base::Value::List file_paths_value;
  for (const auto& file_path : file_paths)
    file_paths_value.Append(file_path);
  inspectable_web_contents_->CallClientFunction(
      "DevToolsAPI", "searchCompleted", base::Value(request_id),
      base::Value(file_system_path), base::Value(std::move(file_paths_value)));
}

void WebContents::SetHtmlApiFullscreen(bool enter_fullscreen) {
  // Window is already in fullscreen mode, save the state.
  if (enter_fullscreen && owner_window()->IsFullscreen()) {
    native_fullscreen_ = true;
    UpdateHtmlApiFullscreen(true);
    return;
  }

  // Exit html fullscreen state but not window's fullscreen mode.
  if (!enter_fullscreen && native_fullscreen_) {
    UpdateHtmlApiFullscreen(false);
    return;
  }

  // Set fullscreen on window if allowed.
  auto* web_preferences = WebContentsPreferences::From(GetWebContents());
  bool html_fullscreenable =
      web_preferences
          ? !web_preferences->ShouldDisableHtmlFullscreenWindowResize()
          : true;

  if (html_fullscreenable)
    owner_window_->SetFullScreen(enter_fullscreen);

  UpdateHtmlApiFullscreen(enter_fullscreen);
  native_fullscreen_ = false;
}

void WebContents::UpdateHtmlApiFullscreen(bool fullscreen) {
  if (fullscreen == is_html_fullscreen())
    return;

  html_fullscreen_ = fullscreen;

  // Notify renderer of the html fullscreen change.
  web_contents()
      ->GetRenderViewHost()
      ->GetWidget()
      ->SynchronizeVisualProperties();

  // The embedder WebContents is separated from the frame tree of webview, so
  // we must manually sync their fullscreen states.
  if (embedder_)
    embedder_->SetHtmlApiFullscreen(fullscreen);

  if (fullscreen) {
    Emit("enter-html-full-screen");
    owner_window_->NotifyWindowEnterHtmlFullScreen();
  } else {
    Emit("leave-html-full-screen");
    owner_window_->NotifyWindowLeaveHtmlFullScreen();
  }

  // Make sure all child webviews quit html fullscreen.
  if (!fullscreen && !is_guest()) {
    auto* manager = WebViewManager::GetWebViewManager(web_contents());
    manager->ForEachGuest(web_contents(), [&](content::WebContents* guest) {
      WebContents* api_web_contents = WebContents::From(guest);
      api_web_contents->SetHtmlApiFullscreen(false);
      return false;
    });
  }
}

// static
void WebContents::FillObjectTemplate(v8::Isolate* isolate,
                                     v8::Local<v8::ObjectTemplate> templ) {
  gin::InvokerOptions options;
  options.holder_is_first_argument = true;
  options.holder_type = GetClassName();
  templ->Set(
      gin::StringToSymbol(isolate, "isDestroyed"),
      gin::CreateFunctionTemplate(
          isolate, base::BindRepeating(&gin_helper::Destroyable::IsDestroyed),
          options));
  // We use gin_helper::ObjectTemplateBuilder instead of
  // gin::ObjectTemplateBuilder here to handle the fact that WebContents is
  // destroyable.
  gin_helper::ObjectTemplateBuilder(isolate, templ)
      .SetMethod("destroy", &WebContents::Destroy)
      .SetMethod("close", &WebContents::Close)
      .SetMethod("getBackgroundThrottling",
                 &WebContents::GetBackgroundThrottling)
      .SetMethod("setBackgroundThrottling",
                 &WebContents::SetBackgroundThrottling)
      .SetMethod("getProcessId", &WebContents::GetProcessID)
      .SetMethod("getOSProcessId", &WebContents::GetOSProcessID)
      .SetMethod("equal", &WebContents::Equal)
      .SetMethod("_loadURL", &WebContents::LoadURL)
      .SetMethod("reload", &WebContents::Reload)
      .SetMethod("reloadIgnoringCache", &WebContents::ReloadIgnoringCache)
      .SetMethod("downloadURL", &WebContents::DownloadURL)
      .SetMethod("getURL", &WebContents::GetURL)
      .SetMethod("getTitle", &WebContents::GetTitle)
      .SetMethod("isLoading", &WebContents::IsLoading)
      .SetMethod("isLoadingMainFrame", &WebContents::IsLoadingMainFrame)
      .SetMethod("isWaitingForResponse", &WebContents::IsWaitingForResponse)
      .SetMethod("stop", &WebContents::Stop)
      .SetMethod("_canGoBack", &WebContents::CanGoBack)
      .SetMethod("_goBack", &WebContents::GoBack)
      .SetMethod("_canGoForward", &WebContents::CanGoForward)
      .SetMethod("_goForward", &WebContents::GoForward)
      .SetMethod("_canGoToOffset", &WebContents::CanGoToOffset)
      .SetMethod("_goToOffset", &WebContents::GoToOffset)
      .SetMethod("canGoToIndex", &WebContents::CanGoToIndex)
      .SetMethod("_goToIndex", &WebContents::GoToIndex)
      .SetMethod("_getActiveIndex", &WebContents::GetActiveIndex)
      .SetMethod("_getNavigationEntryAtIndex",
                 &WebContents::GetNavigationEntryAtIndex)
      .SetMethod("_historyLength", &WebContents::GetHistoryLength)
      .SetMethod("_clearHistory", &WebContents::ClearHistory)
      .SetMethod("isCrashed", &WebContents::IsCrashed)
      .SetMethod("forcefullyCrashRenderer",
                 &WebContents::ForcefullyCrashRenderer)
      .SetMethod("setUserAgent", &WebContents::SetUserAgent)
      .SetMethod("getUserAgent", &WebContents::GetUserAgent)
      .SetMethod("savePage", &WebContents::SavePage)
      .SetMethod("openDevTools", &WebContents::OpenDevTools)
      .SetMethod("closeDevTools", &WebContents::CloseDevTools)
      .SetMethod("isDevToolsOpened", &WebContents::IsDevToolsOpened)
      .SetMethod("isDevToolsFocused", &WebContents::IsDevToolsFocused)
      .SetMethod("getDevToolsTitle", &WebContents::GetDevToolsTitle)
      .SetMethod("setDevToolsTitle", &WebContents::SetDevToolsTitle)
      .SetMethod("enableDeviceEmulation", &WebContents::EnableDeviceEmulation)
      .SetMethod("disableDeviceEmulation", &WebContents::DisableDeviceEmulation)
      .SetMethod("toggleDevTools", &WebContents::ToggleDevTools)
      .SetMethod("inspectElement", &WebContents::InspectElement)
      .SetMethod("setIgnoreMenuShortcuts", &WebContents::SetIgnoreMenuShortcuts)
      .SetMethod("setAudioMuted", &WebContents::SetAudioMuted)
      .SetMethod("isAudioMuted", &WebContents::IsAudioMuted)
      .SetMethod("isCurrentlyAudible", &WebContents::IsCurrentlyAudible)
      .SetMethod("undo", &WebContents::Undo)
      .SetMethod("redo", &WebContents::Redo)
      .SetMethod("cut", &WebContents::Cut)
      .SetMethod("copy", &WebContents::Copy)
      .SetMethod("centerSelection", &WebContents::CenterSelection)
      .SetMethod("paste", &WebContents::Paste)
      .SetMethod("pasteAndMatchStyle", &WebContents::PasteAndMatchStyle)
      .SetMethod("delete", &WebContents::Delete)
      .SetMethod("selectAll", &WebContents::SelectAll)
      .SetMethod("unselect", &WebContents::Unselect)
      .SetMethod("scrollToTop", &WebContents::ScrollToTopOfDocument)
      .SetMethod("scrollToBottom", &WebContents::ScrollToBottomOfDocument)
      .SetMethod("adjustSelection",
                 &WebContents::AdjustSelectionByCharacterOffset)
      .SetMethod("replace", &WebContents::Replace)
      .SetMethod("replaceMisspelling", &WebContents::ReplaceMisspelling)
      .SetMethod("findInPage", &WebContents::FindInPage)
      .SetMethod("stopFindInPage", &WebContents::StopFindInPage)
      .SetMethod("focus", &WebContents::Focus)
      .SetMethod("isFocused", &WebContents::IsFocused)
      .SetMethod("sendInputEvent", &WebContents::SendInputEvent)
      .SetMethod("beginFrameSubscription", &WebContents::BeginFrameSubscription)
      .SetMethod("endFrameSubscription", &WebContents::EndFrameSubscription)
      .SetMethod("startDrag", &WebContents::StartDrag)
      .SetMethod("attachToIframe", &WebContents::AttachToIframe)
      .SetMethod("detachFromOuterFrame", &WebContents::DetachFromOuterFrame)
      .SetMethod("isOffscreen", &WebContents::IsOffScreen)
      .SetMethod("startPainting", &WebContents::StartPainting)
      .SetMethod("stopPainting", &WebContents::StopPainting)
      .SetMethod("isPainting", &WebContents::IsPainting)
      .SetMethod("setFrameRate", &WebContents::SetFrameRate)
      .SetMethod("getFrameRate", &WebContents::GetFrameRate)
      .SetMethod("invalidate", &WebContents::Invalidate)
      .SetMethod("setZoomLevel", &WebContents::SetZoomLevel)
      .SetMethod("getZoomLevel", &WebContents::GetZoomLevel)
      .SetMethod("setZoomFactor", &WebContents::SetZoomFactor)
      .SetMethod("getZoomFactor", &WebContents::GetZoomFactor)
      .SetMethod("getType", &WebContents::type)
      .SetMethod("_getPreloadPaths", &WebContents::GetPreloadPaths)
      .SetMethod("getLastWebPreferences", &WebContents::GetLastWebPreferences)
      .SetMethod("getOwnerBrowserWindow", &WebContents::GetOwnerBrowserWindow)
      .SetMethod("inspectServiceWorker", &WebContents::InspectServiceWorker)
      .SetMethod("inspectSharedWorker", &WebContents::InspectSharedWorker)
      .SetMethod("inspectSharedWorkerById",
                 &WebContents::InspectSharedWorkerById)
      .SetMethod("getAllSharedWorkers", &WebContents::GetAllSharedWorkers)
#if BUILDFLAG(ENABLE_PRINTING)
      .SetMethod("_print", &WebContents::Print)
      .SetMethod("_printToPDF", &WebContents::PrintToPDF)
#endif
      .SetMethod("_setNextChildWebPreferences",
                 &WebContents::SetNextChildWebPreferences)
      .SetMethod("addWorkSpace", &WebContents::AddWorkSpace)
      .SetMethod("removeWorkSpace", &WebContents::RemoveWorkSpace)
      .SetMethod("showDefinitionForSelection",
                 &WebContents::ShowDefinitionForSelection)
      .SetMethod("copyImageAt", &WebContents::CopyImageAt)
      .SetMethod("capturePage", &WebContents::CapturePage)
      .SetMethod("setEmbedder", &WebContents::SetEmbedder)
      .SetMethod("setDevToolsWebContents", &WebContents::SetDevToolsWebContents)
      .SetMethod("getNativeView", &WebContents::GetNativeView)
      .SetMethod("isBeingCaptured", &WebContents::IsBeingCaptured)
      .SetMethod("setWebRTCIPHandlingPolicy",
                 &WebContents::SetWebRTCIPHandlingPolicy)
      .SetMethod("setWebRTCUDPPortRange", &WebContents::SetWebRTCUDPPortRange)
      .SetMethod("getMediaSourceId", &WebContents::GetMediaSourceID)
      .SetMethod("getWebRTCIPHandlingPolicy",
                 &WebContents::GetWebRTCIPHandlingPolicy)
      .SetMethod("getWebRTCUDPPortRange", &WebContents::GetWebRTCUDPPortRange)
      .SetMethod("takeHeapSnapshot", &WebContents::TakeHeapSnapshot)
      .SetMethod("setImageAnimationPolicy",
                 &WebContents::SetImageAnimationPolicy)
      .SetMethod("_getProcessMemoryInfo", &WebContents::GetProcessMemoryInfo)
      .SetProperty("id", &WebContents::ID)
      .SetProperty("session", &WebContents::Session)
      .SetProperty("hostWebContents", &WebContents::HostWebContents)
      .SetProperty("devToolsWebContents", &WebContents::DevToolsWebContents)
      .SetProperty("debugger", &WebContents::Debugger)
      .SetProperty("mainFrame", &WebContents::MainFrame)
      .SetProperty("opener", &WebContents::Opener)
      .SetMethod("_setOwnerWindow", &WebContents::SetOwnerBaseWindow)
      .Build();
}

const char* WebContents::GetTypeName() {
  return GetClassName();
}

ElectronBrowserContext* WebContents::GetBrowserContext() const {
  return static_cast<ElectronBrowserContext*>(
      web_contents()->GetBrowserContext());
}

// static
gin::Handle<WebContents> WebContents::New(
    v8::Isolate* isolate,
    const gin_helper::Dictionary& options) {
  gin::Handle<WebContents> handle =
      gin::CreateHandle(isolate, new WebContents(isolate, options));
  v8::TryCatch try_catch(isolate);
  gin_helper::CallMethod(isolate, handle.get(), "_init");
  if (try_catch.HasCaught()) {
    node::errors::TriggerUncaughtException(isolate, try_catch);
  }
  return handle;
}

// static
gin::Handle<WebContents> WebContents::CreateAndTake(
    v8::Isolate* isolate,
    std::unique_ptr<content::WebContents> web_contents,
    Type type) {
  gin::Handle<WebContents> handle = gin::CreateHandle(
      isolate, new WebContents(isolate, std::move(web_contents), type));
  v8::TryCatch try_catch(isolate);
  gin_helper::CallMethod(isolate, handle.get(), "_init");
  if (try_catch.HasCaught()) {
    node::errors::TriggerUncaughtException(isolate, try_catch);
  }
  return handle;
}

// static
WebContents* WebContents::From(content::WebContents* web_contents) {
  if (!web_contents)
    return nullptr;
  auto* data = static_cast<UserDataLink*>(
      web_contents->GetUserData(kElectronApiWebContentsKey));
  return data ? data->web_contents.get() : nullptr;
}

// static
gin::Handle<WebContents> WebContents::FromOrCreate(
    v8::Isolate* isolate,
    content::WebContents* web_contents) {
  WebContents* api_web_contents = From(web_contents);
  if (!api_web_contents) {
    api_web_contents = new WebContents(isolate, web_contents);
    v8::TryCatch try_catch(isolate);
    gin_helper::CallMethod(isolate, api_web_contents, "_init");
    if (try_catch.HasCaught()) {
      node::errors::TriggerUncaughtException(isolate, try_catch);
    }
  }
  return gin::CreateHandle(isolate, api_web_contents);
}

// static
gin::Handle<WebContents> WebContents::CreateFromWebPreferences(
    v8::Isolate* isolate,
    const gin_helper::Dictionary& web_preferences) {
  // Check if webPreferences has |webContents| option.
  gin::Handle<WebContents> web_contents;
  if (web_preferences.GetHidden("webContents", &web_contents) &&
      !web_contents.IsEmpty()) {
    // Set webPreferences from options if using an existing webContents.
    // These preferences will be used when the webContent launches new
    // render processes.
    auto* existing_preferences =
        WebContentsPreferences::From(web_contents->web_contents());
    gin_helper::Dictionary web_preferences_dict;
    if (gin::ConvertFromV8(isolate, web_preferences.GetHandle(),
                           &web_preferences_dict)) {
      existing_preferences->SetFromDictionary(web_preferences_dict);
      web_contents->SetBackgroundColor(
          existing_preferences->GetBackgroundColor());
    }
  } else {
    // Create one if not.
    web_contents = WebContents::New(isolate, web_preferences);
  }

  return web_contents;
}

// static
WebContents* WebContents::FromID(int32_t id) {
  return GetAllWebContents().Lookup(id);
}

// static
std::list<WebContents*> WebContents::GetWebContentsList() {
  std::list<WebContents*> list;
  for (auto iter = base::IDMap<WebContents*>::iterator(&GetAllWebContents());
       !iter.IsAtEnd(); iter.Advance()) {
    list.push_back(iter.GetCurrentValue());
  }
  return list;
}

// static
gin::WrapperInfo WebContents::kWrapperInfo = {gin::kEmbedderNativeGin};

}  // namespace electron::api

namespace {

using electron::api::GetAllWebContents;
using electron::api::WebContents;
using electron::api::WebFrameMain;

gin::Handle<WebContents> WebContentsFromID(v8::Isolate* isolate, int32_t id) {
  WebContents* contents = WebContents::FromID(id);
  return contents ? gin::CreateHandle(isolate, contents)
                  : gin::Handle<WebContents>();
}

gin::Handle<WebContents> WebContentsFromFrame(v8::Isolate* isolate,
                                              WebFrameMain* web_frame) {
  content::RenderFrameHost* rfh = web_frame->render_frame_host();
  content::WebContents* source = content::WebContents::FromRenderFrameHost(rfh);
  WebContents* contents = WebContents::From(source);
  return contents ? gin::CreateHandle(isolate, contents)
                  : gin::Handle<WebContents>();
}

gin::Handle<WebContents> WebContentsFromDevToolsTargetID(
    v8::Isolate* isolate,
    std::string target_id) {
  auto agent_host = content::DevToolsAgentHost::GetForId(target_id);
  WebContents* contents =
      agent_host ? WebContents::From(agent_host->GetWebContents()) : nullptr;
  return contents ? gin::CreateHandle(isolate, contents)
                  : gin::Handle<WebContents>();
}

std::vector<gin::Handle<WebContents>> GetAllWebContentsAsV8(
    v8::Isolate* isolate) {
  std::vector<gin::Handle<WebContents>> list;
  for (auto iter = base::IDMap<WebContents*>::iterator(&GetAllWebContents());
       !iter.IsAtEnd(); iter.Advance()) {
    list.push_back(gin::CreateHandle(isolate, iter.GetCurrentValue()));
  }
  return list;
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("WebContents", WebContents::GetConstructor(context));
  dict.SetMethod("fromId", &WebContentsFromID);
  dict.SetMethod("fromFrame", &WebContentsFromFrame);
  dict.SetMethod("fromDevToolsTargetId", &WebContentsFromDevToolsTargetID);
  dict.SetMethod("getAllWebContents", &GetAllWebContentsAsV8);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_web_contents, Initialize)
