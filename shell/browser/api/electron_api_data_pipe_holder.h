// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_DATA_PIPE_HOLDER_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_DATA_PIPE_HOLDER_H_

#include <string>

#include "gin/handle.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/data_element.h"
#include "services/network/public/mojom/data_pipe_getter.mojom.h"

namespace electron::api {

// Retains reference to the data pipe.
class DataPipeHolder : public gin::Wrappable<DataPipeHolder> {
 public:
  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  const char* GetTypeName() override;

  static gin::Handle<DataPipeHolder> Create(
      v8::Isolate* isolate,
      const network::DataElement& element);
  static gin::Handle<DataPipeHolder> From(v8::Isolate* isolate,
                                          const std::string& id);

  // Read all data at once.
  //
  // TODO(zcbenz): This is apparently not suitable for really large data, but
  // no one has complained about it yet.
  v8::Local<v8::Promise> ReadAll(v8::Isolate* isolate);

  // The unique ID that can be used to receive the object.
  const std::string& id() const { return id_; }

  // disable copy
  DataPipeHolder(const DataPipeHolder&) = delete;
  DataPipeHolder& operator=(const DataPipeHolder&) = delete;

 private:
  explicit DataPipeHolder(const network::DataElement& element);
  ~DataPipeHolder() override;

  std::string id_;
  mojo::Remote<network::mojom::DataPipeGetter> data_pipe_;
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_DATA_PIPE_HOLDER_H_
