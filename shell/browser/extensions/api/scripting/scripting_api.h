// Copyright 2023 Microsoft, GmbH
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_EXTENSIONS_API_SCRIPTING_SCRIPTING_API_H_
#define ELECTRON_SHELL_BROWSER_EXTENSIONS_API_SCRIPTING_SCRIPTING_API_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "chrome/common/extensions/api/scripting.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/script_executor.h"
#include "extensions/browser/scripting_utils.h"
#include "extensions/common/mojom/code_injection.mojom.h"
#include "extensions/common/user_script.h"

namespace extensions {

// A simple helper struct to represent a read file (either CSS or JS) to be
// injected.
struct InjectedFileSource {
  InjectedFileSource(std::string file_name, std::unique_ptr<std::string> data);
  InjectedFileSource(InjectedFileSource&&);
  ~InjectedFileSource();

  std::string file_name;
  std::unique_ptr<std::string> data;
};

class ScriptingExecuteScriptFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.executeScript", SCRIPTING_EXECUTESCRIPT)

  ScriptingExecuteScriptFunction();
  ScriptingExecuteScriptFunction(const ScriptingExecuteScriptFunction&) =
      delete;
  ScriptingExecuteScriptFunction& operator=(
      const ScriptingExecuteScriptFunction&) = delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingExecuteScriptFunction() override;

  // Called when the resource files to be injected has been loaded.
  void DidLoadResources(std::vector<InjectedFileSource> file_sources,
                        std::optional<std::string> load_error);

  // Triggers the execution of `sources` in the appropriate context.
  // Returns true on success; on failure, populates `error`.
  bool Execute(std::vector<mojom::JSSourcePtr> sources, std::string* error);

  // Invoked when script execution is complete.
  void OnScriptExecuted(std::vector<ScriptExecutor::FrameResult> frame_results);

  api::scripting::ScriptInjection injection_;
};

class ScriptingInsertCSSFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.insertCSS", SCRIPTING_INSERTCSS)

  ScriptingInsertCSSFunction();
  ScriptingInsertCSSFunction(const ScriptingInsertCSSFunction&) = delete;
  ScriptingInsertCSSFunction& operator=(const ScriptingInsertCSSFunction&) =
      delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingInsertCSSFunction() override;

  // Called when the resource files to be injected has been loaded.
  void DidLoadResources(std::vector<InjectedFileSource> file_sources,
                        std::optional<std::string> load_error);

  // Triggers the execution of `sources` in the appropriate context.
  // Returns true on success; on failure, populates `error`.
  bool Execute(std::vector<mojom::CSSSourcePtr> sources, std::string* error);

  // Called when the CSS insertion is complete.
  void OnCSSInserted(std::vector<ScriptExecutor::FrameResult> results);

  api::scripting::CSSInjection injection_;
};

class ScriptingRemoveCSSFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.removeCSS", SCRIPTING_REMOVECSS)

  ScriptingRemoveCSSFunction();
  ScriptingRemoveCSSFunction(const ScriptingRemoveCSSFunction&) = delete;
  ScriptingRemoveCSSFunction& operator=(const ScriptingRemoveCSSFunction&) =
      delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingRemoveCSSFunction() override;

  // Called when the CSS removal is complete.
  void OnCSSRemoved(std::vector<ScriptExecutor::FrameResult> results);
};

class ScriptingRegisterContentScriptsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.registerContentScripts",
                             SCRIPTING_REGISTERCONTENTSCRIPTS)

  ScriptingRegisterContentScriptsFunction();
  ScriptingRegisterContentScriptsFunction(
      const ScriptingRegisterContentScriptsFunction&) = delete;
  ScriptingRegisterContentScriptsFunction& operator=(
      const ScriptingRegisterContentScriptsFunction&) = delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingRegisterContentScriptsFunction() override;

  // Called when script files have been checked.
  void OnContentScriptFilesValidated(
      std::set<std::string> persistent_script_ids,
      scripting::ValidateScriptsResult result);

  // Called when content scripts have been registered.
  void OnContentScriptsRegistered(const std::optional<std::string>& error);
};

class ScriptingGetRegisteredContentScriptsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.getRegisteredContentScripts",
                             SCRIPTING_GETREGISTEREDCONTENTSCRIPTS)

  ScriptingGetRegisteredContentScriptsFunction();
  ScriptingGetRegisteredContentScriptsFunction(
      const ScriptingGetRegisteredContentScriptsFunction&) = delete;
  ScriptingGetRegisteredContentScriptsFunction& operator=(
      const ScriptingGetRegisteredContentScriptsFunction&) = delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingGetRegisteredContentScriptsFunction() override;
};

class ScriptingUnregisterContentScriptsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.unregisterContentScripts",
                             SCRIPTING_UNREGISTERCONTENTSCRIPTS)

  ScriptingUnregisterContentScriptsFunction();
  ScriptingUnregisterContentScriptsFunction(
      const ScriptingUnregisterContentScriptsFunction&) = delete;
  ScriptingUnregisterContentScriptsFunction& operator=(
      const ScriptingUnregisterContentScriptsFunction&) = delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingUnregisterContentScriptsFunction() override;

  // Called when content scripts have been unregistered.
  void OnContentScriptsUnregistered(const std::optional<std::string>& error);
};

class ScriptingUpdateContentScriptsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("scripting.updateContentScripts",
                             SCRIPTING_UPDATECONTENTSCRIPTS)

  ScriptingUpdateContentScriptsFunction();
  ScriptingUpdateContentScriptsFunction(
      const ScriptingUpdateContentScriptsFunction&) = delete;
  ScriptingUpdateContentScriptsFunction& operator=(
      const ScriptingUpdateContentScriptsFunction&) = delete;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  ~ScriptingUpdateContentScriptsFunction() override;

  // Returns a UserScript object by updating the `original_script` with the
  // `new_script` given delta. If the updated script cannot be parsed, populates
  // `parse_error` and returns nullptr.
  std::unique_ptr<UserScript> ApplyUpdate(
      std::set<std::string>* script_ids_to_persist,
      api::scripting::RegisteredContentScript& new_script,
      api::scripting::RegisteredContentScript& original_script,
      std::u16string* parse_error);

  // Called when script files have been checked.
  void OnContentScriptFilesValidated(
      std::set<std::string> persistent_script_ids,
      scripting::ValidateScriptsResult result);

  // Called when content scripts have been updated.
  void OnContentScriptsUpdated(const std::optional<std::string>& error);
};

}  // namespace extensions

#endif  // ELECTRON_SHELL_BROWSER_EXTENSIONS_API_SCRIPTING_SCRIPTING_API_H_
