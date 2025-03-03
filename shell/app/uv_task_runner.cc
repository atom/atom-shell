// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/app/uv_task_runner.h"

#include <utility>

#include "base/location.h"
#include "base/time/time.h"

namespace electron {

UvTaskRunner::UvTaskRunner(uv_loop_t* loop) : loop_{loop} {
  uv_async_init(loop_, post_tasks_.get(), [](uv_async_t* handle) {
    reinterpret_cast<UvTaskRunner*>(handle->data)->PostTasks();
  });
  post_tasks_->data = this;
  uv_unref(reinterpret_cast<uv_handle_t*>(post_tasks_.get()));
}

UvTaskRunner::~UvTaskRunner() = default;

void UvTaskRunner::Shutdown() {
  post_tasks_.reset();
}

bool UvTaskRunner::PostDelayedTask(const base::Location& /*from_here*/,
                                   base::OnceClosure closure,
                                   base::TimeDelta delay) {
  if (!post_tasks_.get())
    return false;

  unposted_tasks_lock_.Acquire();
  unposted_tasks_.emplace_front(std::move(closure), delay);
  unposted_tasks_lock_.Release();

  uv_async_send(post_tasks_.get());

  return true;
}

void UvTaskRunner::PostTasks() {
  auto on_timeout = [](uv_timer_t* timer) {
    auto& tasks = static_cast<UvTaskRunner*>(timer->data)->posted_tasks_;
    if (auto iter = tasks.find(timer); iter != tasks.end())
      std::move(tasks.extract(iter).mapped()).Run();
  };

  unposted_tasks_lock_.Acquire();
  decltype(unposted_tasks_) tasks;
  std::swap(tasks, unposted_tasks_);
  unposted_tasks_lock_.Release();

  for (auto& [closure, delay] : tasks) {
    UvHandle<uv_timer_t> timer;
    uv_timer_init(loop_, timer.get());
    timer->data = this;
    uv_timer_start(timer.get(), on_timeout, delay.InMilliseconds(), 0);
    posted_tasks_.try_emplace(std::move(timer), std::move(closure));
  }
}

bool UvTaskRunner::RunsTasksInCurrentSequence() const {
  return true;
}

bool UvTaskRunner::PostNonNestableDelayedTask(const base::Location& from_here,
                                              base::OnceClosure closure,
                                              base::TimeDelta delay) {
  return PostDelayedTask(from_here, std::move(closure), delay);
}

}  // namespace electron
