// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_NODE_BINDINGS_H_
#define ELECTRON_SHELL_COMMON_NODE_BINDINGS_H_

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/weak_ptr.h"
#include "gin/public/context_holder.h"
#include "gin/public/gin_embedders.h"
#include "uv.h"  // NOLINT(build/include_directory)
#include "v8/include/v8-forward.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace node {
class Environment;
class IsolateData;
class MultiIsolatePlatform;
}  // namespace node

namespace electron {

// A helper class to manage uv_handle_t types, e.g. uv_async_t.
//
// As per the uv docs: "uv_close() MUST be called on each handle before
// memory is released. Moreover, the memory can only be released in
// close_cb or after it has returned." This class encapsulates the work
// needed to follow those requirements.
template <typename T,
          typename std::enable_if<
              // these are the C-style 'subclasses' of uv_handle_t
              std::is_same<T, uv_async_t>::value ||
              std::is_same<T, uv_check_t>::value ||
              std::is_same<T, uv_fs_event_t>::value ||
              std::is_same<T, uv_fs_poll_t>::value ||
              std::is_same<T, uv_idle_t>::value ||
              std::is_same<T, uv_pipe_t>::value ||
              std::is_same<T, uv_poll_t>::value ||
              std::is_same<T, uv_prepare_t>::value ||
              std::is_same<T, uv_process_t>::value ||
              std::is_same<T, uv_signal_t>::value ||
              std::is_same<T, uv_stream_t>::value ||
              std::is_same<T, uv_tcp_t>::value ||
              std::is_same<T, uv_timer_t>::value ||
              std::is_same<T, uv_tty_t>::value ||
              std::is_same<T, uv_udp_t>::value>::type* = nullptr>
class UvHandle {
 public:
  UvHandle() : t_(new T) {}
  ~UvHandle() { reset(); }
  T* get() { return t_; }
  uv_handle_t* handle() { return reinterpret_cast<uv_handle_t*>(t_); }

  void reset() {
    auto* h = handle();
    if (h != nullptr) {
      DCHECK_EQ(0, uv_is_closing(h));
      uv_close(h, OnClosed);
      t_ = nullptr;
    }
  }

 private:
  static void OnClosed(uv_handle_t* handle) {
    delete reinterpret_cast<T*>(handle);
  }

  RAW_PTR_EXCLUSION T* t_ = {};
};

class NodeBindings {
 public:
  enum class BrowserEnvironment { kBrowser, kRenderer, kUtility, kWorker };

  static std::unique_ptr<NodeBindings> Create(BrowserEnvironment browser_env);
  static void RegisterBuiltinBindings();
  static bool IsInitialized();

  virtual ~NodeBindings();

  // Setup V8, libuv.
  void Initialize(v8::Local<v8::Context> context);

  std::vector<std::string> ParseNodeCliFlags();

  // Create the environment and load node.js.
  std::shared_ptr<node::Environment> CreateEnvironment(
      v8::Local<v8::Context> context,
      node::MultiIsolatePlatform* platform,
      std::vector<std::string> args,
      std::vector<std::string> exec_args,
      std::optional<base::RepeatingCallback<void()>> on_app_code_ready =
          std::nullopt);

  std::shared_ptr<node::Environment> CreateEnvironment(
      v8::Local<v8::Context> context,
      node::MultiIsolatePlatform* platform,
      std::optional<base::RepeatingCallback<void()>> on_app_code_ready =
          std::nullopt);

  // Load node.js in the environment.
  void LoadEnvironment(node::Environment* env);

  // Prepare embed thread for message loop integration.
  void PrepareEmbedThread();

  // Notify embed thread to start polling after environment is loaded.
  void StartPolling();

  node::IsolateData* isolate_data(v8::Local<v8::Context> context) const;

  // Gets/sets the environment to wrap uv loop.
  void set_uv_env(node::Environment* env) { uv_env_ = env; }
  node::Environment* uv_env() const { return uv_env_; }

  [[nodiscard]] constexpr uv_loop_t* uv_loop() { return uv_loop_; }

  // disable copy
  NodeBindings(const NodeBindings&) = delete;
  NodeBindings& operator=(const NodeBindings&) = delete;

  // Blocks until app code is signaled to be loaded via |SetAppCodeLoaded|.
  // Only has an effect if called in the browser process
  void JoinAppCode();

 protected:
  explicit NodeBindings(BrowserEnvironment browser_env);

  // Called to poll events in new thread.
  virtual void PollEvents() = 0;

  // Make the main thread run libuv loop.
  void WakeupMainThread();

  // Interrupt the PollEvents.
  void WakeupEmbedThread();

 private:
  static uv_loop_t* InitEventLoop(BrowserEnvironment browser_env,
                                  uv_loop_t* worker_loop);

  // Run the libuv loop for once.
  void UvRunOnce();

  [[nodiscard]] constexpr bool in_worker_loop() const {
    return browser_env_ == BrowserEnvironment::kWorker;
  }

  // Which environment we are running.
  const BrowserEnvironment browser_env_;

  // Loop used when constructed in WORKER mode
  uv_loop_t worker_loop_;

  // Current thread's libuv loop.
  // depends-on: worker_loop_
  const raw_ptr<uv_loop_t> uv_loop_;

  // Current thread's MessageLoop.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  // Choose a reasonable unique index that's higher than any Blink uses
  // and thus unlikely to collide with an existing index.
  static constexpr int kElectronContextEmbedderDataIndex =
      static_cast<int>(gin::kPerContextDataStartIndex) +
      static_cast<int>(gin::kEmbedderElectron);

  // Thread to poll uv events.
  static void EmbedThreadRunner(void* arg);

  // Default callback to indicate when the node environment has finished
  // initializing and the primary import chain is fully resolved and executed
  void SetAppCodeLoaded();

  // Indicates whether polling thread has been created.
  bool initialized_ = false;

  // Indicates whether the app code has finished loading
  // for ESM this is async after the module is loaded
  bool app_code_loaded_ = false;

  // Whether the libuv loop has ended.
  bool embed_closed_ = false;

  // Dummy handle to make uv's loop not quit.
  UvHandle<uv_async_t> dummy_uv_handle_;

  // Thread for polling events.
  uv_thread_t embed_thread_;

  // Semaphore to wait for main loop in the embed thread.
  uv_sem_t embed_sem_;

  // Environment that to wrap the uv loop.
  raw_ptr<node::Environment> uv_env_ = nullptr;

  // Isolate data used in creating the environment
  raw_ptr<node::IsolateData> isolate_data_ = nullptr;

  base::WeakPtrFactory<NodeBindings> weak_factory_{this};
};

// A thread-safe function responsible for loading preload script which runs for
// all node environments (including child processes and workers).
void OnNodePreload(node::Environment* env,
                   v8::Local<v8::Value> process,
                   v8::Local<v8::Value> require);

}  // namespace electron

#endif  // ELECTRON_SHELL_COMMON_NODE_BINDINGS_H_
