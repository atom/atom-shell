// Copyright (c) 2021 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/logging.h"

#include <string>
#include <string_view>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/common/content_switches.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#include "base/win/scoped_handle.h"
#include "base/win/win_util.h"
#include "sandbox/policy/switches.h"
#endif

namespace logging {

constexpr std::string_view kLogFileName{"ELECTRON_LOG_FILE"};
constexpr std::string_view kElectronEnableLogging{"ELECTRON_ENABLE_LOGGING"};

#if BUILDFLAG(IS_WIN)
base::win::ScopedHandle GetLogInheritedHandle(
    const base::CommandLine& command_line) {
  auto handle_str = command_line.GetSwitchValueNative(::switches::kLogFile);
  uint32_t handle_value = 0;
  if (!base::StringToUint(handle_str, &handle_value)) {
    return base::win::ScopedHandle();
  }
  // Duplicate the handle from the command line so that different things can
  // init logging. This means the handle from the parent is never closed, but
  // there will only be one of these in the process.
  HANDLE log_handle = nullptr;
  if (!::DuplicateHandle(::GetCurrentProcess(),
                         base::win::Uint32ToHandle(handle_value),
                         ::GetCurrentProcess(), &log_handle, 0,
                         /*bInheritHandle=*/FALSE, DUPLICATE_SAME_ACCESS)) {
    return base::win::ScopedHandle();
  }
  // Transfer ownership to the caller.
  return base::win::ScopedHandle(log_handle);
}
#endif

base::FilePath GetLogFileName(const base::CommandLine& command_line) {
  std::string filename = command_line.GetSwitchValueASCII(switches::kLogFile);
  if (filename.empty())
    base::Environment::Create()->GetVar(kLogFileName, &filename);
  if (!filename.empty())
    return base::FilePath::FromUTF8Unsafe(filename);

  const base::FilePath log_filename(FILE_PATH_LITERAL("electron_debug.log"));
  base::FilePath log_path;

  if (base::PathService::Get(chrome::DIR_LOGS, &log_path)) {
    log_path = log_path.Append(log_filename);
    return log_path;
  } else {
    // error with path service, just use some default file somewhere
    return log_filename;
  }
}

namespace {

bool HasExplicitLogFile(const base::CommandLine& command_line) {
  std::string filename = command_line.GetSwitchValueASCII(switches::kLogFile);
  if (filename.empty())
    base::Environment::Create()->GetVar(kLogFileName, &filename);
  return !filename.empty();
}

LoggingDestination DetermineLoggingDestination(
    const base::CommandLine& command_line,
    bool is_preinit,
    bool& filename_is_handle) {
  filename_is_handle = false;

  bool enable_logging = false;
  std::string logging_destination;
  if (command_line.HasSwitch(::switches::kEnableLogging)) {
    enable_logging = true;
    logging_destination =
        command_line.GetSwitchValueASCII(switches::kEnableLogging);
  } else {
    auto env = base::Environment::Create();
    if (env->HasVar(kElectronEnableLogging)) {
      enable_logging = true;
      env->GetVar(kElectronEnableLogging, &logging_destination);
    }
  }
  if (!enable_logging)
    return LOG_NONE;

  bool also_log_to_stderr = false;
#if !defined(NDEBUG)
  std::string also_log_to_stderr_str;
  if (base::Environment::Create()->GetVar("ELECTRON_ALSO_LOG_TO_STDERR",
                                          &also_log_to_stderr_str) &&
      !also_log_to_stderr_str.empty())
    also_log_to_stderr = true;
#endif

#if BUILDFLAG(IS_WIN)
  if (logging_destination == "handle" &&
      command_line.HasSwitch(::switches::kProcessType) &&
      command_line.HasSwitch(::switches::kLogFile)) {
    // Child processes can log to a handle duplicated from the parent, and
    // provided in the log-file switch value.
    filename_is_handle = true;
    return LOG_TO_FILE | (also_log_to_stderr ? LOG_TO_STDERR : 0);
  }
#endif  // BUILDFLAG(IS_WIN)

  // --enable-logging logs to stderr, --enable-logging=file logs to a file.
  // NB. this differs from Chromium, in which --enable-logging logs to a file
  // and --enable-logging=stderr logs to stderr, because that's how Electron
  // used to work, so in order to not break anyone who was depending on
  // --enable-logging logging to stderr, we preserve the old behavior by
  // default.
  // If --log-file or ELECTRON_LOG_FILE is specified along with
  // --enable-logging, return LOG_TO_FILE.
  // If we're in the pre-init phase, before JS has run, we want to avoid
  // logging to the default log file, which is inside the user data directory,
  // because we aren't able to accurately determine the user data directory
  // before JS runs. Instead, log to stderr unless there's an explicit filename
  // given.
  if (HasExplicitLogFile(command_line) ||
      (logging_destination == "file" && !is_preinit))
    return LOG_TO_FILE | (also_log_to_stderr ? LOG_TO_STDERR : 0);
  return LOG_TO_SYSTEM_DEBUG_LOG | LOG_TO_STDERR;
}

}  // namespace

void InitElectronLogging(const base::CommandLine& command_line,
                         bool is_preinit) {
  const std::string process_type =
      command_line.GetSwitchValueASCII(::switches::kProcessType);
  bool filename_is_handle = false;
  LoggingDestination logging_dest =
      DetermineLoggingDestination(command_line, is_preinit, filename_is_handle);
  LogLockingState log_locking_state = LOCK_LOG_FILE;
  base::FilePath log_path;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle log_handle;
#endif

  if (command_line.HasSwitch(::switches::kLoggingLevel) &&
      GetMinLogLevel() >= 0) {
    std::string log_level =
        command_line.GetSwitchValueASCII(::switches::kLoggingLevel);
    int level = 0;
    if (base::StringToInt(log_level, &level) && level >= 0 &&
        level < LOGGING_NUM_SEVERITIES) {
      SetMinLogLevel(level);
    } else {
      DLOG(WARNING) << "Bad log level: " << log_level;
    }
  }

  // Don't resolve the log path unless we need to. Otherwise we leave an open
  // ALPC handle after sandbox lockdown on Windows.
  if ((logging_dest & LOG_TO_FILE) != 0) {
    if (filename_is_handle) {
#if BUILDFLAG(IS_WIN)
      // Child processes on Windows are provided a file handle if logging is
      // enabled as sandboxed processes cannot open files.
      log_handle = GetLogInheritedHandle(command_line);
      if (!log_handle.is_valid()) {
        LOG(ERROR) << "Unable to initialize logging from handle.";
        return;
      }
#endif
    } else {
      log_path = GetLogFileName(command_line);
    }
  } else {
    log_locking_state = DONT_LOCK_LOG_FILE;
  }

  // On Windows, having non canonical forward slashes in log file name causes
  // problems with sandbox filters, see https://crbug.com/859676
  log_path = log_path.NormalizePathSeparators();

  LoggingSettings settings;
  settings.logging_dest = logging_dest;
  settings.log_file_path = log_path.value().c_str();
#if BUILDFLAG(IS_WIN)
  // Avoid initializing with INVALID_HANDLE_VALUE.
  // This handle is owned by the logging framework and is closed when the
  // process exits.
  // TODO(crbug.com/328285906) Use a ScopedHandle in logging settings.
  settings.log_file = log_handle.is_valid() ? log_handle.release() : nullptr;
#endif
  settings.lock_log = log_locking_state;
  // If we're logging to an explicit file passed with --log-file, we don't want
  // to delete the log file on our second initialization.
  settings.delete_old =
      process_type.empty() && (is_preinit || !HasExplicitLogFile(command_line))
          ? DELETE_OLD_LOG_FILE
          : APPEND_TO_OLD_LOG_FILE;
  bool success = InitLogging(settings);
  if (!success) {
    PLOG(ERROR) << "Failed to init logging";
  }

  SetLogItems(true /* pid */, false, true /* timestamp */, false);
}

}  // namespace logging
