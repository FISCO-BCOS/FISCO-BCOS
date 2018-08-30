#pragma once
#include <libdevcore/Exceptions.h>
#include <exception>
#include <string>

namespace dev {

namespace storage {

class StorageException : public dev::Exception {
 public:
  StorageException(int errorCode, const std::string &what)
      : dev::Exception(what), _errorCode(errorCode) {}

  virtual int errorCode() { return _errorCode; }

 private:
  int _errorCode = 0;
};

}  // namespace storage

}  // namespace dev
