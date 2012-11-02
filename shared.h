#ifndef SANDBOX_SHARED_H
#define SANDBOX_SHARED_H

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
