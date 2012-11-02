#include <stdio.h>
#include <stdarg.h>

#include "shared.h"

void debug(const char* format, ...)
{
  if (!global.debug_mode) {
    return;
  }
  
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}
