// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Most code came from: chrome/browser/chrome_browser_main_posix.cc.

#include "base/notreached.h"
#include "shell/browser/electron_browser_main_parts.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include "base/debug/leak_annotations.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/platform_thread.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "shell/browser/browser.h"

namespace electron {

namespace {

// See comment in |PreEarlyInitialization()|, where sigaction is called.
void SIGCHLDHandler(int signal) {}

// The OSX fork() implementation can crash in the child process before
// fork() returns.  In that case, the shutdown pipe will still be
// shared with the parent process.  To prevent child crashes from
// causing parent shutdowns, |g_pipe_pid| is the pid for the process
// which registered |g_shutdown_pipe_write_fd|.
// See <http://crbug.com/175341>.
pid_t g_pipe_pid = -1;
int g_shutdown_pipe_write_fd = -1;
int g_shutdown_pipe_read_fd = -1;

// Common code between SIG{HUP, INT, TERM}Handler.
void GracefulShutdownHandler(int signal) {
  // Reinstall the default handler.  We had one shot at graceful shutdown.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_DFL;
  RAW_CHECK(sigaction(signal, &action, nullptr) == 0);

  RAW_CHECK(g_pipe_pid == getpid());
  RAW_CHECK(g_shutdown_pipe_write_fd != -1);
  RAW_CHECK(g_shutdown_pipe_read_fd != -1);
  size_t bytes_written = 0;
  do {
    int rv = HANDLE_EINTR(
        write(g_shutdown_pipe_write_fd,
              reinterpret_cast<const char*>(&signal) + bytes_written,
              sizeof(signal) - bytes_written));
    RAW_CHECK(rv >= 0);
    bytes_written += rv;
  } while (bytes_written < sizeof(signal));
}

// See comment in |PostCreateMainMessageLoop()|, where sigaction is called.
void SIGHUPHandler(int signal) {
  RAW_CHECK(signal == SIGHUP);
  GracefulShutdownHandler(signal);
}

// See comment in |PostCreateMainMessageLoop()|, where sigaction is called.
void SIGINTHandler(int signal) {
  RAW_CHECK(signal == SIGINT);
  GracefulShutdownHandler(signal);
}

// See comment in |PostCreateMainMessageLoop()|, where sigaction is called.
void SIGTERMHandler(int signal) {
  RAW_CHECK(signal == SIGTERM);
  GracefulShutdownHandler(signal);
}

class ShutdownDetector : public base::PlatformThread::Delegate {
 public:
  explicit ShutdownDetector(
      int shutdown_fd,
      base::OnceCallback<void()> shutdown_callback,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);

  // disable copy
  ShutdownDetector(const ShutdownDetector&) = delete;
  ShutdownDetector& operator=(const ShutdownDetector&) = delete;

  // base::PlatformThread::Delegate:
  void ThreadMain() override;

 private:
  const int shutdown_fd_;
  base::OnceCallback<void()> shutdown_callback_;
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
};

ShutdownDetector::ShutdownDetector(
    int shutdown_fd,
    base::OnceCallback<void()> shutdown_callback,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : shutdown_fd_(shutdown_fd),
      shutdown_callback_(std::move(shutdown_callback)),
      task_runner_(task_runner) {
  CHECK_NE(shutdown_fd_, -1);
  CHECK(!shutdown_callback_.is_null());
  CHECK(task_runner_);
}

// These functions are used to help us diagnose crash dumps that happen
// during the shutdown process.
NOINLINE void ShutdownFDReadError() {
  // Ensure function isn't optimized away.
  asm("");
  sleep(UINT_MAX);
}

NOINLINE void ShutdownFDClosedError() {
  // Ensure function isn't optimized away.
  asm("");
  sleep(UINT_MAX);
}

NOINLINE void ExitPosted() {
  // Ensure function isn't optimized away.
  asm("");
  sleep(UINT_MAX);
}

void ShutdownDetector::ThreadMain() {
  base::PlatformThread::SetName("CrShutdownDetector");

  int signal;
  size_t bytes_read = 0;
  do {
    const ssize_t ret = HANDLE_EINTR(
        read(shutdown_fd_, reinterpret_cast<char*>(&signal) + bytes_read,
             sizeof(signal) - bytes_read));
    if (ret < 0) {
      NOTREACHED_IN_MIGRATION() << "Unexpected error: " << strerror(errno);
      ShutdownFDReadError();
      break;
    } else if (ret == 0) {
      NOTREACHED_IN_MIGRATION() << "Unexpected closure of shutdown pipe.";
      ShutdownFDClosedError();
      break;
    }
    bytes_read += ret;
  } while (bytes_read < sizeof(signal));
  VLOG(1) << "Handling shutdown for signal " << signal << ".";

  if (!task_runner_->PostTask(FROM_HERE,
                              base::BindOnce(std::move(shutdown_callback_)))) {
    // Without a valid task runner to post the exit task to, there aren't many
    // options. Raise the signal again. The default handler will pick it up
    // and cause an ungraceful exit.
    RAW_LOG(WARNING, "No valid task runner, exiting ungracefully.");
    kill(getpid(), signal);

    // The signal may be handled on another thread.  Give that a chance to
    // happen.
    sleep(3);

    // We really should be dead by now.  For whatever reason, we're not. Exit
    // immediately, with the exit status set to the signal number with bit 8
    // set.  On the systems that we care about, this exit status is what is
    // normally used to indicate an exit by this signal's default handler.
    // This mechanism isn't a de jure standard, but even in the worst case, it
    // should at least result in an immediate exit.
    RAW_LOG(WARNING, "Still here, exiting really ungracefully.");
    _exit(signal | (1 << 7));
  }
  ExitPosted();
}

}  // namespace

void ElectronBrowserMainParts::HandleSIGCHLD() {
  // We need to accept SIGCHLD, even though our handler is a no-op because
  // otherwise we cannot wait on children. (According to POSIX 2001.)
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIGCHLDHandler;
  CHECK_EQ(sigaction(SIGCHLD, &action, nullptr), 0);
}

void ElectronBrowserMainParts::InstallShutdownSignalHandlers(
    base::OnceCallback<void()> shutdown_callback,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner) {
  int pipefd[2];
  int ret = pipe(pipefd);
  if (ret < 0) {
    PLOG(DFATAL) << "Failed to create pipe";
    return;
  }
  g_pipe_pid = getpid();
  g_shutdown_pipe_read_fd = pipefd[0];
  g_shutdown_pipe_write_fd = pipefd[1];
#if !defined(ADDRESS_SANITIZER)
  const size_t kShutdownDetectorThreadStackSize = PTHREAD_STACK_MIN * 2;
#else
  // ASan instrumentation bloats the stack frames, so we need to increase the
  // stack size to avoid hitting the guard page.
  const size_t kShutdownDetectorThreadStackSize = PTHREAD_STACK_MIN * 4;
#endif
  ShutdownDetector* detector = new ShutdownDetector(
      g_shutdown_pipe_read_fd, std::move(shutdown_callback), task_runner);

  // PlatformThread does not delete its delegate.
  ANNOTATE_LEAKING_OBJECT_PTR(detector);
  if (!base::PlatformThread::CreateNonJoinable(kShutdownDetectorThreadStackSize,
                                               detector)) {
    LOG(DFATAL) << "Failed to create shutdown detector task.";
  }
  // Setup signal handlers for shutdown AFTER shutdown pipe is setup because
  // it may be called right away after handler is set.

  // If adding to this list of signal handlers, note the new signal probably
  // needs to be reset in child processes. See
  // base/process_util_posix.cc:LaunchProcess.

  // We need to handle SIGTERM, because that is how many POSIX-based distros
  // ask processes to quit gracefully at shutdown time.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIGTERMHandler;
  CHECK_EQ(sigaction(SIGTERM, &action, nullptr), 0);

  // Also handle SIGINT - when the user terminates the browser via Ctrl+C. If
  // the browser process is being debugged, GDB will catch the SIGINT first.
  action.sa_handler = SIGINTHandler;
  CHECK_EQ(sigaction(SIGINT, &action, nullptr), 0);

  // And SIGHUP, for when the terminal disappears. On shutdown, many Linux
  // distros send SIGHUP, SIGTERM, and then SIGKILL.
  action.sa_handler = SIGHUPHandler;
  CHECK_EQ(sigaction(SIGHUP, &action, nullptr), 0);
}

}  // namespace electron
