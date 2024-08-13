// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/app/electron_crash_reporter_client.h"

#include <map>
#include <string>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/crash/core/common/crash_keys.h"
#include "components/upload_list/crash_upload_list.h"
#include "content/public/common/content_switches.h"
#include "electron/electron_version.h"
#include "shell/common/electron_paths.h"
#include "shell/common/thread_restrictions.h"

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
#include "components/version_info/version_info_values.h"
#endif

#if BUILDFLAG(IS_POSIX)
#include "base/debug/dump_without_crashing.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "base/strings/string_util_win.h"
#endif

namespace {

ElectronCrashReporterClient* Instance() {
  static base::NoDestructor<ElectronCrashReporterClient> crash_client;
  return crash_client.get();
}

}  // namespace

// static
void ElectronCrashReporterClient::Create() {
  crash_reporter::SetCrashReporterClient(Instance());

  // By setting the BREAKPAD_DUMP_LOCATION environment variable, an alternate
  // location to write crash dumps can be set.
  auto env = base::Environment::Create();
  std::string alternate_crash_dump_location;
  base::FilePath crash_dumps_dir_path;
  if (env->GetVar("BREAKPAD_DUMP_LOCATION", &alternate_crash_dump_location)) {
    crash_dumps_dir_path =
        base::FilePath::FromUTF8Unsafe(alternate_crash_dump_location);
  }
  if (!crash_dumps_dir_path.empty()) {
    electron::ScopedAllowBlockingForElectron allow_blocking;
    base::PathService::Override(electron::DIR_CRASH_DUMPS,
                                crash_dumps_dir_path);
  }
}

// static
ElectronCrashReporterClient* ElectronCrashReporterClient::Get() {
  return Instance();
}

void ElectronCrashReporterClient::SetCollectStatsConsent(bool upload_allowed) {
  collect_stats_consent_ = upload_allowed;
}

void ElectronCrashReporterClient::SetUploadUrl(const std::string& url) {
  upload_url_ = url;
}

void ElectronCrashReporterClient::SetShouldRateLimit(bool rate_limit) {
  rate_limit_ = rate_limit;
}

void ElectronCrashReporterClient::SetShouldCompressUploads(bool compress) {
  compress_uploads_ = compress;
}

void ElectronCrashReporterClient::SetGlobalAnnotations(
    const std::map<std::string, std::string>& annotations) {
  global_annotations_ = annotations;
}

ElectronCrashReporterClient::ElectronCrashReporterClient() = default;

ElectronCrashReporterClient::~ElectronCrashReporterClient() = default;

#if BUILDFLAG(IS_LINUX)
void ElectronCrashReporterClient::SetCrashReporterClientIdFromGUID(
    const std::string& client_guid) {
  crash_keys::SetMetricsClientIdFromGUID(client_guid);
}
void ElectronCrashReporterClient::GetProductNameAndVersion(
    const char** product_name,
    const char** version) {
  DCHECK(product_name);
  DCHECK(version);
  *product_name = ELECTRON_PRODUCT_NAME;
  *version = ELECTRON_VERSION_STRING;
}

void ElectronCrashReporterClient::GetProductNameAndVersion(
    std::string* product_name,
    std::string* version,
    std::string* channel) {
  const char* c_product_name;
  const char* c_version;
  GetProductNameAndVersion(&c_product_name, &c_version);
  *product_name = c_product_name;
  *version = c_version;
  *channel = "";
}

base::FilePath ElectronCrashReporterClient::GetReporterLogFilename() {
  return base::FilePath(CrashUploadList::kReporterLogFilename);
}
#endif

#if BUILDFLAG(IS_WIN)
void ElectronCrashReporterClient::GetProductNameAndVersion(
    const std::wstring& exe_path,
    std::wstring* product_name,
    std::wstring* version,
    std::wstring* special_build,
    std::wstring* channel_name) {
  *product_name = base::UTF8ToWide(ELECTRON_PRODUCT_NAME);
  *version = base::UTF8ToWide(ELECTRON_VERSION_STRING);
}
#endif

#if BUILDFLAG(IS_WIN)
bool ElectronCrashReporterClient::GetCrashDumpLocation(
    std::wstring* crash_dir_str) {
  base::FilePath crash_dir;
  if (!base::PathService::Get(electron::DIR_CRASH_DUMPS, &crash_dir))
    return false;
  *crash_dir_str = crash_dir.value();
  return true;
}
#else
bool ElectronCrashReporterClient::GetCrashDumpLocation(
    base::FilePath* crash_dir) {
  bool result = base::PathService::Get(electron::DIR_CRASH_DUMPS, crash_dir);
  {
    // If the DIR_CRASH_DUMPS path is overridden with
    // app.setPath('crashDumps', ...) then the directory might not have been
    // created.
    electron::ScopedAllowBlockingForElectron allow_blocking;
    if (result && !base::PathExists(*crash_dir)) {
      return base::CreateDirectory(*crash_dir);
    }
  }
  return result;
}
#endif

bool ElectronCrashReporterClient::IsRunningUnattended() {
  return !collect_stats_consent_;
}

bool ElectronCrashReporterClient::GetCollectStatsConsent() {
  return collect_stats_consent_;
}

#if BUILDFLAG(IS_MAC)
bool ElectronCrashReporterClient::ReportingIsEnforcedByPolicy(
    bool* breakpad_enabled) {
  return false;
}
#endif

bool ElectronCrashReporterClient::GetShouldRateLimit() {
  return rate_limit_;
}

bool ElectronCrashReporterClient::GetShouldCompressUploads() {
  return compress_uploads_;
}

void ElectronCrashReporterClient::GetProcessSimpleAnnotations(
    std::map<std::string, std::string>* annotations) {
  for (auto&& pair : global_annotations_) {
    (*annotations)[pair.first] = pair.second;
  }
  (*annotations)["prod"] = ELECTRON_PRODUCT_NAME;
  (*annotations)["ver"] = ELECTRON_VERSION_STRING;
}

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
bool ElectronCrashReporterClient::ShouldMonitorCrashHandlerExpensively() {
  return false;
}
#endif

std::string ElectronCrashReporterClient::GetUploadUrl() {
  return upload_url_;
}

bool ElectronCrashReporterClient::EnableBreakpadForProcess(
    const std::string& process_type) {
  return process_type == switches::kRendererProcess ||
         process_type == switches::kZygoteProcess ||
         process_type == switches::kGpuProcess ||
         process_type == switches::kUtilityProcess || process_type == "node";
}
