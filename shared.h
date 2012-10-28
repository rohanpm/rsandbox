#ifndef SANDBOX_SHARED_H
#define SANDBOX_SHARED_H

struct Global {
  int debug_mode;
};
extern struct Global global;

struct Context {
  unsigned netns :1;
  unsigned pidns :1;
  unsigned mountns :1;
  unsigned ipcns :1;
  unsigned fs :1;
  unsigned mount_proc :1;
  unsigned clone_for_fuse :1;
  char** child_argv;
  const char* fuse_mountpoint;
  const char* fuse_writable_paths[8];
};

void debug(const char*, ...);

#endif
