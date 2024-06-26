// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "services/network/public/cpp/network_switches.h"
#include "shell/common/gin_converters/base_converter.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_includes.h"

namespace {

bool HasSwitch(const std::string& name) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(name);
}

base::CommandLine::StringType GetSwitchValue(const std::string& name) {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueNative(name);
}

void AppendSwitch(const std::string& switch_string,
                  gin_helper::Arguments* args) {
  auto* command_line = base::CommandLine::ForCurrentProcess();

  if (base::EndsWith(switch_string, "-path",
                     base::CompareCase::INSENSITIVE_ASCII) ||
      switch_string == network::switches::kLogNetLog) {
    base::FilePath path;
    args->GetNext(&path);
    command_line->AppendSwitchPath(switch_string, path);
    return;
  }

  base::CommandLine::StringType value;
  if (args->GetNext(&value))
    command_line->AppendSwitchNative(switch_string, value);
  else
    command_line->AppendSwitch(switch_string);
}

void RemoveSwitch(const std::string& switch_string) {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  command_line->RemoveSwitch(switch_string);
}

void AppendArg(const std::string& arg) {
  auto* command_line = base::CommandLine::ForCurrentProcess();

  command_line->AppendArg(arg);
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  gin_helper::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("hasSwitch", &HasSwitch);
  dict.SetMethod("getSwitchValue", &GetSwitchValue);
  dict.SetMethod("appendSwitch", &AppendSwitch);
  dict.SetMethod("removeSwitch", &RemoveSwitch);
  dict.SetMethod("appendArgument", &AppendArg);
}

}  // namespace

NODE_LINKED_BINDING_CONTEXT_AWARE(electron_common_command_line, Initialize)
