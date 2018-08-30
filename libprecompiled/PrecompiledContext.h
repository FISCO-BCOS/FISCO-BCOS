#pragma once

#include <libdevcore/FixedHash.h>
#include <libdevcrypto/Common.h>
#include "Precompiled.h"

namespace dev {

namespace precompiled {

struct BlockContext {
  std::map<Address, Precompiled::Ptr> address2Precompiled;
};

class PrecompiledContext
    : public std::enable_shared_from_this<PrecompiledContext> {
 public:
  typedef std::shared_ptr<PrecompiledContext> Ptr;

  PrecompiledContext();

  virtual ~PrecompiledContext(){};

  virtual void beforeBlock();            //区块执行前触发
  virtual void afterBlock(bool commit);  //区块提交后触发

  virtual bytes call(Address address, bytesConstRef param);  //交易调用

  virtual std::string toString(Address address);  //将某个address以string输出

  virtual Address registerPrecompiled(Precompiled::Ptr p);  //注册临时合约地址

  virtual bool isPrecompiled(Address address);

  Precompiled::Ptr getPrecompiled(Address address);

  void setAddress2Precompiled(Address address, Precompiled::Ptr precompiled) {
    _address2Precompiled.insert(std::make_pair(address, precompiled));
  }

  BlockInfo blockInfo() { return _blockInfo; }
  void setBlockInfo(BlockInfo blockInfo) { _blockInfo = blockInfo; }

  PrecompiledContext::Ptr clone();

 private:
  std::unordered_map<Address, Precompiled::Ptr> _address2Precompiled;
  int _addressCount = 0x10000;
  BlockInfo _blockInfo;
};

}  // namespace precompiled

}  // namespace dev
