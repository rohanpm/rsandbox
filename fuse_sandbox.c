#define FUSE_USE_VERSION 26
#define _GNU_SOURCE
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

#include "shared.h"
#include "fuse_sandbox.h"

/* a path decomposed */
struct Path {
  char* path;
  char* dirname;
  char* basename;
  int len;
};

static struct Path mountpoint = {};
static struct Path writable_paths[8] = {};

/* strdup or die */
char* xstrdup(const char* in)
{
  char* out = strdup(in);
  if (!out) {
    perror("malloc");
    exit(127);
  }
  return out;
}

/* allocating dirname() */
char* adirname(const char* path)
{
  char* in = xstrdup(path);
  char* out = xstrdup(dirname(in));
  free(in);
  return out;
}

/* allocating basename() */
char* abasename(const char* path)
{
  char* in = xstrdup(path);
  char* out = xstrdup(basename(in));
  free(in);
  return out;
} 

void path_init(struct Path* out, const char* path)
{
  out->path = xstrdup(path);
  out->len = strlen(path);
  out->basename = abasename(path);
  out->dirname = adirname(path);
}

void path_destroy(struct Path* p)
{
  free(p->path);
  free(p->basename);
  free(p->dirname);
}

/* returns 1 if path should be hidden in the sandbox */
int should_hide(const char* path)
{
  return (0 == strncmp(path, mountpoint.path, mountpoint.len));
}

/* returns 1 if write access under the given path should not be blocked */
int permit_write(const char* path)
{
  for (struct Path* tree = writable_paths; tree->path; ++tree) {
    if (0 == strncmp(path, tree->path, tree->len)
	&& strlen(path) > tree->len) {
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

  int hide_mountpoint = (0 == strcmp(path, mountpoint.dirname));
  struct dirent* ent;

  while (ent = readdir(dir)) {
    if (hide_mountpoint && 0 == strcmp(ent->d_name, mountpoint.basename)) {
      continue;
    }
    struct stat st;
    memset(&st, 0, sizeof(st));
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

const struct fuse_operations oper = {
  .getattr = sandbox_getattr,
  .readlink = sandbox_readlink,
  .mknod = sandbox_mknod,
  .mkdir = sandbox_mkdir,
  .rmdir = sandbox_rmdir,
  .symlink = sandbox_symlink,
  .rename = sandbox_rename,
  .link = sandbox_link,
  .chmod = sandbox_chmod,
  .chown = sandbox_chown,
  .truncate = sandbox_truncate,
  .open = sandbox_open,
  .read = sandbox_read,
  .write = sandbox_write,
  .unlink = sandbox_unlink,
  .opendir = sandbox_opendir,
  .readdir = sandbox_readdir,
  .access = sandbox_access,
  .statfs = sandbox_statfs,
  .getxattr = sandbox_getxattr,
  .setxattr = sandbox_setxattr,
  .listxattr = sandbox_listxattr,
  .removexattr = sandbox_removexattr,
  .utimens = sandbox_utimens,
  .init = sandbox_init
};

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

  path_init(&mountpoint, ctx->fuse_mountpoint);
  for (int i = 0; ctx->fuse_writable_paths[i]; ++i) {
    assert(i < sizeof(writable_paths)/sizeof(writable_paths[0]));
    path_init(&writable_paths[i], ctx->fuse_writable_paths[i]);
    debug("fs: path %s is writable\n", ctx->fuse_writable_paths[i]);
  }

  char* argv[] = {
    "sandbox",
    mountpoint.path,
    "-o", "direct_io",
    global.debug_mode > 1 ? "-d" : "-f",
    0
  };
  int argc = sizeof(argv)/sizeof(argv[0]) - 1;
  exit(fuse_main(argc, argv, &oper, &statusfd[1]));
}
