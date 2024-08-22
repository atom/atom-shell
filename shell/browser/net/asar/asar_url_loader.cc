// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/net/asar/asar_url_loader.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <string_view>
#include "base/task/thread_pool.h"
#include "content/public/browser/file_url_loader.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "mojo/public/cpp/system/file_data_source.h"
#include "net/base/filename_util.h"
#include "net/base/mime_sniffer.h"
#include "net/base/mime_util.h"
#include "net/http/http_byte_range.h"
#include "net/http/http_util.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "shell/browser/net/asar/asar_file_validator.h"
#include "shell/common/asar/archive.h"
#include "shell/common/asar/asar_util.h"

namespace asar {

namespace {

net::Error ConvertMojoResultToNetError(MojoResult result) {
  switch (result) {
    case MOJO_RESULT_OK:
      return net::OK;
    case MOJO_RESULT_NOT_FOUND:
      return net::ERR_FILE_NOT_FOUND;
    case MOJO_RESULT_PERMISSION_DENIED:
      return net::ERR_ACCESS_DENIED;
    case MOJO_RESULT_RESOURCE_EXHAUSTED:
      return net::ERR_INSUFFICIENT_RESOURCES;
    case MOJO_RESULT_ABORTED:
      return net::ERR_ABORTED;
    default:
      return net::ERR_FAILED;
  }
}

constexpr size_t kDefaultFileUrlPipeSize = 65536;

// Because this makes things simpler.
static_assert(kDefaultFileUrlPipeSize >= net::kMaxBytesToSniff,
              "Default file data pipe size must be at least as large as a MIME-"
              "type sniffing buffer.");

// Modified from the |FileURLLoader| in |file_url_loader_factory.cc|, to serve
// asar files instead of normal files.
class AsarURLLoader : public network::mojom::URLLoader {
 public:
  static void CreateAndStart(
      const network::ResourceRequest& request,
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      scoped_refptr<net::HttpResponseHeaders> extra_response_headers) {
    // Owns itself. Will live as long as its URLLoader and URLLoaderClientPtr
    // bindings are alive - essentially until either the client gives up or all
    // file data has been sent to it.
    auto* asar_url_loader = new AsarURLLoader;
    asar_url_loader->Start(request, std::move(loader), std::move(client),
                           std::move(extra_response_headers));
  }

  // network::mojom::URLLoader:
  void FollowRedirect(
      const std::vector<std::string>& removed_headers,
      const net::HttpRequestHeaders& modified_headers,
      const net::HttpRequestHeaders& modified_cors_exempt_headers,
      const std::optional<GURL>& new_url) override {}
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {}
  void PauseReadingBodyFromNet() override {}
  void ResumeReadingBodyFromNet() override {}

  // disable copy
  AsarURLLoader(const AsarURLLoader&) = delete;
  AsarURLLoader& operator=(const AsarURLLoader&) = delete;

 private:
  AsarURLLoader() = default;
  ~AsarURLLoader() override = default;

  void Start(const network::ResourceRequest& request,
             mojo::PendingReceiver<network::mojom::URLLoader> loader,
             mojo::PendingRemote<network::mojom::URLLoaderClient> client,
             scoped_refptr<net::HttpResponseHeaders> extra_response_headers) {
    auto head = network::mojom::URLResponseHead::New();
    head->request_start = base::TimeTicks::Now();
    head->response_start = base::TimeTicks::Now();
    head->headers = extra_response_headers;

    base::FilePath path;
    if (!net::FileURLToFilePath(request.url, &path)) {
      mojo::Remote<network::mojom::URLLoaderClient> client_remote(
          std::move(client));
      client_remote->OnComplete(
          network::URLLoaderCompletionStatus(net::ERR_FAILED));
      MaybeDeleteSelf();
      return;
    }

    // Determine whether it is an asar file.
    base::FilePath asar_path, relative_path;
    if (!GetAsarArchivePath(path, &asar_path, &relative_path)) {
      content::CreateFileURLLoaderBypassingSecurityChecks(
          request, std::move(loader), std::move(client), nullptr, false,
          extra_response_headers);
      MaybeDeleteSelf();
      return;
    }

    client_.Bind(std::move(client));
    receiver_.Bind(std::move(loader));
    receiver_.set_disconnect_handler(base::BindOnce(
        &AsarURLLoader::OnConnectionError, base::Unretained(this)));

    // Parse asar archive.
    std::shared_ptr<Archive> archive = GetOrCreateAsarArchive(asar_path);
    Archive::FileInfo info;
    if (!archive || !archive->GetFileInfo(relative_path, &info)) {
      OnClientComplete(net::ERR_FILE_NOT_FOUND);
      return;
    }
    bool is_verifying_file = info.integrity.has_value();

    // For unpacked path, read like normal file.
    base::FilePath real_path;
    if (info.unpacked) {
      archive->CopyFileOut(relative_path, &real_path);
      info.offset = 0;
    }

    mojo::ScopedDataPipeProducerHandle producer_handle;
    mojo::ScopedDataPipeConsumerHandle consumer_handle;
    if (mojo::CreateDataPipe(kDefaultFileUrlPipeSize, producer_handle,
                             consumer_handle) != MOJO_RESULT_OK) {
      OnClientComplete(net::ERR_FAILED);
      return;
    }

    // Note that while the |Archive| already opens a |base::File|, we still need
    // to create a new |base::File| here, as it might be accessed by multiple
    // requests at the same time.
    base::File file(info.unpacked ? real_path : archive->path(),
                    base::File::FLAG_OPEN | base::File::FLAG_READ);
    auto file_data_source =
        std::make_unique<mojo::FileDataSource>(file.Duplicate());
    std::unique_ptr<mojo::DataPipeProducer::DataSource> readable_data_source;
    mojo::FileDataSource* file_data_source_raw = file_data_source.get();
    AsarFileValidator* file_validator_raw = nullptr;
    uint32_t block_size = 0;
    if (info.integrity.has_value()) {
      block_size = info.integrity.value().block_size;
      auto asar_validator = std::make_unique<AsarFileValidator>(
          std::move(info.integrity.value()), std::move(file));
      file_validator_raw = asar_validator.get();
      readable_data_source = std::make_unique<mojo::FilteredDataSource>(
          std::move(file_data_source), std::move(asar_validator));
    } else {
      readable_data_source = std::move(file_data_source);
    }

    std::vector<char> initial_read_buffer(
        std::min(static_cast<uint32_t>(net::kMaxBytesToSniff), info.size));
    auto read_result = readable_data_source.get()->Read(
        info.offset, base::span<char>(initial_read_buffer));
    if (read_result.result != MOJO_RESULT_OK) {
      OnClientComplete(ConvertMojoResultToNetError(read_result.result));
      return;
    }

    auto range_header =
        request.headers.GetHeader(net::HttpRequestHeaders::kRange);
    net::HttpByteRange byte_range;
    if (range_header) {
      // Handle a simple Range header for a single range.
      std::vector<net::HttpByteRange> ranges;
      bool fail = false;
      if (net::HttpUtil::ParseRangeHeader(range_header.value(), &ranges) &&
          ranges.size() == 1) {
        byte_range = ranges[0];
        if (!byte_range.ComputeBounds(info.size))
          fail = true;
      } else {
        fail = true;
      }

      if (fail) {
        OnClientComplete(net::ERR_REQUEST_RANGE_NOT_SATISFIABLE);
        return;
      }
    }

    uint64_t first_byte_to_send = 0;
    uint64_t total_bytes_dropped_from_head = initial_read_buffer.size();
    uint64_t total_bytes_to_send = info.size;

    if (byte_range.IsValid()) {
      first_byte_to_send = byte_range.first_byte_position();
      total_bytes_to_send =
          byte_range.last_byte_position() - first_byte_to_send + 1;
    }

    total_bytes_written_ = total_bytes_to_send;

    head->content_length = base::saturated_cast<int64_t>(total_bytes_to_send);

    if (first_byte_to_send < read_result.bytes_read) {
      // Write any data we read for MIME sniffing, constraining by range where
      // applicable. This will always fit in the pipe (see assertion near
      // |kDefaultFileUrlPipeSize| definition).
      const size_t write_size = std::min(
          (read_result.bytes_read - first_byte_to_send), total_bytes_to_send);
      base::span<const uint8_t> bytes =
          base::as_byte_span(initial_read_buffer)
              .subspan(first_byte_to_send, write_size);
      size_t bytes_written = 0;
      MojoResult result = producer_handle->WriteData(
          bytes, MOJO_WRITE_DATA_FLAG_NONE, bytes_written);
      if (result != MOJO_RESULT_OK || write_size != bytes_written) {
        OnFileWritten(result);
        return;
      }

      // Discount the bytes we just sent from the total range.
      first_byte_to_send = read_result.bytes_read;
      total_bytes_to_send -= write_size;
    } else if (is_verifying_file &&
               first_byte_to_send >= static_cast<uint64_t>(block_size)) {
      // If validation is active and the range of bytes the request wants starts
      // beyond the first block we need to read the next 4MB-1KB to validate
      // that block. Then we can skip ahead to the target block in the SetRange
      // call below If we hit this case it is assumed that none of the data read
      // will be needed by the producer
      uint64_t bytes_to_drop = block_size - net::kMaxBytesToSniff;
      total_bytes_dropped_from_head += bytes_to_drop;
      std::vector<char> abandoned_buffer(bytes_to_drop);
      auto abandon_read_result =
          readable_data_source.get()->Read(info.offset + net::kMaxBytesToSniff,
                                           base::span<char>(abandoned_buffer));
      if (abandon_read_result.result != MOJO_RESULT_OK) {
        OnClientComplete(
            ConvertMojoResultToNetError(abandon_read_result.result));
        return;
      }
    }

    if (!net::GetMimeTypeFromFile(path, &head->mime_type)) {
      std::string new_type;
      net::SniffMimeType(
          std::string_view(initial_read_buffer.data(), read_result.bytes_read),
          request.url, head->mime_type,
          net::ForceSniffFileUrlsForHtml::kDisabled, &new_type);
      head->mime_type.assign(new_type);
      head->did_mime_sniff = true;
    }
    if (head->headers) {
      head->headers->AddHeader(net::HttpRequestHeaders::kContentType,
                               head->mime_type);
    }
    client_->OnReceiveResponse(std::move(head), std::move(consumer_handle),
                               std::nullopt);

    if (total_bytes_to_send == 0) {
      // There's definitely no more data, so we're already done.
      // We provide the range data to the file validator so that
      // it can validate the tiny amount of data we did send
      if (file_validator_raw)
        file_validator_raw->SetRange(info.offset + first_byte_to_send,
                                     total_bytes_dropped_from_head,
                                     info.offset + info.size);
      OnFileWritten(MOJO_RESULT_OK);
      return;
    }

    if (is_verifying_file) {
      int start_block = first_byte_to_send / block_size;

      // If we're starting from the first block, we might not be starting from
      // where we sniffed. We might be a few KB into a file so we need to read
      // the data in the middle so it gets hashed.
      //
      // If we're starting from a later block we might be starting half-way
      // through the block regardless of what was sniffed.  We need to read the
      // data from the start of our initial block up to the start of our actual
      // read point so it gets hashed.
      uint64_t bytes_to_drop =
          start_block == 0 ? first_byte_to_send - net::kMaxBytesToSniff
                           : first_byte_to_send - (start_block * block_size);
      if (file_validator_raw)
        file_validator_raw->SetCurrentBlock(start_block);

      if (bytes_to_drop > 0) {
        uint64_t dropped_bytes_offset =
            info.offset + (start_block * block_size);
        if (start_block == 0)
          dropped_bytes_offset += net::kMaxBytesToSniff;
        total_bytes_dropped_from_head += bytes_to_drop;
        std::vector<char> abandoned_buffer(bytes_to_drop);
        auto abandon_read_result = readable_data_source.get()->Read(
            dropped_bytes_offset, base::span<char>(abandoned_buffer));
        if (abandon_read_result.result != MOJO_RESULT_OK) {
          OnClientComplete(
              ConvertMojoResultToNetError(abandon_read_result.result));
          return;
        }
      }
    }

    // In case of a range request, seek to the appropriate position before
    // sending the remaining bytes asynchronously. Under normal conditions
    // (i.e., no range request) this Seek is effectively a no-op.
    //
    // Note that in Electron we also need to add file offset.
    file_data_source_raw->SetRange(
        first_byte_to_send + info.offset,
        first_byte_to_send + info.offset + total_bytes_to_send);
    if (file_validator_raw)
      file_validator_raw->SetRange(info.offset + first_byte_to_send,
                                   total_bytes_dropped_from_head,
                                   info.offset + info.size);

    data_producer_ =
        std::make_unique<mojo::DataPipeProducer>(std::move(producer_handle));
    data_producer_->Write(
        std::move(readable_data_source),
        base::BindOnce(&AsarURLLoader::OnFileWritten, base::Unretained(this)));
  }

  void OnConnectionError() {
    receiver_.reset();
    MaybeDeleteSelf();
  }

  void OnClientComplete(net::Error net_error) {
    client_->OnComplete(network::URLLoaderCompletionStatus(net_error));
    client_.reset();
    MaybeDeleteSelf();
  }

  void MaybeDeleteSelf() {
    if (!receiver_.is_bound() && !client_.is_bound())
      delete this;
  }

  void OnFileWritten(MojoResult result) {
    // All the data has been written now. Close the data pipe. The consumer will
    // be notified that there will be no more data to read from now.
    data_producer_.reset();

    if (result == MOJO_RESULT_OK) {
      network::URLLoaderCompletionStatus status(net::OK);
      status.encoded_data_length = total_bytes_written_;
      status.encoded_body_length = total_bytes_written_;
      status.decoded_body_length = total_bytes_written_;
      client_->OnComplete(status);
    } else {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_FAILED));
    }
    client_.reset();
    MaybeDeleteSelf();
  }

  std::unique_ptr<mojo::DataPipeProducer> data_producer_;
  mojo::Receiver<network::mojom::URLLoader> receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> client_;

  // In case of successful loads, this holds the total number of bytes written
  // to the response (this may be smaller than the total size of the file when
  // a byte range was requested).
  // It is used to set some of the URLLoaderCompletionStatus data passed back
  // to the URLLoaderClients (eg SimpleURLLoader).
  size_t total_bytes_written_ = 0;
};

}  // namespace

void CreateAsarURLLoader(
    const network::ResourceRequest& request,
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    scoped_refptr<net::HttpResponseHeaders> extra_response_headers) {
  auto task_runner = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN});
  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&AsarURLLoader::CreateAndStart, request, std::move(loader),
                     std::move(client), std::move(extra_response_headers)));
}

}  // namespace asar
