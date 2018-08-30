#pragma once

#include <libdevcore/FixedHash.h>
#include <memory>

namespace dev {

namespace precompiled {

class PrecompiledContext;

struct BlockInfo {
  h256 hash;
  u256 number;
};

class Precompiled : public std::enable_shared_from_this<Precompiled> {
 public:
  typedef std::shared_ptr<Precompiled> Ptr;

  virtual ~Precompiled(){};

  virtual void beforeBlock(std::shared_ptr<PrecompiledContext> context) = 0;
  virtual void afterBlock(std::shared_ptr<PrecompiledContext> context,
                          bool commit) = 0;

  virtual std::string toString(std::shared_ptr<PrecompiledContext>) {
    return "";
  }

  virtual bytes call(std::shared_ptr<PrecompiledContext> context,
                     bytesConstRef param) = 0;

  virtual uint32_t getParamFunc(bytesConstRef param) {
    auto funcBytes = param.cropped(0, 4);
    uint32_t func = *((uint32_t*)(funcBytes.data()));

    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
           ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
  }

  virtual bytesConstRef getParamData(bytesConstRef param) {
    return param.cropped(4);
  }
};

}  // namespace precompiled

}  // namespace dev
