#ifndef SANDBOX_PATH_H
#define SANDBOX_PATH_H

#include <string>

class Path {
 public:
  Path() = default;
  Path(std::string const&);
  void set(std::string const&);
  inline std::string const& path() const { return _path; }
  inline std::string const& dirname() const { return _dirname; }
  inline std::string const& basename() const { return _basename; }
  inline size_t length() const { return _length; }
 private:
  std::string _path;
  std::string _dirname;
  std::string _basename;
  size_t _length;
};

#endif
