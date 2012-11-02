#include "path.h"

#include <libgen.h>

Path::Path(std::string const& in)
{
  set(in);
}

void Path::set(std::string const& in)
{
  _path = in;

  // basename, dirname may modify their argument :(
  {
    std::string incopy{in.c_str()};
    _basename = ::basename(const_cast<char*>(incopy.c_str()));
  }

  {
    std::string incopy{in.c_str()};
    _dirname = ::dirname(const_cast<char*>(incopy.c_str()));
  }

  _length = in.length();
}
