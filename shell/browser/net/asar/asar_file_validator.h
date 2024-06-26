// Copyright (c) 2021 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_NET_ASAR_ASAR_FILE_VALIDATOR_H_
#define ELECTRON_SHELL_BROWSER_NET_ASAR_ASAR_FILE_VALIDATOR_H_

#include <algorithm>
#include <memory>
#include <optional>

#include "crypto/secure_hash.h"
#include "mojo/public/cpp/system/file_data_source.h"
#include "mojo/public/cpp/system/filtered_data_source.h"
#include "shell/common/asar/archive.h"

namespace asar {

class AsarFileValidator : public mojo::FilteredDataSource::Filter {
 public:
  AsarFileValidator(IntegrityPayload integrity, base::File file);
  ~AsarFileValidator() override;

  // disable copy
  AsarFileValidator(const AsarFileValidator&) = delete;
  AsarFileValidator& operator=(const AsarFileValidator&) = delete;

  // mojo::FilteredDataSource::Filter
  void OnRead(base::span<char> buffer,
              mojo::FileDataSource::ReadResult* result) override;

  void OnDone() override;

  void SetRange(uint64_t read_start, uint64_t extra_read, uint64_t read_max);
  void SetCurrentBlock(int current_block);

 protected:
  bool FinishBlock();

 private:
  base::File file_;
  IntegrityPayload integrity_;

  // The offset in the file_ that the underlying file reader is starting at
  uint64_t read_start_ = 0;
  // The number of bytes this DataSourceFilter will have seen that aren't used
  // by the DataProducer.  These extra bytes are exclusively for hash validation
  // but we need to know how many we've used so we know when we're done.
  uint64_t extra_read_ = 0;
  // The maximum offset in the file_ that we should read to, used to determine
  // which bytes we're missing or if we need to read up to a block boundary in
  // OnDone
  uint64_t read_max_ = 0;
  bool done_reading_ = false;
  int current_block_;
  int max_block_;
  uint64_t current_hash_byte_count_ = 0;
  uint64_t total_hash_byte_count_ = 0;
  std::unique_ptr<crypto::SecureHash> current_hash_;
};

}  // namespace asar

#endif  // ELECTRON_SHELL_BROWSER_NET_ASAR_ASAR_FILE_VALIDATOR_H_
