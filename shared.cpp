#include <stdio.h>
#include <stdarg.h>

#include "shared.h"

int Global::debug_mode = 0;

void debug(const char* format, ...)
{
  if (!Global::debug_mode) {
    return;
  }
  
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}
