#ifndef SANDBOX_SHARED_H
#define SANDBOX_SHARED_H

#include <string>
#include <list>

struct Global {
  static int debug_mode;
};

struct Context {
  unsigned netns :1;
  unsigned pidns :1;
  unsigned mountns :1;
  unsigned ipcns :1;
  unsigned fs :1;
  unsigned mount_proc :1;
  unsigned clone_for_fuse :1;
  char** child_argv;
  std::string fuse_mountpoint;
  std::list<std::string> fuse_writable_paths;
};

void debug(const char*, ...);

#endif
