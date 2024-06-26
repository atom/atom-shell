// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_API_ELECTRON_API_CLIPBOARD_H_
#define ELECTRON_SHELL_COMMON_API_ELECTRON_API_CLIPBOARD_H_

#include <string>
#include <vector>

#include "shell/common/gin_converters/file_path_converter.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/gfx/image/image.h"
#include "v8/include/v8.h"

namespace gin_helper {
class Arguments;
class Dictionary;
}  // namespace gin_helper

namespace electron::api {

class Clipboard {
 public:
  // disable copy
  Clipboard(const Clipboard&) = delete;
  Clipboard& operator=(const Clipboard&) = delete;

  static ui::ClipboardBuffer GetClipboardBuffer(gin_helper::Arguments* args);
  static std::vector<std::u16string> AvailableFormats(
      gin_helper::Arguments* args);
  static bool Has(const std::string& format_string,
                  gin_helper::Arguments* args);
  static void Clear(gin_helper::Arguments* args);

  static std::string Read(const std::string& format_string);
  static void Write(const gin_helper::Dictionary& data,
                    gin_helper::Arguments* args);

  static std::u16string ReadText(gin_helper::Arguments* args);
  static void WriteText(const std::u16string& text,
                        gin_helper::Arguments* args);

  static std::u16string ReadRTF(gin_helper::Arguments* args);
  static void WriteRTF(const std::string& text, gin_helper::Arguments* args);

  static std::u16string ReadHTML(gin_helper::Arguments* args);
  static void WriteHTML(const std::u16string& html,
                        gin_helper::Arguments* args);

  static v8::Local<v8::Value> ReadBookmark(gin_helper::Arguments* args);
  static void WriteBookmark(const std::u16string& title,
                            const std::string& url,
                            gin_helper::Arguments* args);

  static gfx::Image ReadImage(gin_helper::Arguments* args);
  static void WriteImage(const gfx::Image& image, gin_helper::Arguments* args);

  static std::u16string ReadFindText();
  static void WriteFindText(const std::u16string& text);

  static v8::Local<v8::Value> ReadBuffer(const std::string& format_string,
                                         gin_helper::Arguments* args);
  static void WriteBuffer(const std::string& format_string,
                          const v8::Local<v8::Value> buffer,
                          gin_helper::Arguments* args);

  static void WriteFilesForTesting(const std::vector<base::FilePath>& files);
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_COMMON_API_ELECTRON_API_CLIPBOARD_H_
