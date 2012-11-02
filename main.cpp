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

#include <string>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>


#include "shared.h"
#include "run.h"

#define OPTION_NOT  (1<<16)
#define OPTION_FS_ALLOW 0x101
static const char optionstring[] = "+hd";

#define OPTION_BOOL(longopt, value) \
  { longopt, 0, 0, value }, \
  { "no-" longopt, 0, 0, value|OPTION_NOT }

static const struct option options[] = {
  { "help", 0, 0, 'h' },
  { "debug", 0, 0, 'd' },
  { "none", 0, 0, 'N' },
  { "fs-allow", 1, 0, OPTION_FS_ALLOW },
  OPTION_BOOL("net", 'n'),
  OPTION_BOOL("pid", 'p'),
  OPTION_BOOL("ipc", 'i'),
  OPTION_BOOL("mount", 'm'),
  OPTION_BOOL("fs", 'f'),
  { 0, 0, 0, 0 }
};

void usage(FILE* stream, int exitcode)
{
  fprintf(stream,
"Usage: sandbox [options] [--] command [args]\n\n"
"Run a command in a sandbox.\n\n"
"Options:\n"
"  --help, -h        Show this message\n"
"  --debug, -d       Enable debugging messages\n"
"\n"
"Sandbox features:\n"
"  All of the following sandbox features are enabled by default.\n"
"  `--no-<feature>' disables a feature.\n"
"\n"
"  --none\n"
"        Disable all sandbox features.\n"
"        May be followed by additional options to turn on selected features.\n"
"\n"
"  --net, --no-net\n"
"        Network isolation; prevents connections to any host (including\n"
"        localhost). This includes Unix domain socket connections.\n"
"\n"
"  --pid, --no-pid\n"
"        PID isolation; prevents sending signals to any process outside of\n"
"        the sandbox.\n"
"\n"
"  --mount, --no-mount\n"
"        Mount isolation; prevents mounting or unmounting any mounts outside\n"
"        of the sandbox.\n"
"\n"
"  --ipc, --no-ipc\n"
"        IPC isolation; prevents access to any SysV IPC resources outside of\n"
"        the sandbox.\n"
"\n"
"  --fs, --no-fs\n"
"        Filesystem isolation; prevents modification of files outside of the\n"
"        sandbox.\n"
"\n"
"Filesystem options:\n"
"\n"
"  --fs-allow <PATH> [ --fs-allow <PATH2> ... ]\n"
"        Allow writes to the specified path(s).\n"
"        <PATH> may contain a single relative or absolute path, or\n"
"        several paths separated with the : character.\n"
	  );
  exit(exitcode);
}

std::string realpath(std::string const& path)
{
  char* resolved = realpath(path.c_str(), 0);
  if (!resolved) {
    fprintf(stderr, "Could not resolve %s: %s\n", path.c_str(),
	    strerror(errno));
    exit(4);
  }
  std::string out{resolved};
  free(resolved);
  return out;
}

void parse_fs_allow(Context* ctx, const char* arg)
{
  int backslash = 0;
  std::string current_arg;
  while (*arg) {
    if (backslash) {
      current_arg.append(1, *arg);
      backslash = 0;
    } else if (*arg == '\\') {
      backslash = 1;
    } else if (*arg == ':') {
      ctx->fuse_writable_paths.push_back(realpath(current_arg));
      current_arg.clear();
    } else {
      current_arg.append(1, *arg);
    }
    ++arg;
  }
  ctx->fuse_writable_paths.push_back(realpath(current_arg));
}

void parse_arguments(Context* ctx, int argc, char** argv)
{
  int gotopt;
  while ((gotopt = getopt_long(argc, argv,
			       optionstring, options, 0))) {
    if (gotopt == -1) {
      break;
    }
    
    debug("gotopt 0x%x\n", gotopt);

    int enable = !(gotopt&OPTION_NOT);

    gotopt &= 0xffff;

    switch (gotopt) {

    case 'h':
      usage(stdout, 0);

    case '?':
      usage(stderr, 3);

    case 'd':
      ++Global::debug_mode;
      break;

    case 'N':
      ctx->netns = 0;
      ctx->pidns = 0;
      ctx->mountns = 0;
      ctx->ipcns = 0;
      ctx->fs = 0;
      break;

    case 'n':
      ctx->netns = enable;
      break;

    case 'p':
      ctx->pidns = enable;
      break;

    case 'i':
      ctx->ipcns = enable;
      break;

    case 'm':
      ctx->mountns = enable;
      break;

    case 'f':
      ctx->fs = enable;
      break;

    case OPTION_FS_ALLOW:
      parse_fs_allow(ctx, optarg);
      break;
    }
  }

  debug("optind %d\n", optind);

  ctx->child_argv = &argv[optind];
  if (!ctx->child_argv[0]) {
    fprintf(stderr, "Not enough arguments\n");
    usage(stderr, 3);
  }

  if (ctx->fs && !ctx->mountns) {
    fprintf(stderr, "error: filesystem sandbox requires mount sandbox.\n"
	    "Try adding --mount to the sandbox arguments.\n");
    exit(3);
  }

  ctx->mount_proc = ctx->mountns && ctx->pidns;

  /*
    If using fuse and pidns, create a pid namespace and mount namespace
    for the fuse layer only; this ensures the sandbox fuse process won't
    be visible within the sandbox, and killing the top-level sandbox
    process is guaranteed to kill the fuse process.
  */
  ctx->clone_for_fuse = ctx->fs && ctx->pidns;
}

static char* mountpoint = 0;
void remove_mountpoint()
{
  if (rmdir(mountpoint)) {
    if (errno != ENOENT) {
      fprintf(stderr, "warning: could not remove sandbox fuse mount point %s: "
	      "%s\n", mountpoint, strerror(errno));
    }
  }
}

void setup_fuse_context(Context* ctx)
{
  const char* tempdir = getenv("TMPDIR");
  if (!tempdir) {
    tempdir = "/tmp";
  }

  std::string tmpl(tempdir);
  tmpl += "/sandbox-fuse-XXXXXX";
  char* c_mountpoint = strdup(tmpl.c_str());

  if (!mkdtemp(c_mountpoint)) {
    perror("Can't create mount point for FUSE");
    exit(3);
  }
  ctx->fuse_mountpoint = c_mountpoint;
  mountpoint = c_mountpoint;
  atexit(remove_mountpoint);
}

int main(int argc, char** argv)
{
  Context ctx = {
    .netns = 1,
    .pidns = 1,
    .mountns = 1,
    .ipcns = 1,
    .fs = 1
  };
  parse_arguments(&ctx, argc, argv);
  if (ctx.fs) {
    setup_fuse_context(&ctx);
  }
  return run(&ctx);
}
