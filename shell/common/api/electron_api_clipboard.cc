// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/api/electron_api_clipboard.h"

#include <map>

#include "base/containers/contains.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "shell/browser/browser.h"
#include "shell/common/gin_converters/image_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/gfx/codec/png_codec.h"

namespace electron::api {

ui::ClipboardBuffer Clipboard::GetClipboardBuffer(gin_helper::Arguments* args) {
  std::string type;
  if (args->GetNext(&type) && type == "selection")
    return ui::ClipboardBuffer::kSelection;
  else
    return ui::ClipboardBuffer::kCopyPaste;
}

std::vector<std::u16string> Clipboard::AvailableFormats(
    gin_helper::Arguments* args) {
  std::vector<std::u16string> format_types;
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->ReadAvailableTypes(GetClipboardBuffer(args),
                                /* data_dst = */ nullptr, &format_types);
  return format_types;
}

bool Clipboard::Has(const std::string& format_string,
                    gin_helper::Arguments* args) {
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  ui::ClipboardFormatType format(
      ui::ClipboardFormatType::GetType(format_string));
  if (format.GetName().empty())
    format = ui::ClipboardFormatType::CustomPlatformType(format_string);
  return clipboard->IsFormatAvailable(format, GetClipboardBuffer(args),
                                      /* data_dst = */ nullptr);
}

std::string Clipboard::Read(const std::string& format_string) {
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  // Prefer raw platform format names
  ui::ClipboardFormatType rawFormat(
      ui::ClipboardFormatType::CustomPlatformType(format_string));
  bool rawFormatAvailable = clipboard->IsFormatAvailable(
      rawFormat, ui::ClipboardBuffer::kCopyPaste, /* data_dst = */ nullptr);
#if BUILDFLAG(IS_LINUX)
  if (!rawFormatAvailable) {
    rawFormatAvailable = clipboard->IsFormatAvailable(
        rawFormat, ui::ClipboardBuffer::kSelection, /* data_dst = */ nullptr);
  }
#endif
  if (rawFormatAvailable) {
    std::string data;
    clipboard->ReadData(rawFormat, /* data_dst = */ nullptr, &data);
    return data;
  }
  // Otherwise, resolve custom format names
  std::map<std::string, std::string> custom_format_names;
  custom_format_names =
      clipboard->ExtractCustomPlatformNames(ui::ClipboardBuffer::kCopyPaste,
                                            /* data_dst = */ nullptr);
#if BUILDFLAG(IS_LINUX)
  if (!base::Contains(custom_format_names, format_string)) {
    custom_format_names =
        clipboard->ExtractCustomPlatformNames(ui::ClipboardBuffer::kSelection,
                                              /* data_dst = */ nullptr);
  }
#endif

  ui::ClipboardFormatType format;
  if (base::Contains(custom_format_names, format_string)) {
    format =
        ui::ClipboardFormatType(ui::ClipboardFormatType::CustomPlatformType(
            custom_format_names[format_string]));
  } else {
    format = ui::ClipboardFormatType(
        ui::ClipboardFormatType::CustomPlatformType(format_string));
  }
  std::string data;
  clipboard->ReadData(format, /* data_dst = */ nullptr, &data);
  return data;
}

v8::Local<v8::Value> Clipboard::ReadBuffer(const std::string& format_string,
                                           gin_helper::Arguments* args) {
  std::string data = Read(format_string);
  return node::Buffer::Copy(args->isolate(), data.data(), data.length())
      .ToLocalChecked();
}

void Clipboard::WriteBuffer(const std::string& format,
                            const v8::Local<v8::Value> buffer,
                            gin_helper::Arguments* args) {
  if (!node::Buffer::HasInstance(buffer)) {
    args->ThrowError("buffer must be a node Buffer");
    return;
  }

  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  base::span<const uint8_t> payload_span(
      reinterpret_cast<const uint8_t*>(node::Buffer::Data(buffer)),
      node::Buffer::Length(buffer));
  writer.WriteUnsafeRawData(base::UTF8ToUTF16(format),
                            mojo_base::BigBuffer(payload_span));
}

void Clipboard::Write(const gin_helper::Dictionary& data,
                      gin_helper::Arguments* args) {
  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  std::u16string text, html, bookmark;
  gfx::Image image;

  if (data.Get("text", &text)) {
    writer.WriteText(text);

    if (data.Get("bookmark", &bookmark))
      writer.WriteBookmark(bookmark, base::UTF16ToUTF8(text));
  }

  if (data.Get("rtf", &text)) {
    std::string rtf = base::UTF16ToUTF8(text);
    writer.WriteRTF(rtf);
  }

  if (data.Get("html", &html))
    writer.WriteHTML(html, std::string());

  if (data.Get("image", &image))
    writer.WriteImage(image.AsBitmap());
}

std::u16string Clipboard::ReadText(gin_helper::Arguments* args) {
  std::u16string data;
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  auto type = GetClipboardBuffer(args);
  if (clipboard->IsFormatAvailable(ui::ClipboardFormatType::PlainTextType(),
                                   type, /* data_dst = */ nullptr)) {
    clipboard->ReadText(type, /* data_dst = */ nullptr, &data);
  } else {
#if BUILDFLAG(IS_WIN)
    if (clipboard->IsFormatAvailable(ui::ClipboardFormatType::PlainTextAType(),
                                     type,
                                     /* data_dst = */ nullptr)) {
      std::string result;
      clipboard->ReadAsciiText(type, /* data_dst = */ nullptr, &result);
      data = base::ASCIIToUTF16(result);
    }
#endif
  }
  return data;
}

void Clipboard::WriteText(const std::u16string& text,
                          gin_helper::Arguments* args) {
  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  writer.WriteText(text);
}

std::u16string Clipboard::ReadRTF(gin_helper::Arguments* args) {
  std::string data;
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->ReadRTF(GetClipboardBuffer(args), /* data_dst = */ nullptr, &data);
  return base::UTF8ToUTF16(data);
}

void Clipboard::WriteRTF(const std::string& text, gin_helper::Arguments* args) {
  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  writer.WriteRTF(text);
}

std::u16string Clipboard::ReadHTML(gin_helper::Arguments* args) {
  std::u16string data;
  std::u16string html;
  std::string url;
  uint32_t start;
  uint32_t end;
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->ReadHTML(GetClipboardBuffer(args), /* data_dst = */ nullptr, &html,
                      &url, &start, &end);
  data = html.substr(start, end - start);
  return data;
}

void Clipboard::WriteHTML(const std::u16string& html,
                          gin_helper::Arguments* args) {
  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  writer.WriteHTML(html, std::string());
}

v8::Local<v8::Value> Clipboard::ReadBookmark(gin_helper::Arguments* args) {
  std::u16string title;
  std::string url;
  auto dict = gin_helper::Dictionary::CreateEmpty(args->isolate());
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->ReadBookmark(/* data_dst = */ nullptr, &title, &url);
  dict.Set("title", title);
  dict.Set("url", url);
  return dict.GetHandle();
}

void Clipboard::WriteBookmark(const std::u16string& title,
                              const std::string& url,
                              gin_helper::Arguments* args) {
  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  writer.WriteBookmark(title, url);
}

gfx::Image Clipboard::ReadImage(gin_helper::Arguments* args) {
  // The ReadPng uses thread pool which requires app ready.
  if (IsBrowserProcess() && !Browser::Get()->is_ready()) {
    args->ThrowError(
        "clipboard.readImage is available only after app ready in the main "
        "process");
    return gfx::Image();
  }

  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  std::optional<gfx::Image> image;

  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  base::RepeatingClosure callback = run_loop.QuitClosure();
  clipboard->ReadPng(
      GetClipboardBuffer(args),
      /* data_dst = */ nullptr,
      base::BindOnce(
          [](std::optional<gfx::Image>* image, base::RepeatingClosure cb,
             const std::vector<uint8_t>& result) {
            SkBitmap bitmap;
            gfx::PNGCodec::Decode(result.data(), result.size(), &bitmap);
            image->emplace(gfx::Image::CreateFrom1xBitmap(bitmap));
            std::move(cb).Run();
          },
          &image, std::move(callback)));
  run_loop.Run();

  DCHECK(image.has_value());
  return image.value();
}

void Clipboard::WriteImage(const gfx::Image& image,
                           gin_helper::Arguments* args) {
  ui::ScopedClipboardWriter writer(GetClipboardBuffer(args));
  SkBitmap orig = image.AsBitmap();
  SkBitmap bmp;

  if (bmp.tryAllocPixels(orig.info()) &&
      orig.readPixels(bmp.info(), bmp.getPixels(), bmp.rowBytes(), 0, 0)) {
    writer.WriteImage(bmp);
  }
}

#if !BUILDFLAG(IS_MAC)
void Clipboard::WriteFindText(const std::u16string& text) {}
std::u16string Clipboard::ReadFindText() {
  return std::u16string();
}
#endif

void Clipboard::Clear(gin_helper::Arguments* args) {
  ui::Clipboard::GetForCurrentThread()->Clear(GetClipboardBuffer(args));
}

// This exists for testing purposes ONLY.
void Clipboard::WriteFilesForTesting(const std::vector<base::FilePath>& files) {
  std::vector<ui::FileInfo> file_infos;
  for (const auto& file : files) {
    file_infos.emplace_back(ui::FileInfo(ui::FileInfo(file, file.BaseName())));
  }

  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteFilenames(ui::FileInfosToURIList(file_infos));
}

}  // namespace electron::api

namespace {

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin_helper::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("availableFormats",
                 &electron::api::Clipboard::AvailableFormats);
  dict.SetMethod("has", &electron::api::Clipboard::Has);
  dict.SetMethod("read", &electron::api::Clipboard::Read);
  dict.SetMethod("write", &electron::api::Clipboard::Write);
  dict.SetMethod("readText", &electron::api::Clipboard::ReadText);
  dict.SetMethod("writeText", &electron::api::Clipboard::WriteText);
  dict.SetMethod("readRTF", &electron::api::Clipboard::ReadRTF);
  dict.SetMethod("writeRTF", &electron::api::Clipboard::WriteRTF);
  dict.SetMethod("readHTML", &electron::api::Clipboard::ReadHTML);
  dict.SetMethod("writeHTML", &electron::api::Clipboard::WriteHTML);
  dict.SetMethod("readBookmark", &electron::api::Clipboard::ReadBookmark);
  dict.SetMethod("writeBookmark", &electron::api::Clipboard::WriteBookmark);
  dict.SetMethod("readImage", &electron::api::Clipboard::ReadImage);
  dict.SetMethod("writeImage", &electron::api::Clipboard::WriteImage);
  dict.SetMethod("readFindText", &electron::api::Clipboard::ReadFindText);
  dict.SetMethod("writeFindText", &electron::api::Clipboard::WriteFindText);
  dict.SetMethod("readBuffer", &electron::api::Clipboard::ReadBuffer);
  dict.SetMethod("writeBuffer", &electron::api::Clipboard::WriteBuffer);
  dict.SetMethod("_writeFilesForTesting",
                 &electron::api::Clipboard::WriteFilesForTesting);
  dict.SetMethod("clear", &electron::api::Clipboard::Clear);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_common_clipboard, Initialize)
