// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_data_pipe_holder.h"

#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "net/base/net_errors.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/key_weak_map.h"

#include "shell/common/node_includes.h"

namespace electron::api {

namespace {

// Incremental ID.
int g_next_id = 0;

// Map that manages all the DataPipeHolder objects.
KeyWeakMap<std::string>& AllDataPipeHolders() {
  static base::NoDestructor<KeyWeakMap<std::string>> weak_map;
  return *weak_map.get();
}

// Utility class to read from data pipe.
class DataPipeReader {
 public:
  DataPipeReader(gin_helper::Promise<v8::Local<v8::Value>> promise,
                 mojo::Remote<network::mojom::DataPipeGetter> data_pipe_getter)
      : promise_(std::move(promise)),
        data_pipe_getter_(std::move(data_pipe_getter)),
        handle_watcher_(FROM_HERE,
                        mojo::SimpleWatcher::ArmingPolicy::MANUAL,
                        base::SequencedTaskRunner::GetCurrentDefault()) {
    // Get a new data pipe and start.
    mojo::ScopedDataPipeProducerHandle producer_handle;
    CHECK_EQ(mojo::CreateDataPipe(nullptr, producer_handle, data_pipe_),
             MOJO_RESULT_OK);
    data_pipe_getter_->Read(std::move(producer_handle),
                            base::BindOnce(&DataPipeReader::ReadCallback,
                                           weak_factory_.GetWeakPtr()));
    handle_watcher_.Watch(data_pipe_.get(), MOJO_HANDLE_SIGNAL_READABLE,
                          base::BindRepeating(&DataPipeReader::OnHandleReadable,
                                              weak_factory_.GetWeakPtr()));
  }

  ~DataPipeReader() = default;

  // disable copy
  DataPipeReader(const DataPipeReader&) = delete;
  DataPipeReader& operator=(const DataPipeReader&) = delete;

 private:
  // Callback invoked by DataPipeGetter::Read.
  void ReadCallback(int32_t status, uint64_t size) {
    if (status != net::OK) {
      OnFailure();
      return;
    }
    buffer_.resize(size);
    head_ = &buffer_.front();
    remaining_size_ = size;
    handle_watcher_.ArmOrNotify();
  }

  // Called by |handle_watcher_| when data is available or the pipe was closed,
  // and there's a pending Read() call.
  void OnHandleReadable(MojoResult result) {
    if (result != MOJO_RESULT_OK) {
      OnFailure();
      return;
    }

    // Read.
    size_t length = remaining_size_;
    result = data_pipe_->ReadData(head_, &length, MOJO_READ_DATA_FLAG_NONE);
    if (result == MOJO_RESULT_OK) {  // success
      remaining_size_ -= length;
      head_ += length;
      if (remaining_size_ == 0) {
        OnSuccess();
      } else {
        handle_watcher_.ArmOrNotify();
      }
    } else if (result == MOJO_RESULT_SHOULD_WAIT) {  // IO pending
      handle_watcher_.ArmOrNotify();
    } else {  // error
      OnFailure();
    }
  }

  void OnFailure() {
    promise_.RejectWithErrorMessage("Could not get blob data");
    delete this;
  }

  void OnSuccess() {
    // Copy the buffer to JS.
    // TODO(nornagon): make this zero-copy by allocating the array buffer
    // inside the sandbox
    v8::HandleScope handle_scope(promise_.isolate());
    v8::Local<v8::Value> buffer =
        node::Buffer::Copy(promise_.isolate(), &buffer_.front(), buffer_.size())
            .ToLocalChecked();
    promise_.Resolve(buffer);

    // Destroy data pipe.
    handle_watcher_.Cancel();
    delete this;
  }

  gin_helper::Promise<v8::Local<v8::Value>> promise_;

  mojo::Remote<network::mojom::DataPipeGetter> data_pipe_getter_;
  mojo::ScopedDataPipeConsumerHandle data_pipe_;
  mojo::SimpleWatcher handle_watcher_;

  // Stores read data.
  std::vector<char> buffer_;

  // The head of buffer.
  raw_ptr<char, AllowPtrArithmetic> head_ = nullptr;

  // Remaining data to read.
  uint64_t remaining_size_ = 0;

  base::WeakPtrFactory<DataPipeReader> weak_factory_{this};
};

}  // namespace

gin::WrapperInfo DataPipeHolder::kWrapperInfo = {gin::kEmbedderNativeGin};

DataPipeHolder::DataPipeHolder(const network::DataElement& element)
    : id_(base::NumberToString(++g_next_id)) {
  data_pipe_.Bind(
      element.As<network::DataElementDataPipe>().CloneDataPipeGetter());
}

DataPipeHolder::~DataPipeHolder() = default;

v8::Local<v8::Promise> DataPipeHolder::ReadAll(v8::Isolate* isolate) {
  gin_helper::Promise<v8::Local<v8::Value>> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();
  if (!data_pipe_) {
    promise.RejectWithErrorMessage("Could not get blob data");
    return handle;
  }

  new DataPipeReader(std::move(promise), std::move(data_pipe_));
  return handle;
}

const char* DataPipeHolder::GetTypeName() {
  return "DataPipeHolder";
}

// static
gin::Handle<DataPipeHolder> DataPipeHolder::Create(
    v8::Isolate* isolate,
    const network::DataElement& element) {
  auto handle = gin::CreateHandle(isolate, new DataPipeHolder(element));
  AllDataPipeHolders().Set(isolate, handle->id(),
                           handle->GetWrapper(isolate).ToLocalChecked());
  return handle;
}

// static
gin::Handle<DataPipeHolder> DataPipeHolder::From(v8::Isolate* isolate,
                                                 const std::string& id) {
  v8::MaybeLocal<v8::Object> object = AllDataPipeHolders().Get(isolate, id);
  if (!object.IsEmpty()) {
    gin::Handle<DataPipeHolder> handle;
    if (gin::ConvertFromV8(isolate, object.ToLocalChecked(), &handle))
      return handle;
  }
  return gin::Handle<DataPipeHolder>();
}

}  // namespace electron::api
