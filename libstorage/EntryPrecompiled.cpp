#include "EntryPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::precompiled;

void EntryPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext>) {
  //什么也不做
}

void EntryPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext>,
                                  bool commit) {
  //什么也不做
}

std::string EntryPrecompiled::toString(std::shared_ptr<PrecompiledContext>) {
  return "Entry";
}

bytes EntryPrecompiled::call(std::shared_ptr<PrecompiledContext> context,
                             bytesConstRef param) {
  LOG(DEBUG) << "调用Entry:";

  //解析出函数名
  uint32_t func = getParamFunc(param);
  bytesConstRef data = getParamData(param);

  LOG(DEBUG) << "func:" << std::hex << func;

  dev::eth::ContractABI abi;

  bytes out;

  switch (func) {
    case 0xfda69fae: {  // getInt(string)
      std::string str;
      abi.abiOut(data, str);

      std::string value = _entry->getField(str);

      u256 num = boost::lexical_cast<u256>(value);
      out = abi.abiIn("", num);

      break;
    }
    case 0x9c981fcb: {  // getString(string)
      std::string str;
      abi.abiOut(data, str);

      std::string value = _entry->getField(str);
      Address address = _stringFactoryPrecompiled->newString(context, value);

      out = abi.abiIn("", address);

      break;
    }
    case 0xa815ff15: {  // set(string,address)
      std::string str;
      Address address;
      abi.abiOut(data, str, address);

      auto stringPrecompiled = context->getPrecompiled(address);
      _entry->setField(str, stringPrecompiled->toString(context));

      break;
    }
    case 0x2ef8ba74: {  // set(string,int256)
      std::string str;
      u256 value;
      abi.abiOut(data, str, value);

      _entry->setField(str, boost::lexical_cast<std::string>(value));

      break;
    }
    case 0xe942b516: {  // set(string,string)
      std::string str;
      std::string value;
      abi.abiOut(data, str, value);

      _entry->setField(str, value);

      break;
    }
    case 0xbf40fac1: {  // getAddress(string)
      std::string str;
      abi.abiOut(data, str);

      std::string value = _entry->getField(str);
      Address ret = Address(value);
      out = abi.abiIn("", ret);
      break;
    }
    case 0xd52decd4: {  //"getBytes64(string)"
      std::string str;
      abi.abiOut(data, str);

      std::string value = _entry->getField(str);
      string64 ret;
      for (unsigned i = 0; i < 64; ++i)
        ret[i] = i < value.size() ? value[i] : 0;

      out = abi.abiIn("", ret);
      break;
    }
    default: { break; }
  }

  return out;
}
