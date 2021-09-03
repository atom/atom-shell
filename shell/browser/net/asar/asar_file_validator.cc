// Copyright (c) 2021 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/net/asar/asar_file_validator.h"

#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace asar {

AsarFileValidator::AsarFileValidator(absl::optional<IntegrityPayload> integrity,
                                     base::File file)
    : should_validate_(integrity.has_value()),
      file_(std::move(file)),
      integrity_(std::move(integrity)) {
  current_block_ = 0;
  max_block_ = integrity_.value().blocks.size() - 1;
}

void AsarFileValidator::OnRead(base::span<char> buffer,
                               mojo::FileDataSource::ReadResult* result) {
  if (!should_validate_)
    return;

  DCHECK(!done_reading_);

  uint64_t buffer_size = result->bytes_read;
  DCHECK(buffer_size >= 0);

  // Compute how many bytes we should hash, and add them to the current hash.
  int block_size = integrity_.value().block_size;
  uint64_t bytes_added = 0;
  while (bytes_added < buffer_size) {
    if (current_block_ > max_block_) {
      LOG(FATAL)
          << "Unexpected number of blocks while validating ASAR file stream";
      return;
    }

    // Create a hash if we don't have one yet
    if (!current_hash_) {
      current_hash_byte_count_ = 0;
      switch (integrity_.value().algorithm) {
        case HashAlgorithm::SHA256:
          current_hash_ =
              crypto::SecureHash::Create(crypto::SecureHash::SHA256);
          break;
        default:
          NOTREACHED();
          break;
      }
    }

    // Compute how many bytes we should hash, and add them to the current hash.
    int bytes_to_hash = std::min(block_size - current_hash_byte_count_,
                                 buffer_size - bytes_added);
    DCHECK_GT(bytes_to_hash, 0);
    current_hash_->Update(buffer.data() + bytes_added, bytes_to_hash);
    bytes_added += bytes_to_hash;
    current_hash_byte_count_ += bytes_to_hash;
    total_hash_byte_count_ += bytes_to_hash;

    if (current_hash_byte_count_ == static_cast<uint64_t>(block_size) &&
        !FinishBlock()) {
      LOG(FATAL) << "Failed to validate block while streaming ASAR file: "
                 << current_block_ - 1;
      return;
    }
  }
}

bool AsarFileValidator::FinishBlock() {
  if (current_hash_byte_count_ == 0) {
    if (!done_reading_ || current_block_ > max_block_) {
      return true;
    }
  }

  if (!current_hash_) {
    // This happens when we fail to read the resource. Compute empty content's
    // hash in this case.
    current_hash_ = crypto::SecureHash::Create(crypto::SecureHash::SHA256);
  }

  uint8_t actual[crypto::kSHA256Length];

  // If the file reader is done we need to make sure we've either read up to the
  // end of the file (the check below) or up to the end of a block_size byte
  // boundary. If the below check fails we compute the next block boundary, how
  // many bytes are needed to get there and then we manually read those bytes
  // from our own file handle ensuring the data producer is unaware but we can
  // validate the hash still.
  if (done_reading_ &&
      total_hash_byte_count_ - extra_read_ != read_max_ - read_start_) {
    int block_size = integrity_.value().block_size;
    uint64_t bytes_needed = std::min(
        block_size - current_hash_byte_count_,
        read_max_ - read_start_ - total_hash_byte_count_ + extra_read_);
    uint64_t offset = read_start_ + total_hash_byte_count_ - extra_read_;
    std::vector<uint8_t> abandoned_buffer(bytes_needed);
    if (!file_.ReadAndCheck(offset, abandoned_buffer)) {
      LOG(FATAL) << "Failed to read required portion of streamed ASAR archive";
      return false;
    }

    current_hash_->Update(&abandoned_buffer.front(), bytes_needed);
  }

  current_hash_->Finish(actual, sizeof(actual));
  current_hash_.reset();
  current_hash_byte_count_ = 0;

  int block = current_block_++;

  const std::string expected_hash = integrity_.value().blocks[block];
  const std::string actual_hex_hash =
      base::ToLowerASCII(base::HexEncode(actual, sizeof(actual)));
  if (expected_hash != actual_hex_hash) {
    return false;
  }

  return true;
}

void AsarFileValidator::OnDone() {
  if (!should_validate_)
    return;

  DCHECK(!done_reading_);
  done_reading_ = true;
  if (!FinishBlock()) {
    LOG(FATAL) << "Failed to validate block while ending ASAR file stream: "
               << current_block_ - 1;
  }
}

void AsarFileValidator::SetRange(uint64_t read_start,
                                 uint64_t extra_read,
                                 uint64_t read_max) {
  read_start_ = read_start;
  extra_read_ = extra_read;
  read_max_ = read_max;
}

void AsarFileValidator::SetCurrentBlock(int current_block) {
  current_block_ = current_block;
}

}  // namespace asar
