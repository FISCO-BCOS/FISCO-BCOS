#include "PrecompiledContext.h"
#include <libdevcore/easylog.h>
#include <libevm/VMFace.h>

using namespace dev;
using namespace dev::precompiled;

PrecompiledContext::PrecompiledContext() {
  LOG(TRACE) << "构建PrecompiledContext";
}

void PrecompiledContext::beforeBlock() {
  LOG(DEBUG) << "beforeBlock:" << _blockInfo.hash;

  for (auto it : _address2Precompiled) {
    it.second->beforeBlock(shared_from_this());
  }
}

void PrecompiledContext::afterBlock(bool commit) {
  LOG(DEBUG) << "afterBlock:" << commit << " " << _blockInfo.hash;

  for (auto it : _address2Precompiled) {
    it.second->afterBlock(shared_from_this(), commit);
  }

  _address2Precompiled.clear();
}

bytes PrecompiledContext::call(Address address, bytesConstRef param) {
  try {
    LOG(TRACE) << "PrecompiledEngine调用:" << _blockInfo.hash << " "
               << _blockInfo.number << " " << address << " " << toHex(param);

    auto p = getPrecompiled(address);

    if (p) {
      bytes out = p->call(shared_from_this(), param);
      // LOG(TRACE) << "PrecompiledEngine调用结果:" <<
      return out;
    } else {
      LOG(DEBUG) << "未找到address:" << address;
    }
  } catch (std::exception &e) {
    LOG(ERROR) << "Precompiled执行错误:" << e.what();

    throw dev::eth::PrecompiledError();
  }

  return bytes();
}

std::string PrecompiledContext::toString(Address address) {
  LOG(DEBUG) << "toString:" << address;

  auto p = getPrecompiled(address);

  if (p) {
    return p->toString(shared_from_this());
  }

  return "";
}

Address PrecompiledContext::registerPrecompiled(Precompiled::Ptr p) {
  Address address(++_addressCount);

  _address2Precompiled.insert(std::make_pair(address, p));

  return address;
}

bool PrecompiledContext::isPrecompiled(Address address) {
  LOG(TRACE) << "PrecompiledEngine isPrecompiled:" << _blockInfo.hash << " "
             << address;

  auto p = getPrecompiled(address);

  if (p) {
    LOG(DEBUG) << "内部合约：" << address;
  }

  return p.get() != NULL;
}

Precompiled::Ptr PrecompiledContext::getPrecompiled(Address address) {
  LOG(TRACE) << "PrecompiledEngine getPrecompiled:" << _blockInfo.hash << " "
             << address;

  LOG(TRACE) << "address size:" << _address2Precompiled.size();
  auto itPrecompiled = _address2Precompiled.find(address);

  if (itPrecompiled != _address2Precompiled.end()) {
    return itPrecompiled->second;
  }

  return Precompiled::Ptr();
}

PrecompiledContext::Ptr PrecompiledContext::clone() {
  PrecompiledContext::Ptr precompiledContext =
      std::make_shared<PrecompiledContext>();

  precompiledContext->_address2Precompiled = _address2Precompiled;
  precompiledContext->_addressCount = _addressCount;
  precompiledContext->_blockInfo = _blockInfo;

  return precompiledContext;
}
