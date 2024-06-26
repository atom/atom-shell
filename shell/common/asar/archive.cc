// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/asar/archive.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/pickle.h"
#include "base/values.h"
#include "electron/fuses.h"
#include "shell/common/asar/asar_util.h"
#include "shell/common/asar/scoped_temporary_file.h"
#include "shell/common/thread_restrictions.h"

#if BUILDFLAG(IS_WIN)
#include <io.h>
#endif

namespace asar {

namespace {

#if BUILDFLAG(IS_WIN)
const char kSeparators[] = "\\/";
#else
const char kSeparators[] = "/";
#endif

const base::Value::Dict* GetNodeFromPath(std::string path,
                                         const base::Value::Dict& root);

// Gets the "files" from "dir".
const base::Value::Dict* GetFilesNode(const base::Value::Dict& root,
                                      const base::Value::Dict& dir) {
  // Test for symbol linked directory.
  const std::string* link = dir.FindString("link");
  if (link != nullptr) {
    const base::Value::Dict* linked_node = GetNodeFromPath(*link, root);
    if (!linked_node)
      return nullptr;
    return linked_node->FindDict("files");
  }

  return dir.FindDict("files");
}

// Gets sub-file "name" from "dir".
const base::Value::Dict* GetChildNode(const base::Value::Dict& root,
                                      const std::string& name,
                                      const base::Value::Dict& dir) {
  if (name.empty())
    return &root;

  const base::Value::Dict* files = GetFilesNode(root, dir);
  return files ? files->FindDict(name) : nullptr;
}

// Gets the node of "path" from "root".
const base::Value::Dict* GetNodeFromPath(std::string path,
                                         const base::Value::Dict& root) {
  if (path.empty())
    return &root;

  const base::Value::Dict* dir = &root;
  for (size_t delimiter_position = path.find_first_of(kSeparators);
       delimiter_position != std::string::npos;
       delimiter_position = path.find_first_of(kSeparators)) {
    const base::Value::Dict* child =
        GetChildNode(root, path.substr(0, delimiter_position), *dir);
    if (!child)
      return nullptr;

    dir = child;
    path.erase(0, delimiter_position + 1);
  }

  return GetChildNode(root, path, *dir);
}

bool FillFileInfoWithNode(Archive::FileInfo* info,
                          uint32_t header_size,
                          bool load_integrity,
                          const base::Value::Dict* node) {
  if (std::optional<int> size = node->FindInt("size")) {
    info->size = static_cast<uint32_t>(*size);
  } else {
    return false;
  }

  if (std::optional<bool> unpacked = node->FindBool("unpacked")) {
    info->unpacked = *unpacked;
    if (info->unpacked) {
      return true;
    }
  }

  const std::string* offset = node->FindString("offset");
  if (offset &&
      base::StringToUint64(std::string_view{*offset}, &info->offset)) {
    info->offset += header_size;
  } else {
    return false;
  }

  if (std::optional<bool> executable = node->FindBool("executable")) {
    info->executable = *executable;
  }

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  if (load_integrity &&
      electron::fuses::IsEmbeddedAsarIntegrityValidationEnabled()) {
    if (const base::Value::Dict* integrity = node->FindDict("integrity")) {
      const std::string* algorithm = integrity->FindString("algorithm");
      const std::string* hash = integrity->FindString("hash");
      std::optional<int> block_size = integrity->FindInt("blockSize");
      const base::Value::List* blocks = integrity->FindList("blocks");

      if (algorithm && hash && block_size && block_size > 0 && blocks) {
        IntegrityPayload integrity_payload;
        integrity_payload.hash = *hash;
        integrity_payload.block_size =
            static_cast<uint32_t>(block_size.value());
        for (auto& value : *blocks) {
          if (const std::string* block = value.GetIfString()) {
            integrity_payload.blocks.push_back(*block);
          } else {
            LOG(FATAL)
                << "Invalid block integrity value for file in ASAR archive";
          }
        }
        if (*algorithm == "SHA256") {
          integrity_payload.algorithm = HashAlgorithm::kSHA256;
          info->integrity = std::move(integrity_payload);
        }
      }
    }

    if (!info->integrity.has_value()) {
      LOG(FATAL) << "Failed to read integrity for file in ASAR archive";
    }
  }
#endif

  return true;
}

}  // namespace

IntegrityPayload::IntegrityPayload()
    : algorithm(HashAlgorithm::kNone), block_size(0) {}
IntegrityPayload::~IntegrityPayload() = default;
IntegrityPayload::IntegrityPayload(const IntegrityPayload& other) = default;

Archive::FileInfo::FileInfo()
    : unpacked(false), executable(false), size(0), offset(0) {}
Archive::FileInfo::~FileInfo() = default;

Archive::Archive(const base::FilePath& path)
    : initialized_(false), path_(path), file_(base::File::FILE_OK) {
  electron::ScopedAllowBlockingForElectron allow_blocking;
  file_.Initialize(path_, base::File::FLAG_OPEN | base::File::FLAG_READ);
#if BUILDFLAG(IS_WIN)
  fd_ = _open_osfhandle(reinterpret_cast<intptr_t>(file_.GetPlatformFile()), 0);
#elif BUILDFLAG(IS_POSIX)
  fd_ = file_.GetPlatformFile();
#endif
}

Archive::~Archive() {
#if BUILDFLAG(IS_WIN)
  if (fd_ != -1) {
    _close(fd_);
    // Don't close the handle since we already closed the fd.
    file_.TakePlatformFile();
  }
#endif
  electron::ScopedAllowBlockingForElectron allow_blocking;
  file_.Close();
}

bool Archive::Init() {
  // Should only be initialized once
  CHECK(!initialized_);
  initialized_ = true;

  if (!file_.IsValid()) {
    if (file_.error_details() != base::File::FILE_ERROR_NOT_FOUND) {
      LOG(WARNING) << "Opening " << path_.value() << ": "
                   << base::File::ErrorToString(file_.error_details());
    }
    return false;
  }

  std::vector<char> buf;
  int len;

  buf.resize(8);
  {
    electron::ScopedAllowBlockingForElectron allow_blocking;
    len = file_.ReadAtCurrentPos(buf.data(), buf.size());
  }
  if (len != static_cast<int>(buf.size())) {
    PLOG(ERROR) << "Failed to read header size from " << path_.value();
    return false;
  }

  uint32_t size;
  if (!base::PickleIterator(base::Pickle::WithData(base::as_byte_span(buf)))
           .ReadUInt32(&size)) {
    LOG(ERROR) << "Failed to parse header size from " << path_.value();
    return false;
  }

  buf.resize(size);
  {
    electron::ScopedAllowBlockingForElectron allow_blocking;
    len = file_.ReadAtCurrentPos(buf.data(), buf.size());
  }
  if (len != static_cast<int>(buf.size())) {
    PLOG(ERROR) << "Failed to read header from " << path_.value();
    return false;
  }

  std::string header;
  if (!base::PickleIterator(base::Pickle::WithData(base::as_byte_span(buf)))
           .ReadString(&header)) {
    LOG(ERROR) << "Failed to parse header from " << path_.value();
    return false;
  }

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  // Validate header signature if required and possible
  if (electron::fuses::IsEmbeddedAsarIntegrityValidationEnabled() &&
      RelativePath().has_value()) {
    std::optional<IntegrityPayload> integrity = HeaderIntegrity();
    if (!integrity.has_value()) {
      LOG(FATAL) << "Failed to get integrity for validatable asar archive: "
                 << RelativePath().value();
    }

    // Currently we only support the sha256 algorithm, we can add support for
    // more below ensure we read them in preference order from most secure to
    // least
    if (integrity.value().algorithm != HashAlgorithm::kNone) {
      ValidateIntegrityOrDie(header.c_str(), header.length(),
                             integrity.value());
    } else {
      LOG(FATAL) << "No eligible hash for validatable asar archive: "
                 << RelativePath().value();
    }

    header_validated_ = true;
  }
#endif

  std::optional<base::Value> value = base::JSONReader::Read(header);
  if (!value || !value->is_dict()) {
    LOG(ERROR) << "Failed to parse header";
    return false;
  }

  header_size_ = 8 + size;
  header_ = std::move(*value).TakeDict();
  return true;
}

#if !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_WIN)
std::optional<IntegrityPayload> Archive::HeaderIntegrity() const {
  return std::nullopt;
}

std::optional<base::FilePath> Archive::RelativePath() const {
  return std::nullopt;
}
#endif

bool Archive::GetFileInfo(const base::FilePath& path, FileInfo* info) const {
  if (!header_)
    return false;

  const base::Value::Dict* node =
      GetNodeFromPath(path.AsUTF8Unsafe(), *header_);
  if (!node)
    return false;

  const std::string* link = node->FindString("link");
  if (link)
    return GetFileInfo(base::FilePath::FromUTF8Unsafe(*link), info);

  return FillFileInfoWithNode(info, header_size_, header_validated_, node);
}

bool Archive::Stat(const base::FilePath& path, Stats* stats) const {
  if (!header_)
    return false;

  const base::Value::Dict* node =
      GetNodeFromPath(path.AsUTF8Unsafe(), *header_);
  if (!node)
    return false;

  if (node->Find("link")) {
    stats->type = FileType::kLink;
    return true;
  }

  if (node->Find("files")) {
    stats->type = FileType::kDirectory;
    return true;
  }

  return FillFileInfoWithNode(stats, header_size_, header_validated_, node);
}

bool Archive::Readdir(const base::FilePath& path,
                      std::vector<base::FilePath>* files) const {
  if (!header_)
    return false;

  const base::Value::Dict* node =
      GetNodeFromPath(path.AsUTF8Unsafe(), *header_);
  if (!node)
    return false;

  const base::Value::Dict* files_node = GetFilesNode(*header_, *node);
  if (!files_node)
    return false;

  for (const auto iter : *files_node)
    files->push_back(base::FilePath::FromUTF8Unsafe(iter.first));
  return true;
}

bool Archive::Realpath(const base::FilePath& path,
                       base::FilePath* realpath) const {
  if (!header_)
    return false;

  const base::Value::Dict* node =
      GetNodeFromPath(path.AsUTF8Unsafe(), *header_);
  if (!node)
    return false;

  const std::string* link = node->FindString("link");
  if (link) {
    *realpath = base::FilePath::FromUTF8Unsafe(*link);
    return true;
  }

  *realpath = path;
  return true;
}

bool Archive::CopyFileOut(const base::FilePath& path, base::FilePath* out) {
  if (!header_)
    return false;

  base::AutoLock auto_lock(external_files_lock_);

  auto it = external_files_.find(path.value());
  if (it != external_files_.end()) {
    *out = it->second->path();
    return true;
  }

  FileInfo info;
  if (!GetFileInfo(path, &info))
    return false;

  if (info.unpacked) {
    *out = path_.AddExtension(FILE_PATH_LITERAL("unpacked")).Append(path);
    return true;
  }

  auto temp_file = std::make_unique<ScopedTemporaryFile>();
  base::FilePath::StringType ext = path.Extension();
  if (!temp_file->InitFromFile(&file_, ext, info.offset, info.size,
                               info.integrity))
    return false;

#if BUILDFLAG(IS_POSIX)
  if (info.executable) {
    // chmod a+x temp_file;
    base::SetPosixFilePermissions(temp_file->path(), 0755);
  }
#endif

  *out = temp_file->path();
  external_files_[path.value()] = std::move(temp_file);
  return true;
}

int Archive::GetUnsafeFD() const {
  return fd_;
}

}  // namespace asar
