// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_desktop_capturer.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/media/webrtc/desktop_capturer_wrapper.h"
#include "chrome/browser/media/webrtc/desktop_media_list.h"
#include "chrome/browser/media/webrtc/thumbnail_capturer_mac.h"
#include "chrome/browser/media/webrtc/window_icon_util.h"
#include "content/public/browser/desktop_capture.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/api/electron_api_native_image.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/event_emitter_caller.h"
#include "shell/common/node_includes.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_options.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"
#include "ui/base/ozone_buildflags.h"

#if BUILDFLAG(IS_WIN)
#include "third_party/webrtc/modules/desktop_capture/win/dxgi_duplicator_controller.h"
#include "third_party/webrtc/modules/desktop_capture/win/screen_capturer_win_directx.h"
#include "ui/display/win/display_info.h"
#elif BUILDFLAG(IS_OZONE_X11)
#include "base/logging.h"
#include "ui/base/x/x11_display_util.h"
#include "ui/base/x/x11_util.h"
#include "ui/display/util/edid_parser.h"  // nogncheck
#include "ui/gfx/x/atom_cache.h"
#include "ui/gfx/x/randr.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "ui/base/cocoa/permissions_utils.h"
#endif

#if BUILDFLAG(IS_LINUX)
// Private function in ui/base/x/x11_display_util.cc
base::flat_map<x11::RandR::Output, int> GetMonitors(
    std::pair<uint32_t, uint32_t> version,
    x11::RandR* randr,
    x11::Window window) {
  base::flat_map<x11::RandR::Output, int> output_to_monitor;
  if (version >= std::pair<uint32_t, uint32_t>{1, 5}) {
    if (auto reply = randr->GetMonitors({window}).Sync()) {
      for (size_t monitor = 0; monitor < reply->monitors.size(); monitor++) {
        for (x11::RandR::Output output : reply->monitors[monitor].outputs)
          output_to_monitor[output] = monitor;
      }
    }
  }
  return output_to_monitor;
}
// Get the EDID data from the |output| and stores to |edid|.
// Private function in ui/base/x/x11_display_util.cc
std::vector<uint8_t> GetEDIDProperty(x11::RandR* randr,
                                     x11::RandR::Output output) {
  constexpr const char kRandrEdidProperty[] = "EDID";
  auto future = randr->GetOutputProperty(x11::RandR::GetOutputPropertyRequest{
      .output = output,
      .property = x11::GetAtom(kRandrEdidProperty),
      .long_length = 128});
  auto response = future.Sync();
  std::vector<uint8_t> edid;
  if (response && response->format == 8 && response->type != x11::Atom::None)
    edid = std::move(response->data);
  return edid;
}

// Find the mapping from monitor name atom to the display identifier
// that the screen API uses. Based on the logic in BuildDisplaysFromXRandRInfo
// in ui/base/x/x11_display_util.cc
base::flat_map<int32_t, uint32_t> MonitorAtomIdToDisplayId() {
  auto* connection = x11::Connection::Get();
  auto& randr = connection->randr();
  auto x_root_window = ui::GetX11RootWindow();

  base::flat_map<int32_t, uint32_t> monitor_atom_to_display;

  auto resources = randr.GetScreenResourcesCurrent({x_root_window}).Sync();
  if (!resources) {
    LOG(ERROR) << "XRandR returned no displays; don't know how to map ids";
    return monitor_atom_to_display;
  }

  const auto output_to_monitor =
      GetMonitors(connection->randr_version(), &randr, x_root_window);
  auto monitors_reply = randr.GetMonitors({x_root_window}).Sync();

  for (size_t i = 0; i < resources->outputs.size(); i++) {
    x11::RandR::Output output_id = resources->outputs[i];
    auto output_info =
        randr.GetOutputInfo({output_id, resources->config_timestamp}).Sync();
    if (!output_info)
      continue;

    if (output_info->connection != x11::RandR::RandRConnection::Connected)
      continue;

    if (output_info->crtc == static_cast<x11::RandR::Crtc>(0))
      continue;

    auto crtc =
        randr.GetCrtcInfo({output_info->crtc, resources->config_timestamp})
            .Sync();
    if (!crtc)
      continue;
    display::EdidParser edid_parser(
        GetEDIDProperty(&randr, static_cast<x11::RandR::Output>(output_id)));
    auto output_32 = static_cast<uint32_t>(output_id);
    int64_t display_id =
        output_32 > 0xff ? 0 : edid_parser.GetIndexBasedDisplayId(output_32);
    // It isn't ideal, but if we can't parse the EDID data, fall back on the
    // display number.
    if (!display_id)
      display_id = i;

    // Find the mapping between output identifier and the monitor name atom
    // Note this isn't the atom string, but the numeric atom identifier,
    // since this is what the WebRTC system uses as the display identifier
    auto output_monitor_iter = output_to_monitor.find(output_id);
    if (output_monitor_iter != output_to_monitor.end()) {
      x11::Atom atom =
          monitors_reply->monitors[output_monitor_iter->second].name;
      monitor_atom_to_display[static_cast<int32_t>(atom)] = display_id;
    }
  }

  return monitor_atom_to_display;
}
#endif

namespace {

std::unique_ptr<ThumbnailCapturer> MakeWindowCapturer() {
#if BUILDFLAG(IS_MAC)
  if (ShouldUseThumbnailCapturerMac(DesktopMediaList::Type::kWindow)) {
    return CreateThumbnailCapturerMac(DesktopMediaList::Type::kWindow);
  }
#endif  // BUILDFLAG(IS_MAC)

  std::unique_ptr<webrtc::DesktopCapturer> window_capturer =
      content::desktop_capture::CreateWindowCapturer();
  return window_capturer ? std::make_unique<DesktopCapturerWrapper>(
                               std::move(window_capturer))
                         : nullptr;
}

std::unique_ptr<ThumbnailCapturer> MakeScreenCapturer() {
#if BUILDFLAG(IS_MAC)
  if (ShouldUseThumbnailCapturerMac(DesktopMediaList::Type::kScreen)) {
    return CreateThumbnailCapturerMac(DesktopMediaList::Type::kScreen);
  }
#endif  // BUILDFLAG(IS_MAC)

  std::unique_ptr<webrtc::DesktopCapturer> screen_capturer =
      content::desktop_capture::CreateScreenCapturer();
  return screen_capturer ? std::make_unique<DesktopCapturerWrapper>(
                               std::move(screen_capturer))
                         : nullptr;
}

#if BUILDFLAG(IS_WIN)
BOOL CALLBACK EnumDisplayMonitorsCallback(HMONITOR monitor,
                                          HDC hdc,
                                          LPRECT rect,
                                          LPARAM data) {
  reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
  return TRUE;
}
#endif

}  // namespace

namespace gin {

template <>
struct Converter<electron::api::DesktopCapturer::Source> {
  static v8::Local<v8::Value> ToV8(
      v8::Isolate* isolate,
      const electron::api::DesktopCapturer::Source& source) {
    auto dict = gin_helper::Dictionary::CreateEmpty(isolate);
    content::DesktopMediaID id = source.media_list_source.id;
    dict.Set("name", base::UTF16ToUTF8(source.media_list_source.name));
    dict.Set("id", id.ToString());
    dict.Set("thumbnail",
             electron::api::NativeImage::Create(
                 isolate, gfx::Image(source.media_list_source.thumbnail)));
    dict.Set("display_id", source.display_id);
    if (source.fetch_icon) {
      dict.Set(
          "appIcon",
          electron::api::NativeImage::Create(
              isolate, gfx::Image(GetWindowIcon(source.media_list_source.id))));
    } else {
      dict.Set("appIcon", nullptr);
    }
    return ConvertToV8(isolate, dict);
  }
};

}  // namespace gin

namespace electron::api {

gin::WrapperInfo DesktopCapturer::kWrapperInfo = {gin::kEmbedderNativeGin};

DesktopCapturer::DesktopCapturer(v8::Isolate* isolate) {}

DesktopCapturer::~DesktopCapturer() = default;

DesktopCapturer::DesktopListListener::DesktopListListener(
    OnceCallback update_callback,
    OnceCallback failure_callback,
    bool skip_thumbnails)
    : update_callback_(std::move(update_callback)),
      failure_callback_(std::move(failure_callback)),
      have_thumbnail_(skip_thumbnails) {}

DesktopCapturer::DesktopListListener::~DesktopListListener() = default;

void DesktopCapturer::DesktopListListener::OnDelegatedSourceListSelection() {
  if (have_thumbnail_) {
    std::move(update_callback_).Run();
  } else {
    have_selection_ = true;
  }
}

void DesktopCapturer::DesktopListListener::OnSourceThumbnailChanged(int index) {
  if (have_selection_) {
    // This is called every time a thumbnail is refreshed. Reset variable to
    // ensure that the callback is not run again.
    have_selection_ = false;

    // PipeWire returns a single source, so index is not relevant.
    std::move(update_callback_).Run();
  } else {
    have_thumbnail_ = true;
  }
}

void DesktopCapturer::DesktopListListener::OnDelegatedSourceListDismissed() {
  std::move(failure_callback_).Run();
}

void DesktopCapturer::StartHandling(bool capture_window,
                                    bool capture_screen,
                                    const gfx::Size& thumbnail_size,
                                    bool fetch_window_icons) {
  fetch_window_icons_ = fetch_window_icons;
#if BUILDFLAG(IS_WIN)
  if (content::desktop_capture::CreateDesktopCaptureOptions()
          .allow_directx_capturer()) {
    // DxgiDuplicatorController should be alive in this scope according to
    // screen_capturer_win.cc.
    auto duplicator = webrtc::DxgiDuplicatorController::Instance();
    using_directx_capturer_ = webrtc::ScreenCapturerWinDirectx::IsSupported();
  }
#endif  // BUILDFLAG(IS_WIN)

  // clear any existing captured sources.
  captured_sources_.clear();

  if (capture_window && capture_screen) {
    // Some capturers like PipeWire support a single capturer for both screens
    // and windows. Use it if possible, treating both as window capture
    std::unique_ptr<webrtc::DesktopCapturer> desktop_capturer =
        webrtc::DesktopCapturer::CreateGenericCapturer(
            content::desktop_capture::CreateDesktopCaptureOptions());
    auto capturer = desktop_capturer ? std::make_unique<DesktopCapturerWrapper>(
                                           std::move(desktop_capturer))
                                     : nullptr;
    if (capturer && capturer->GetDelegatedSourceListController()) {
      capture_screen_ = false;
      capture_window_ = capture_window;
      window_capturer_ = std::make_unique<NativeDesktopMediaList>(
          DesktopMediaList::Type::kWindow, std::move(capturer));
      window_capturer_->SetThumbnailSize(thumbnail_size);

      OnceCallback update_callback = base::BindOnce(
          &DesktopCapturer::UpdateSourcesList, weak_ptr_factory_.GetWeakPtr(),
          window_capturer_.get());
      OnceCallback failure_callback = base::BindOnce(
          &DesktopCapturer::HandleFailure, weak_ptr_factory_.GetWeakPtr());

      window_listener_ = std::make_unique<DesktopListListener>(
          std::move(update_callback), std::move(failure_callback),
          thumbnail_size.IsEmpty());
      window_capturer_->StartUpdating(window_listener_.get());

      return;
    }
  }

  // Start listening for captured sources.
  capture_window_ = capture_window;
  capture_screen_ = capture_screen;

#if BUILDFLAG(IS_MAC)
  if (!ui::TryPromptUserForScreenCapture()) {
    HandleFailure();
    return;
  }
#endif

  {
    // Initialize the source list.
    // Apply the new thumbnail size and restart capture.
    if (capture_window) {
      auto capturer = MakeWindowCapturer();
      if (capturer) {
        window_capturer_ = std::make_unique<NativeDesktopMediaList>(
            DesktopMediaList::Type::kWindow, std::move(capturer));
        window_capturer_->SetThumbnailSize(thumbnail_size);
#if BUILDFLAG(IS_MAC)
        window_capturer_->skip_next_refresh_ =
            ShouldUseThumbnailCapturerMac(DesktopMediaList::Type::kWindow) ? 2
                                                                           : 0;
#endif

        OnceCallback update_callback = base::BindOnce(
            &DesktopCapturer::UpdateSourcesList, weak_ptr_factory_.GetWeakPtr(),
            window_capturer_.get());

        if (window_capturer_->IsSourceListDelegated()) {
          OnceCallback failure_callback = base::BindOnce(
              &DesktopCapturer::HandleFailure, weak_ptr_factory_.GetWeakPtr());
          window_listener_ = std::make_unique<DesktopListListener>(
              std::move(update_callback), std::move(failure_callback),
              thumbnail_size.IsEmpty());
          window_capturer_->StartUpdating(window_listener_.get());
        } else {
          window_capturer_->Update(std::move(update_callback),
                                   /* refresh_thumbnails = */ true);
        }
      }
    }

    if (capture_screen) {
      auto capturer = MakeScreenCapturer();
      if (capturer) {
        screen_capturer_ = std::make_unique<NativeDesktopMediaList>(
            DesktopMediaList::Type::kScreen, std::move(capturer));
        screen_capturer_->SetThumbnailSize(thumbnail_size);
#if BUILDFLAG(IS_MAC)
        screen_capturer_->skip_next_refresh_ =
            ShouldUseThumbnailCapturerMac(DesktopMediaList::Type::kScreen) ? 2
                                                                           : 0;
#endif

        OnceCallback update_callback = base::BindOnce(
            &DesktopCapturer::UpdateSourcesList, weak_ptr_factory_.GetWeakPtr(),
            screen_capturer_.get());

        if (screen_capturer_->IsSourceListDelegated()) {
          OnceCallback failure_callback = base::BindOnce(
              &DesktopCapturer::HandleFailure, weak_ptr_factory_.GetWeakPtr());
          screen_listener_ = std::make_unique<DesktopListListener>(
              std::move(update_callback), std::move(failure_callback),
              thumbnail_size.IsEmpty());
          screen_capturer_->StartUpdating(screen_listener_.get());
        } else {
          screen_capturer_->Update(std::move(update_callback),
                                   /* refresh_thumbnails = */ true);
        }
      }
    }
  }
}

void DesktopCapturer::UpdateSourcesList(DesktopMediaList* list) {
  if (capture_window_ &&
      list->GetMediaListType() == DesktopMediaList::Type::kWindow) {
    capture_window_ = false;
    std::vector<DesktopCapturer::Source> window_sources;
    window_sources.reserve(list->GetSourceCount());
    for (int i = 0; i < list->GetSourceCount(); i++) {
      window_sources.emplace_back(list->GetSource(i), std::string(),
                                  fetch_window_icons_);
    }
    std::move(window_sources.begin(), window_sources.end(),
              std::back_inserter(captured_sources_));
  }

  if (capture_screen_ &&
      list->GetMediaListType() == DesktopMediaList::Type::kScreen) {
    capture_screen_ = false;
    std::vector<DesktopCapturer::Source> screen_sources;
    screen_sources.reserve(list->GetSourceCount());
    for (int i = 0; i < list->GetSourceCount(); i++) {
      screen_sources.emplace_back(list->GetSource(i), std::string());
    }
#if BUILDFLAG(IS_WIN)
    // Gather the same unique screen IDs used by the electron.screen API in
    // order to provide an association between it and
    // desktopCapturer/getUserMedia. This is only required when using the
    // DirectX capturer, otherwise the IDs across the APIs already match.
    if (using_directx_capturer_) {
      std::vector<std::string> device_names;
      // Crucially, this list of device names will be in the same order as
      // |screen_sources|.
      if (!webrtc::DxgiDuplicatorController::Instance()->GetDeviceNames(
              &device_names)) {
        HandleFailure();
        return;
      }
      DCHECK_EQ(device_names.size(), screen_sources.size());

      std::vector<HMONITOR> monitors;
      EnumDisplayMonitors(nullptr, nullptr, EnumDisplayMonitorsCallback,
                          reinterpret_cast<LPARAM>(&monitors));

      base::flat_map<std::string, int64_t> device_name_to_id;
      device_name_to_id.reserve(monitors.size());
      for (auto* monitor : monitors) {
        MONITORINFOEX monitorInfo{{sizeof(MONITORINFOEX)}};
        if (!GetMonitorInfo(monitor, &monitorInfo)) {
          continue;
        }

        device_name_to_id[base::WideToUTF8(monitorInfo.szDevice)] =
            display::win::internal::DisplayInfo::DisplayIdFromMonitorInfo(
                monitorInfo);
      }

      int device_name_index = 0;
      for (auto& source : screen_sources) {
        const auto& device_name = device_names[device_name_index++];
        if (auto id_iter = device_name_to_id.find(device_name);
            id_iter != device_name_to_id.end()) {
          source.display_id = base::NumberToString(id_iter->second);
        }
      }
    }
#elif BUILDFLAG(IS_MAC)
    // On Mac, the IDs across the APIs match.
    for (auto& source : screen_sources) {
      source.display_id = base::NumberToString(source.media_list_source.id.id);
    }
#elif BUILDFLAG(IS_OZONE_X11)
    // On Linux, with X11, the source id is the numeric value of the
    // display name atom and the display id is either the EDID or the
    // loop index when that display was found (see
    // BuildDisplaysFromXRandRInfo in ui/base/x/x11_display_util.cc)
    const auto monitor_atom_to_display_id = MonitorAtomIdToDisplayId();
    for (auto& source : screen_sources) {
      auto display_id_iter =
          monitor_atom_to_display_id.find(source.media_list_source.id.id);
      if (display_id_iter != monitor_atom_to_display_id.end())
        source.display_id = base::NumberToString(display_id_iter->second);
    }
#endif
    std::move(screen_sources.begin(), screen_sources.end(),
              std::back_inserter(captured_sources_));
  }

  if (!capture_window_ && !capture_screen_)
    HandleSuccess();
}

void DesktopCapturer::HandleSuccess() {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);
  gin_helper::CallMethod(this, "_onfinished", captured_sources_);

  screen_capturer_.reset();
  window_capturer_.reset();

  Unpin();
}

void DesktopCapturer::HandleFailure() {
  v8::Isolate* isolate = JavascriptEnvironment::GetIsolate();
  v8::HandleScope scope(isolate);

  gin_helper::CallMethod(this, "_onerror", "Failed to get sources.");

  screen_capturer_.reset();
  window_capturer_.reset();

  Unpin();
}

// static
gin::Handle<DesktopCapturer> DesktopCapturer::Create(v8::Isolate* isolate) {
  auto handle = gin::CreateHandle(isolate, new DesktopCapturer(isolate));

  // Keep reference alive until capturing has finished.
  handle->Pin(isolate);

  return handle;
}

gin::ObjectTemplateBuilder DesktopCapturer::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<DesktopCapturer>::GetObjectTemplateBuilder(isolate)
      .SetMethod("startHandling", &DesktopCapturer::StartHandling);
}

const char* DesktopCapturer::GetTypeName() {
  return "DesktopCapturer";
}

}  // namespace electron::api

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin_helper::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("createDesktopCapturer",
                 &electron::api::DesktopCapturer::Create);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_browser_desktop_capturer, Initialize)
