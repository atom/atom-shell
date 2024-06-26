// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/api/electron_api_native_image.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "gin/arguments.h"
#include "gin/object_template_builder.h"
#include "gin/per_isolate_data.h"
#include "gin/wrappable.h"
#include "net/base/data_url.h"
#include "shell/browser/browser.h"
#include "shell/common/asar/asar_util.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_converters/gfx_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/function_template_extensions.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"
#include "shell/common/process_util.h"
#include "shell/common/skia_util.h"
#include "shell/common/thread_restrictions.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPixelRef.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_util.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_gdi_object.h"
#include "shell/common/asar/archive.h"
#include "ui/gfx/icon_util.h"
#endif

namespace electron::api {

namespace {

// This is needed to avoid a hard CHECK when certain aspects of
// ImageSkia are invoked before the browser process is ready,
// since supported scales are normally set by
// ui::ResourceBundle::InitSharedInstance during browser process startup.
void EnsureSupportedScaleFactors() {
  if (!electron::IsBrowserProcess())
    return;

  if (!Browser::Get()->is_ready())
    ui::SetSupportedResourceScaleFactors({ui::k100Percent});
}

// Get the scale factor from options object at the first argument
float GetScaleFactorFromOptions(gin::Arguments* args) {
  float scale_factor = 1.0f;
  gin_helper::Dictionary options;
  if (args->GetNext(&options))
    options.Get("scaleFactor", &scale_factor);
  return scale_factor;
}

base::FilePath NormalizePath(const base::FilePath& path) {
  if (!path.ReferencesParent()) {
    return path;
  }

  ScopedAllowBlockingForElectron allow_blocking;
  base::FilePath absolute_path = MakeAbsoluteFilePath(path);
  // MakeAbsoluteFilePath returns an empty path on failures so use original path
  if (absolute_path.empty()) {
    return path;
  } else {
    return absolute_path;
  }
}

#if BUILDFLAG(IS_MAC)
bool IsTemplateFilename(const base::FilePath& path) {
  return (base::MatchPattern(path.value(), "*Template.*") ||
          base::MatchPattern(path.value(), "*Template@*x.*"));
}
#endif

#if BUILDFLAG(IS_WIN)
base::win::ScopedHICON ReadICOFromPath(int size, const base::FilePath& path) {
  // If file is in asar archive, we extract it to a temp file so LoadImage can
  // load it.
  base::FilePath asar_path, relative_path;
  base::FilePath image_path(path);
  if (asar::GetAsarArchivePath(image_path, &asar_path, &relative_path)) {
    std::shared_ptr<asar::Archive> archive =
        asar::GetOrCreateAsarArchive(asar_path);
    if (archive)
      archive->CopyFileOut(relative_path, &image_path);
  }

  // Load the icon from file.
  return base::win::ScopedHICON(
      static_cast<HICON>(LoadImage(nullptr, image_path.value().c_str(),
                                   IMAGE_ICON, size, size, LR_LOADFROMFILE)));
}
#endif

}  // namespace

NativeImage::NativeImage(v8::Isolate* isolate, const gfx::Image& image)
    : image_(image), isolate_(isolate) {
  EnsureSupportedScaleFactors();
  UpdateExternalAllocatedMemoryUsage();
}

#if BUILDFLAG(IS_WIN)
NativeImage::NativeImage(v8::Isolate* isolate, const base::FilePath& hicon_path)
    : hicon_path_(hicon_path), isolate_(isolate) {
  EnsureSupportedScaleFactors();

  // Use the 256x256 icon as fallback icon.
  gfx::ImageSkia image_skia;
  electron::util::ReadImageSkiaFromICO(&image_skia, GetHICON(256));
  image_ = gfx::Image(image_skia);

  UpdateExternalAllocatedMemoryUsage();
}
#endif

NativeImage::~NativeImage() {
  isolate_->AdjustAmountOfExternalAllocatedMemory(-memory_usage_);
}

void NativeImage::UpdateExternalAllocatedMemoryUsage() {
  int32_t new_memory_usage = 0;

  if (image_.HasRepresentation(gfx::Image::kImageRepSkia)) {
    auto* const image_skia = image_.ToImageSkia();
    if (!image_skia->isNull()) {
      new_memory_usage = image_skia->bitmap()->computeByteSize();
    }
  }

  isolate_->AdjustAmountOfExternalAllocatedMemory(new_memory_usage -
                                                  memory_usage_);
  memory_usage_ = new_memory_usage;
}

// static
bool NativeImage::TryConvertNativeImage(v8::Isolate* isolate,
                                        v8::Local<v8::Value> image,
                                        NativeImage** native_image,
                                        OnConvertError on_error) {
  std::string error_message;

  base::FilePath icon_path;
  if (gin::ConvertFromV8(isolate, image, &icon_path)) {
    *native_image = NativeImage::CreateFromPath(isolate, icon_path).get();
    if ((*native_image)->image().IsEmpty()) {
#if BUILDFLAG(IS_WIN)
      const auto img_path = base::WideToUTF8(icon_path.value());
#else
      const auto img_path = icon_path.value();
#endif
      error_message = "Failed to load image from path '" + img_path + "'";
    }
  } else {
    if (!gin::ConvertFromV8(isolate, image, native_image)) {
      error_message = "Argument must be a file path or a NativeImage";
    }
  }

  if (!error_message.empty()) {
    switch (on_error) {
      case OnConvertError::kThrow:
        isolate->ThrowException(
            v8::Exception::Error(gin::StringToV8(isolate, error_message)));
        break;
      case OnConvertError::kWarn:
        LOG(WARNING) << error_message;
        break;
    }
    return false;
  }

  return true;
}

#if BUILDFLAG(IS_WIN)
HICON NativeImage::GetHICON(int size) {
  if (auto iter = hicons_.find(size); iter != hicons_.end())
    return iter->second.get();

  // First try loading the icon with specified size.
  if (!hicon_path_.empty()) {
    auto& hicon = hicons_[size];
    hicon = ReadICOFromPath(size, hicon_path_);
    return hicon.get();
  }

  // Then convert the image to ICO.
  if (image_.IsEmpty())
    return nullptr;

  auto& hicon = hicons_[size];
  hicon = IconUtil::CreateHICONFromSkBitmap(image_.AsBitmap());
  return hicon.get();
}
#endif

v8::Local<v8::Value> NativeImage::ToPNG(gin::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  if (scale_factor == 1.0f) {
    // Use raw 1x PNG bytes when available
    scoped_refptr<base::RefCountedMemory> png = image_.As1xPNGBytes();
    if (png->size() > 0) {
      const char* data = reinterpret_cast<const char*>(png->front());
      size_t size = png->size();
      return node::Buffer::Copy(args->isolate(), data, size).ToLocalChecked();
    }
  }

  const SkBitmap bitmap =
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap();
  std::vector<unsigned char> encoded;
  gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false, &encoded);
  const char* data = reinterpret_cast<char*>(encoded.data());
  size_t size = encoded.size();
  return node::Buffer::Copy(args->isolate(), data, size).ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::ToBitmap(gin::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  const SkBitmap bitmap =
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap();

  SkImageInfo info =
      SkImageInfo::MakeN32Premul(bitmap.width(), bitmap.height());

  auto array_buffer =
      v8::ArrayBuffer::New(args->isolate(), info.computeMinByteSize());
  auto backing_store = array_buffer->GetBackingStore();
  if (bitmap.readPixels(info, backing_store->Data(), info.minRowBytes(), 0,
                        0)) {
    return node::Buffer::New(args->isolate(), array_buffer, 0,
                             info.computeMinByteSize())
        .ToLocalChecked();
  }
  return node::Buffer::New(args->isolate(), 0).ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::ToJPEG(v8::Isolate* isolate, int quality) {
  std::vector<unsigned char> output;
  gfx::JPEG1xEncodedDataFromImage(image_, quality, &output);
  if (output.empty())
    return node::Buffer::New(isolate, 0).ToLocalChecked();
  return node::Buffer::Copy(isolate,
                            reinterpret_cast<const char*>(&output.front()),
                            output.size())
      .ToLocalChecked();
}

std::string NativeImage::ToDataURL(gin::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  return webui::GetBitmapDataUrl(
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap());
}

v8::Local<v8::Value> NativeImage::GetBitmap(gin::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  const SkBitmap bitmap =
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap();
  SkPixelRef* ref = bitmap.pixelRef();
  if (!ref)
    return node::Buffer::New(args->isolate(), 0).ToLocalChecked();
  return node::Buffer::Copy(args->isolate(),
                            reinterpret_cast<char*>(ref->pixels()),
                            bitmap.computeByteSize())
      .ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::GetNativeHandle(
    gin_helper::ErrorThrower thrower) {
#if BUILDFLAG(IS_MAC)
  if (IsEmpty())
    return node::Buffer::New(thrower.isolate(), 0).ToLocalChecked();

  NSImage* ptr = image_.AsNSImage();
  return node::Buffer::Copy(thrower.isolate(), reinterpret_cast<char*>(ptr),
                            sizeof(void*))
      .ToLocalChecked();
#else
  thrower.ThrowError("Not implemented");
  return v8::Undefined(thrower.isolate());
#endif
}

bool NativeImage::IsEmpty() {
  return image_.IsEmpty();
}

gfx::Size NativeImage::GetSize(const std::optional<float> scale_factor) {
  float sf = scale_factor.value_or(1.0f);
  gfx::ImageSkiaRep image_rep = image_.AsImageSkia().GetRepresentation(sf);

  return gfx::Size(image_rep.GetWidth(), image_rep.GetHeight());
}

std::vector<float> NativeImage::GetScaleFactors() {
  gfx::ImageSkia image_skia = image_.AsImageSkia();
  std::vector<float> scale_factors;
  for (const auto& rep : image_skia.image_reps()) {
    scale_factors.push_back(rep.scale());
  }
  return scale_factors;
}

float NativeImage::GetAspectRatio(const std::optional<float> scale_factor) {
  float sf = scale_factor.value_or(1.0f);
  gfx::Size size = GetSize(sf);
  if (size.IsEmpty())
    return 1.f;
  else
    return static_cast<float>(size.width()) / static_cast<float>(size.height());
}

gin::Handle<NativeImage> NativeImage::Resize(gin::Arguments* args,
                                             base::Value::Dict options) {
  float scale_factor = GetScaleFactorFromOptions(args);

  gfx::Size size = GetSize(scale_factor);
  std::optional<int> new_width = options.FindInt("width");
  std::optional<int> new_height = options.FindInt("height");
  int width = new_width.value_or(size.width());
  int height = new_height.value_or(size.height());
  size.SetSize(width, height);

  if (width <= 0 && height <= 0) {
    return CreateEmpty(args->isolate());
  } else if (new_width && !new_height) {
    // Scale height to preserve original aspect ratio
    size.set_height(width);
    size =
        gfx::ScaleToRoundedSize(size, 1.f, 1.f / GetAspectRatio(scale_factor));
  } else if (new_height && !new_width) {
    // Scale width to preserve original aspect ratio
    size.set_width(height);
    size = gfx::ScaleToRoundedSize(size, GetAspectRatio(scale_factor), 1.f);
  }

  skia::ImageOperations::ResizeMethod method =
      skia::ImageOperations::ResizeMethod::RESIZE_BEST;
  std::string* quality = options.FindString("quality");
  if (quality && *quality == "good")
    method = skia::ImageOperations::ResizeMethod::RESIZE_GOOD;
  else if (quality && *quality == "better")
    method = skia::ImageOperations::ResizeMethod::RESIZE_BETTER;

  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      image_.AsImageSkia(), method, size);
  return gin::CreateHandle(
      args->isolate(), new NativeImage(args->isolate(), gfx::Image(resized)));
}

gin::Handle<NativeImage> NativeImage::Crop(v8::Isolate* isolate,
                                           const gfx::Rect& rect) {
  gfx::ImageSkia cropped =
      gfx::ImageSkiaOperations::ExtractSubset(image_.AsImageSkia(), rect);
  return gin::CreateHandle(isolate,
                           new NativeImage(isolate, gfx::Image(cropped)));
}

void NativeImage::AddRepresentation(const gin_helper::Dictionary& options) {
  int width = 0;
  int height = 0;
  float scale_factor = 1.0f;
  options.Get("width", &width);
  options.Get("height", &height);
  options.Get("scaleFactor", &scale_factor);

  bool skia_rep_added = false;
  gfx::ImageSkia image_skia = image_.AsImageSkia();

  v8::Local<v8::Value> buffer;
  GURL url;
  if (options.Get("buffer", &buffer) && node::Buffer::HasInstance(buffer)) {
    auto* data = reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer));
    auto size = node::Buffer::Length(buffer);
    skia_rep_added = electron::util::AddImageSkiaRepFromBuffer(
        &image_skia, data, size, width, height, scale_factor);
  } else if (options.Get("dataURL", &url)) {
    std::string mime_type, charset, data;
    if (net::DataURL::Parse(url, &mime_type, &charset, &data)) {
      auto* data_ptr = reinterpret_cast<const unsigned char*>(data.c_str());
      if (mime_type == "image/png") {
        skia_rep_added = electron::util::AddImageSkiaRepFromPNG(
            &image_skia, data_ptr, data.size(), scale_factor);
      } else if (mime_type == "image/jpeg") {
        skia_rep_added = electron::util::AddImageSkiaRepFromJPEG(
            &image_skia, data_ptr, data.size(), scale_factor);
      }
    }
  }

  // Re-initialize image when first representation is added to an empty image
  if (skia_rep_added && IsEmpty()) {
    gfx::Image image(image_skia);
    image_ = std::move(image);
  }
}

#if !BUILDFLAG(IS_MAC)
void NativeImage::SetTemplateImage(bool setAsTemplate) {}

bool NativeImage::IsTemplateImage() {
  return false;
}
#endif

// static
gin::Handle<NativeImage> NativeImage::CreateEmpty(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, new NativeImage(isolate, gfx::Image()));
}

// static
gin::Handle<NativeImage> NativeImage::Create(v8::Isolate* isolate,
                                             const gfx::Image& image) {
  return gin::CreateHandle(isolate, new NativeImage(isolate, image));
}

// static
gin::Handle<NativeImage> NativeImage::CreateFromPNG(v8::Isolate* isolate,
                                                    const char* buffer,
                                                    size_t length) {
  gfx::ImageSkia image_skia;
  electron::util::AddImageSkiaRepFromPNG(
      &image_skia, reinterpret_cast<const unsigned char*>(buffer), length, 1.0);
  return Create(isolate, gfx::Image(image_skia));
}

// static
gin::Handle<NativeImage> NativeImage::CreateFromJPEG(v8::Isolate* isolate,
                                                     const char* buffer,
                                                     size_t length) {
  gfx::ImageSkia image_skia;
  electron::util::AddImageSkiaRepFromJPEG(
      &image_skia, reinterpret_cast<const unsigned char*>(buffer), length, 1.0);
  return Create(isolate, gfx::Image(image_skia));
}

// static
gin::Handle<NativeImage> NativeImage::CreateFromPath(
    v8::Isolate* isolate,
    const base::FilePath& path) {
  base::FilePath image_path = NormalizePath(path);
#if BUILDFLAG(IS_WIN)
  if (image_path.MatchesExtension(FILE_PATH_LITERAL(".ico"))) {
    return gin::CreateHandle(isolate, new NativeImage(isolate, image_path));
  }
#endif
  gfx::ImageSkia image_skia;
  electron::util::PopulateImageSkiaRepsFromPath(&image_skia, image_path);
  gfx::Image image(image_skia);
  gin::Handle<NativeImage> handle = Create(isolate, image);
#if BUILDFLAG(IS_MAC)
  if (IsTemplateFilename(image_path))
    handle->SetTemplateImage(true);
#endif
  return handle;
}

// static
gin::Handle<NativeImage> NativeImage::CreateFromBitmap(
    gin_helper::ErrorThrower thrower,
    v8::Local<v8::Value> buffer,
    const gin_helper::Dictionary& options) {
  if (!node::Buffer::HasInstance(buffer)) {
    thrower.ThrowError("buffer must be a node Buffer");
    return gin::Handle<NativeImage>();
  }

  unsigned int width = 0;
  unsigned int height = 0;
  double scale_factor = 1.;

  if (!options.Get("width", &width)) {
    thrower.ThrowError("width is required");
    return gin::Handle<NativeImage>();
  }

  if (!options.Get("height", &height)) {
    thrower.ThrowError("height is required");
    return gin::Handle<NativeImage>();
  }

  auto info = SkImageInfo::MakeN32(width, height, kPremul_SkAlphaType);
  auto size_bytes = info.computeMinByteSize();

  if (size_bytes != node::Buffer::Length(buffer)) {
    thrower.ThrowError("invalid buffer size");
    return gin::Handle<NativeImage>();
  }

  options.Get("scaleFactor", &scale_factor);

  if (width == 0 || height == 0) {
    return CreateEmpty(thrower.isolate());
  }

  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height, false);
  bitmap.writePixels({info, node::Buffer::Data(buffer), bitmap.rowBytes()});

  gfx::ImageSkia image_skia =
      gfx::ImageSkia::CreateFromBitmap(bitmap, scale_factor);

  return Create(thrower.isolate(), gfx::Image(image_skia));
}

// static
gin::Handle<NativeImage> NativeImage::CreateFromBuffer(
    gin_helper::ErrorThrower thrower,
    v8::Local<v8::Value> buffer,
    gin::Arguments* args) {
  if (!node::Buffer::HasInstance(buffer)) {
    thrower.ThrowError("buffer must be a node Buffer");
    return gin::Handle<NativeImage>();
  }

  int width = 0;
  int height = 0;
  double scale_factor = 1.;

  gin_helper::Dictionary options;
  if (args->GetNext(&options)) {
    options.Get("width", &width);
    options.Get("height", &height);
    options.Get("scaleFactor", &scale_factor);
  }

  gfx::ImageSkia image_skia;
  electron::util::AddImageSkiaRepFromBuffer(
      &image_skia, reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer)),
      node::Buffer::Length(buffer), width, height, scale_factor);
  return Create(args->isolate(), gfx::Image(image_skia));
}

// static
gin::Handle<NativeImage> NativeImage::CreateFromDataURL(v8::Isolate* isolate,
                                                        const GURL& url) {
  std::string mime_type, charset, data;
  if (net::DataURL::Parse(url, &mime_type, &charset, &data)) {
    if (mime_type == "image/png")
      return CreateFromPNG(isolate, data.c_str(), data.size());
    else if (mime_type == "image/jpeg")
      return CreateFromJPEG(isolate, data.c_str(), data.size());
  }

  return CreateEmpty(isolate);
}

#if !BUILDFLAG(IS_MAC)
gin::Handle<NativeImage> NativeImage::CreateFromNamedImage(gin::Arguments* args,
                                                           std::string name) {
  return CreateEmpty(args->isolate());
}
#endif

// static
gin::ObjectTemplateBuilder NativeImage::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  gin::PerIsolateData* data = gin::PerIsolateData::From(isolate);
  auto* wrapper_info = &kWrapperInfo;
  v8::Local<v8::FunctionTemplate> constructor =
      data->GetFunctionTemplate(wrapper_info);
  if (constructor.IsEmpty()) {
    constructor = v8::FunctionTemplate::New(isolate);
    constructor->SetClassName(gin::StringToV8(isolate, GetTypeName()));
    data->SetFunctionTemplate(wrapper_info, constructor);
  }
  return gin::ObjectTemplateBuilder(isolate, GetTypeName(),
                                    constructor->InstanceTemplate())
      .SetMethod("toPNG", &NativeImage::ToPNG)
      .SetMethod("toJPEG", &NativeImage::ToJPEG)
      .SetMethod("toBitmap", &NativeImage::ToBitmap)
      .SetMethod("getBitmap", &NativeImage::GetBitmap)
      .SetMethod("getScaleFactors", &NativeImage::GetScaleFactors)
      .SetMethod("getNativeHandle", &NativeImage::GetNativeHandle)
      .SetMethod("toDataURL", &NativeImage::ToDataURL)
      .SetMethod("isEmpty", &NativeImage::IsEmpty)
      .SetMethod("getSize", &NativeImage::GetSize)
      .SetMethod("setTemplateImage", &NativeImage::SetTemplateImage)
      .SetMethod("isTemplateImage", &NativeImage::IsTemplateImage)
      .SetProperty("isMacTemplateImage", &NativeImage::IsTemplateImage,
                   &NativeImage::SetTemplateImage)
      .SetMethod("resize", &NativeImage::Resize)
      .SetMethod("crop", &NativeImage::Crop)
      .SetMethod("getAspectRatio", &NativeImage::GetAspectRatio)
      .SetMethod("addRepresentation", &NativeImage::AddRepresentation);
}

const char* NativeImage::GetTypeName() {
  return "NativeImage";
}

// static
gin::WrapperInfo NativeImage::kWrapperInfo = {gin::kEmbedderNativeGin};

}  // namespace electron::api

namespace {

using electron::api::NativeImage;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary dict(isolate, exports);
  auto native_image = gin_helper::Dictionary::CreateEmpty(isolate);
  dict.Set("nativeImage", native_image);

  native_image.SetMethod("createEmpty", &NativeImage::CreateEmpty);
  native_image.SetMethod("createFromPath", &NativeImage::CreateFromPath);
  native_image.SetMethod("createFromBitmap", &NativeImage::CreateFromBitmap);
  native_image.SetMethod("createFromBuffer", &NativeImage::CreateFromBuffer);
  native_image.SetMethod("createFromDataURL", &NativeImage::CreateFromDataURL);
  native_image.SetMethod("createFromNamedImage",
                         &NativeImage::CreateFromNamedImage);
#if !BUILDFLAG(IS_LINUX)
  native_image.SetMethod("createThumbnailFromPath",
                         &NativeImage::CreateThumbnailFromPath);
#endif
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_common_native_image, Initialize)
