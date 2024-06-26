// Copyright (c) 2022 Microsoft, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_API_ELECTRON_API_UTILITY_PROCESS_H_
#define ELECTRON_SHELL_BROWSER_API_ELECTRON_API_UTILITY_PROCESS_H_

#include <map>
#include <memory>
#include <string>

#include "base/containers/id_map.h"
#include "base/environment.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "content/public/browser/service_process_host.h"
#include "gin/wrappable.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "shell/browser/event_emitter_mixin.h"
#include "shell/common/gin_helper/pinnable.h"
#include "shell/services/node/public/mojom/node_service.mojom.h"
#include "v8/include/v8.h"

namespace gin {
class Arguments;
template <typename T>
class Handle;
}  // namespace gin

namespace base {
class Process;
}  // namespace base

namespace electron::api {

class UtilityProcessWrapper
    : public gin::Wrappable<UtilityProcessWrapper>,
      public gin_helper::Pinnable<UtilityProcessWrapper>,
      public gin_helper::EventEmitterMixin<UtilityProcessWrapper>,
      public mojo::MessageReceiver,
      public content::ServiceProcessHost::Observer {
 public:
  enum class IOHandle : size_t { STDIN = 0, STDOUT = 1, STDERR = 2 };
  enum class IOType { IO_PIPE, IO_INHERIT, IO_IGNORE };

  ~UtilityProcessWrapper() override;
  static gin::Handle<UtilityProcessWrapper> Create(gin::Arguments* args);
  static raw_ptr<UtilityProcessWrapper> FromProcessId(base::ProcessId pid);

  void Shutdown(uint64_t exit_code);

  // gin::Wrappable
  static gin::WrapperInfo kWrapperInfo;
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override;
  const char* GetTypeName() override;

 private:
  UtilityProcessWrapper(node::mojom::NodeServiceParamsPtr params,
                        std::u16string display_name,
                        std::map<IOHandle, IOType> stdio,
                        base::EnvironmentMap env_map,
                        base::FilePath current_working_directory,
                        bool use_plugin_helper);
  void OnServiceProcessLaunch(const base::Process& process);
  void CloseConnectorPort();

  void HandleTermination(uint64_t exit_code);

  void PostMessage(gin::Arguments* args);
  bool Kill() const;
  v8::Local<v8::Value> GetOSProcessId(v8::Isolate* isolate) const;

  // mojo::MessageReceiver
  bool Accept(mojo::Message* mojo_message) override;

  // content::ServiceProcessHost::Observer
  void OnServiceProcessTerminatedNormally(
      const content::ServiceProcessInfo& info) override;
  void OnServiceProcessCrashed(
      const content::ServiceProcessInfo& info) override;

  void OnServiceProcessDisconnected(uint32_t exit_code,
                                    const std::string& description);

  base::ProcessId pid_ = base::kNullProcessId;
#if BUILDFLAG(IS_WIN)
  // Non-owning handles, these will be closed when the
  // corresponding FD are closed via _close.
  HANDLE stdout_read_handle_;
  HANDLE stderr_read_handle_;
#endif
  int stdout_read_fd_ = -1;
  int stderr_read_fd_ = -1;
  bool connector_closed_ = false;
  std::unique_ptr<mojo::Connector> connector_;
  blink::MessagePortDescriptor host_port_;
  mojo::Remote<node::mojom::NodeService> node_service_remote_;
  base::WeakPtrFactory<UtilityProcessWrapper> weak_factory_{this};
};

}  // namespace electron::api

#endif  // ELECTRON_SHELL_BROWSER_API_ELECTRON_API_UTILITY_PROCESS_H_
