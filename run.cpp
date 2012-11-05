/*
  Copyright (c) 2012 Rohan McGovern <rohan@mcgovern.id.au>

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "run.h"
#include "fuse_sandbox.h"

#include <list>
#include <string>
#include <utility>

#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <mntent.h>
#include <unistd.h>

int run_children(void*);

/* remount all mounted filesystems of the given type */
int remount(const char* type)
{
  debug("attempting to remount filesystems of type %s\n", type);

  FILE* file = setmntent("/proc/mounts", "r");
  if (!file) {
    perror("setmntent /proc/mounts");
    return -1;
  }

  struct mntent* ent = getmntent(file);
  if (!ent) {
    perror("getmntent");
    return -1;
  }
  debug("entering loop\n");

  std::list<std::string> to_mount;

  while (ent) {
    debug("looking at filesystem %s %s\n", ent->mnt_dir, ent->mnt_type);
    if (0 == strcmp(ent->mnt_type, type)) {
      debug("remount %s (%s) ...\n", ent->mnt_dir, type);
      to_mount.push_back(ent->mnt_dir);
    }
    ent = getmntent(file);
  }
  endmntent(file);

  for (auto mountpoint : to_mount) {
    if (mount(0, mountpoint.c_str(), type, 0, 0)) {
      fprintf(stderr, "remount %s: %s\n", mountpoint.c_str(), strerror(errno));
      return -1;
    }
  }

  return 0;
}

int exec_child(void* arg)
{
  const Context* ctx = reinterpret_cast<const Context*>(arg);

  /* FIXME: don't hardcode the proc and devtmpfs stuff */
  if (ctx->fs) {
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
      perror("getcwd");
      return 255;
    }
    if (chroot(ctx->fuse_mountpoint.c_str())) {
      perror("chroot");
      return 255;
    }
    if (chdir(cwd)) {
      perror("chdir");
    }
    if (remount("devtmpfs")) {
      return 255;
    }
  }

  if (ctx->mount_proc) {
    debug("mounting /proc\n");
    if (remount("proc")) {
      return 255;
    }
  }

  char** argv = (char**)ctx->child_argv;
  execvp(argv[0], argv);
  perror("execvp");
  return 255;
}

int do_nothing(void* arg)
{
  return 0;
}

#define test_clone(flags, kernel_config) \
  test_clone_real(flags, #flags, kernel_config)

int test_clone_real(int flags, const char* flagstr, const char* kernel_config)
{
  char stack[4096];
  int pid = clone(do_nothing, stack+sizeof(stack), SIGCHLD|flags, 0);
  if (pid == -1) {
    if (errno == EPERM) {
      fprintf(stderr, "error: permission denied for clone() with %s.\n"
	      "rsandbox should have CAP_SYS_ADMIN capability set.\n", flagstr);
    } else if (errno == EINVAL) {
      fprintf(stderr, "error: your kernel does not support clone() with %s.\n"
	      "%s%s%s",
	      flagstr,
              kernel_config ? "Kernel should be configured with " : "",
	      kernel_config,
	      kernel_config ? ".\n" : "");
    } else {
      fprintf(stderr, "error: clone() with %s failed: %s\n", flagstr,
	      strerror(errno));
    }
    return -errno;
  }
  waitpid(pid, 0, 0);
  return 0;
}

int run(const Context* ctx)
{
  int status;
  if (ctx->clone_for_fuse) {
    if (test_clone(CLONE_NEWNS, 0)
	|| test_clone(CLONE_NEWPID, "CONFIG_PID_NS"))
    {
      fprintf(stderr, "Could not initialize filesystem sandbox; aborting.\n");
      return 255;
    }
    char stack[16384];
    int tid = clone(run_children, stack+sizeof(stack),
		    SIGCHLD|CLONE_NEWNS|CLONE_NEWPID, (void*)ctx);
    if (tid == -1) {
      perror("clone");
      return 255;
    }
    if (-1 == waitpid(tid, &status, 0)) {
      perror("waitpid");
      return 255;
    }
  } else {
    status = run_children((void*)ctx);
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 255;
}

int run_children(void* arg)
{
  const Context* ctx = reinterpret_cast<const struct Context*>(arg);
  int clone_flags = SIGCHLD;
  if (ctx->netns) {
    if (test_clone(CLONE_NEWNET, "CONFIG_NET_NS")) {
      fprintf(stderr, "Could not initialize network sandbox; aborting.\n");
      return 255;
    }
    clone_flags |= CLONE_NEWNET;
    debug("Using CLONE_NEWNET\n");
  }
  if (ctx->pidns) {
    if (!ctx->clone_for_fuse && test_clone(CLONE_NEWPID, "CONFIG_PID_NS")) {
      fprintf(stderr, "Could not initialize process sandbox; aborting.\n");
      return 255;
    }
    clone_flags |= CLONE_NEWPID;
    debug("Using CLONE_NEWPID\n");
  }
  if (ctx->mountns) {
    if (ctx->clone_for_fuse) {
      debug("Not using CLONE_NEWS; already cloned for FUSE\n");
    } else {
      if (test_clone(CLONE_NEWNS, 0)) {
	fprintf(stderr, "Could not initialize mounts sandbox; aborting.\n");
	return 255;
      }
      clone_flags |= CLONE_NEWNS;
      debug("Using CLONE_NEWNS\n");
    }
  }
  if (ctx->ipcns) {
    if (test_clone(CLONE_NEWIPC, "CONFIG_SYSVIPC and CONFIG_IPC_NS")) {
      fprintf(stderr, "Could not initialize IPC sandbox; aborting.\n");
      return 255;
    }
    clone_flags |= CLONE_NEWIPC;
    debug("Using CLONE_NEWIPC\n");
  }

  int fuse_pid = 0;
  if (ctx->fs) {
    fuse_pid = start_fuse_sandbox(ctx);
    if (fuse_pid == -1) {
      fprintf(stderr, "Could not initialize FUSE; aborting.\n");
      return 255;
    }
    debug("fuse PID: %d\n", fuse_pid);
  }

  char stack[16384];
  int tid = clone(exec_child, stack+sizeof(stack), clone_flags, (void*)ctx);
  if (tid == -1) {
    perror("clone");
    return 255;
  }

  debug("child: %d\n", tid);

  int status;
  int waited = waitpid(tid, &status, 0);
  if (waited == -1) {
    perror("waitpid");
  }

  debug("waited: %d, status: 0x%x\n", waited, status);

  int fuse_status = 0;
  if (fuse_pid) {
    kill(fuse_pid, SIGTERM);
    int fuse_waited = waitpid(fuse_pid, &fuse_status, 0);
    if (fuse_waited == -1) {
      perror("fuse waitpid");
    }
  }

  if (fuse_status) {
    fprintf(stderr, "rsandbox: warning: fuse process exited with status 0x%x\n",
	    fuse_status);
  }

  return status;
}
