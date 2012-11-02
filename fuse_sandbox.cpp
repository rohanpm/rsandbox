#define FUSE_USE_VERSION 26
#define _BSD_SOURCE
#define _XOPEN_SOURCE 500

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fuse/fuse.h>
#include <assert.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/xattr.h>

#include <list>

#include "shared.h"
#include "fuse_sandbox.h"
#include "path.h"

static Path mountpoint;
static std::list<Path> writable_paths;

/* returns 1 if path should be hidden in the sandbox */
int should_hide(const char* path)
{
  return (0 == strncmp(path, mountpoint.path().c_str(), mountpoint.length()));
}

/* returns 1 if write access under the given path should not be blocked */
int permit_write(const char* path)
{
  for (Path tree : writable_paths) {
    if (0 == strncmp(path, tree.path().c_str(), tree.length())
	&& strlen(path) > tree.length()) {
      return 1;
    }
  }
  return 0;
}

#define CHECK_READ(path)			\
  do {						\
    if (should_hide(path)) {			\
      return -ENOENT;				\
    }						\
  } while(0)

#define CHECK_READWRITE(path)			\
  do {						\
    if (should_hide(path)) {			\
      return -ENOENT;				\
    }						\
    if (!permit_write(path)) {			\
      return -EACCES;				\
    }						\
  } while(0)

#define PROXY(...)				\
  ({						\
    int result = __VA_ARGS__;			\
    if (-1 == result) {				\
      result = -errno;				\
    }						\
    result;					\
  })

int sandbox_access(const char* path, int mode)
{
  CHECK_READ(path);
  return PROXY(access(path, mode));
}

int sandbox_mknod(const char* path, mode_t mode, dev_t dev)
{
  CHECK_READWRITE(path);
  return PROXY(mknod(path, mode, dev));
}

int sandbox_readlink(const char* path, char* buf, size_t size)
{
  CHECK_READ(path);
  int result = readlink(path, buf, size-1);
  if (-1 == result) {
    return -errno;
  }
  buf[result] = 0;
  return 0;
}

int sandbox_getattr(const char* path, struct stat* statbuf)
{
  CHECK_READ(path);
  return PROXY(lstat(path, statbuf));
}

/* things which need access control */
int sandbox_unlink(const char* path)
{
  CHECK_READWRITE(path);
  return PROXY(unlink(path));
}

int sandbox_mkdir(const char* path, mode_t mode)
{
  CHECK_READWRITE(path);
  return PROXY(mkdir(path, mode));
}

int sandbox_rmdir(const char* path)
{
  CHECK_READWRITE(path);
  return PROXY(rmdir(path));
}

int sandbox_symlink(const char* oldpath, const char* newpath)
{
  CHECK_READWRITE(newpath);
  return PROXY(symlink(oldpath, newpath));
}

int sandbox_rename(const char* oldpath, const char* newpath)
{
  CHECK_READWRITE(oldpath);
  CHECK_READWRITE(newpath);
  return PROXY(rename(oldpath, newpath));
}

int sandbox_link(const char* oldpath, const char* newpath)
{
  CHECK_READWRITE(newpath);
  return PROXY(link(oldpath, newpath));
}

int sandbox_chmod(const char* path, mode_t mode)
{
  CHECK_READWRITE(path);
  return PROXY(chmod(path, mode));
}

int sandbox_chown(const char* path, uid_t uid, gid_t gid)
{
  CHECK_READWRITE(path);
  return PROXY(chown(path, uid, gid));
}

int sandbox_truncate(const char* path, off_t off)
{
  CHECK_READWRITE(path);
  return PROXY(truncate(path, off));
}

int sandbox_open(const char* path, struct fuse_file_info* fi)
{
  int flags = fi->flags;
  if (flags&O_WRONLY || flags&O_RDWR || flags&O_TRUNC) {
    CHECK_READWRITE(path);
  } else {
    CHECK_READ(path);
  }
  
  int fd = open(path, fi->flags);
  if (-1 == fd) {
    return -errno;
  }
  close(fd);
  return 0;
}

int sandbox_read(const char* path, char* buf, size_t size, off_t off,
		 struct fuse_file_info* fi)
{
  CHECK_READ(path);

  /* NOTE: requires direct_io mounting */
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    return -errno;
  }

  ssize_t out = pread(fd, buf, size, off);
  close(fd);

  if (-1 == out) {
    return -errno;
  }
  return out;
}

int sandbox_statfs(const char* path, struct statvfs* fs)
{
  CHECK_READ(path);
  return PROXY(statvfs(path, fs));
}

int sandbox_write(const char* path, const char* buf, size_t size,
		  off_t off, struct fuse_file_info* fi)
{
  CHECK_READWRITE(path);

  int fd = open(path, O_WRONLY);
  if (fd == -1) {
    return -errno;
  }

  ssize_t wrote = pwrite(fd, buf, size, off);
  int wrote_errno = errno;
  int closed = close(fd);

  if (-1 == wrote) {
    return -wrote_errno;
  }

#if 0
  /*
    FIXME: do something about this.
    With the current implementation, we can't report the error here without
    sometimes incorrectly reporting to the caller that we didn't write any
    bytes.
  */
  if (-1 == closed) {
    return -errno;
  }
#else
  (void)closed;
#endif

  return wrote;
}

int sandbox_opendir(const char* path, struct fuse_file_info* fi)
{
  CHECK_READ(path);

  DIR* dir = opendir(path);
  if (!dir) {
    return -errno;
  }
  closedir(dir);
  return 0;
}

int sandbox_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
		    off_t off, struct fuse_file_info* fi)
{
  CHECK_READ(path);

  DIR* dir = opendir(path);
  if (!dir) {
    return -errno;
  }

  int hide_mountpoint = (mountpoint.dirname() == path);
  struct dirent* ent;

  while ((ent = readdir(dir))) {
    if (hide_mountpoint && mountpoint.basename() == ent->d_name) {
      continue;
    }
    struct stat st{};
    st.st_ino = ent->d_ino;
    st.st_mode = ent->d_type << 12;
    if (filler(buf, ent->d_name, &st, 0))
      break;
  }

  closedir(dir);
  return 0;  
}

int sandbox_setxattr(const char* path, const char* name, const char* value,
		     size_t size, int flags)
{
  CHECK_READWRITE(path);
  return PROXY(lsetxattr(path, name, value, size, flags));
}

int sandbox_getxattr(const char* path, const char* name, char* value,
		     size_t size) 
{
  CHECK_READ(path);
  return PROXY(lgetxattr(path, name, value, size));
}

int sandbox_listxattr(const char* path, char* list, size_t size)
{
  CHECK_READ(path);
  return PROXY(llistxattr(path, list, size));
}

int sandbox_removexattr(const char* path, const char* name)
{
  CHECK_READWRITE(path);
  return PROXY(lremovexattr(path, name));
}

int sandbox_utimens(const char* path, const struct timespec tv[2])
{
  CHECK_READWRITE(path);
  int fd = open(path, O_WRONLY);
  if (fd == -1) {
    return -errno;
  }
  int out = futimens(fd, tv);
  int out_errno = errno;

  close(fd);

  if (-1 == out) {
    return -out_errno;
  }
  return 0;
}

void* sandbox_init(struct fuse_conn_info* conn)
{
  /* let parent know the filesystem has been initialized OK */
  int statusfd = *((int*)fuse_get_context()->private_data);
  int status = 0;
  write(statusfd, &status, sizeof(status));
  close(statusfd);
  debug("fuse init: notified parent\n");
  return 0;
}

int start_fuse_sandbox(const struct Context* ctx)
{
  int statusfd[2];
  if (-1 == pipe(statusfd)) {
    perror("fuse pipe");
    return -1;
  }

  int pid = fork();
  if (pid == -1) {
    perror("fuse fork");
    return -1;
  }
  if (pid > 0) {
    // wait for initialization from child
    close(statusfd[1]);
    int status;
    debug("fuse: reading status from child...\n");
    ssize_t read_bytes = read(statusfd[0], &status, sizeof(status));
    if (read_bytes == -1) {
      perror("fuse read status");
      close(statusfd[0]);
      return -1;
    }
    close(statusfd[0]);

    if (read_bytes == 0) {
      return -1;
    }

    debug("fuse: init in child reports %d\n", status);
    return pid;
  }

  close(statusfd[0]);

  prctl(PR_SET_NAME, "sandbox [fuse]");

  mountpoint.set(ctx->fuse_mountpoint);

  for (unsigned i = 0; ctx->fuse_writable_paths[i]; ++i) {
    Path p{ctx->fuse_writable_paths[i]};
    writable_paths.push_back(p);
    debug("fs: path (%s,%s) is writable\n", p.dirname().c_str(), p.basename().c_str());
  }

  const char* argv[] = {
    "sandbox",
    ctx->fuse_mountpoint,
    "-o", "direct_io",
    global.debug_mode > 1 ? "-d" : "-f",
    0
  };
  int argc = sizeof(argv)/sizeof(argv[0]) - 1;

  struct fuse_operations oper{};
  oper.access = sandbox_access;
  oper.chmod = sandbox_chmod;
  oper.chown = sandbox_chown;
  oper.getattr = sandbox_getattr;
  oper.getxattr = sandbox_getxattr;
  oper.init = sandbox_init;
  oper.link = sandbox_link;
  oper.listxattr = sandbox_listxattr;
  oper.mkdir = sandbox_mkdir;
  oper.mknod = sandbox_mknod;
  oper.open = sandbox_open;
  oper.opendir = sandbox_opendir;
  oper.read = sandbox_read;
  oper.readdir = sandbox_readdir;
  oper.readlink = sandbox_readlink;
  oper.removexattr = sandbox_removexattr;
  oper.rename = sandbox_rename;
  oper.rmdir = sandbox_rmdir;
  oper.setxattr = sandbox_setxattr;
  oper.statfs = sandbox_statfs;
  oper.symlink = sandbox_symlink;
  oper.truncate = sandbox_truncate;
  oper.unlink = sandbox_unlink;
  oper.utimens = sandbox_utimens;
  oper.write = sandbox_write;

  exit(fuse_main(argc, (char**)argv, &oper, &statusfd[1]));
}
