#include "DBPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include "ConditionPrecompiled.h"
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include "DB.h"

using namespace dev;
using namespace dev::precompiled;

void DBPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext>) {
  //什么也不做
}

void DBPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext> context,
                               bool commit) {
  if (!commit) {
    _DB->clear();
  }
}

std::string DBPrecompiled::toString(std::shared_ptr<PrecompiledContext>) {
  return "DB";
}

bytes DBPrecompiled::call(std::shared_ptr<PrecompiledContext> context,
                          bytesConstRef param) {
  LOG(DEBUG) << "调用DB";

  //解析出函数名
  uint32_t func = getParamFunc(param);
  bytesConstRef data = getParamData(param);

  LOG(DEBUG) << "func:" << std::hex << func;

  dev::eth::ContractABI abi;

  bytes out;

  switch (func) {
    case 0xe8434e39: {  // select(string,address)
      std::string key;
      Address conditionAddress;
      abi.abiOut(data, key, conditionAddress);

      ConditionPrecompiled::Ptr conditionPrecompiled =
          std::dynamic_pointer_cast<ConditionPrecompiled>(
              context->getPrecompiled(conditionAddress));
      auto condition = conditionPrecompiled->getCondition();

      auto entries = _DB->select(key, condition);
      auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
      entriesPrecompiled->setEntries(entries);

      auto newAddress = context->registerPrecompiled(entriesPrecompiled);
      out = abi.abiIn("", newAddress);

      break;
    }
    case 0x31afac36: {  // insert(string,address)
      std::string key;
      Address entryAddress;
      abi.abiOut(data, key, entryAddress);

      EntryPrecompiled::Ptr entryPrecompiled =
          std::dynamic_pointer_cast<EntryPrecompiled>(
              context->getPrecompiled(entryAddress));
      auto entry = entryPrecompiled->getEntry();

      size_t count = _DB->insert(key, entry);
      out = abi.abiIn("", u256(count));

      break;
    }
    case 0x7857d7c9: {  // newCondition()
      auto condition = _DB->newCondition();
      auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
      conditionPrecompiled->setCondition(condition);

      auto newAddress = context->registerPrecompiled(conditionPrecompiled);
      out = abi.abiIn("", newAddress);

      break;
    }
    case 0x13db9346: {  // newEntry()
      auto entry = _DB->newEntry();
      auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
      entryPrecompiled->setEntry(entry);

      auto newAddress = context->registerPrecompiled(entryPrecompiled);
      out = abi.abiIn("", newAddress);

      break;
    }
    case 0x28bb2117: {  // remove(string,address)
      std::string key;
      Address conditionAddress;
      abi.abiOut(data, key, conditionAddress);

      ConditionPrecompiled::Ptr conditionPrecompiled =
          std::dynamic_pointer_cast<ConditionPrecompiled>(
              context->getPrecompiled(conditionAddress));
      auto condition = conditionPrecompiled->getCondition();

      size_t count = _DB->remove(key, condition);
      out = abi.abiIn("", u256(count));

      break;
    }
    case 0xbf2b70a1: {  // update(string,address,address)
      std::string key;
      Address entryAddress;
      Address conditionAddress;
      abi.abiOut(data, key, entryAddress, conditionAddress);

      EntryPrecompiled::Ptr entryPrecompiled =
          std::dynamic_pointer_cast<EntryPrecompiled>(
              context->getPrecompiled(entryAddress));
      ConditionPrecompiled::Ptr conditionPrecompiled =
          std::dynamic_pointer_cast<ConditionPrecompiled>(
              context->getPrecompiled(conditionAddress));
      auto entry = entryPrecompiled->getEntry();
      auto condition = conditionPrecompiled->getCondition();

      size_t count = _DB->update(key, entry, condition);
      out = abi.abiIn("", u256(count));

      break;
    }
#if 0
	case 0xbc902ad2: { //insert(address)
		Address address;
		abi.abiOut(data, address);

		EntryPrecompiled::Ptr entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(address));
		auto entry = entryPrecompiled->getEntry();

		size_t count = _DB->insert(entry);
		out = abi.abiIn("", u256(count));

		break;
	}
	case 0x7857d7c9: { //newCondition()
		auto condition = _DB->newCondition();
		auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
		conditionPrecompiled->setCondition(condition);

		auto newAddress = context->registerPrecompiled(conditionPrecompiled);
		out = abi.abiIn("", newAddress);

		break;
	}
	case 0x13db9346: { //newEntry()
		auto entry = _DB->newEntry();
		auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
		entryPrecompiled->setEntry(entry);

		auto newAddress = context->registerPrecompiled(entryPrecompiled);
		out = abi.abiIn("", newAddress);

		break;
	}
	case 0x29092d0e: { //remove(address)
		Address address;
		abi.abiOut(data, address);

		ConditionPrecompiled::Ptr conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(context->getPrecompiled(address));
		auto condition = conditionPrecompiled->getCondition();

		size_t count = _DB->remove(condition);
		out = abi.abiIn("", u256(count));

		break;
	}
	case 0x4f49f01c: { //select(address)
		Address address;
		abi.abiOut(data, address);

		ConditionPrecompiled::Ptr conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(context->getPrecompiled(address));
		auto condition = conditionPrecompiled->getCondition();

		auto entries = _DB->select(condition);
		auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
		entriesPrecompiled->setEntries(entries);

		auto newAddress = context->registerPrecompiled(entriesPrecompiled);
		out = abi.abiIn("", newAddress);

		break;
	}
	case 0xc640752d: { //update(address,address)
		Address entryAddress;
		Address conditionAddress;
		abi.abiOut(data, entryAddress, conditionAddress);

		EntryPrecompiled::Ptr entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress));
		ConditionPrecompiled::Ptr conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(context->getPrecompiled(conditionAddress));
		auto entry = entryPrecompiled->getEntry();
		auto condition = conditionPrecompiled->getCondition();

		size_t count = _DB->update(entry, condition);
		out = abi.abiIn("", u256(count));

		break;
	}
#endif
    default: { break; }
  }

  return out;
}

h256 DBPrecompiled::hash() { return _DB->hash(); }
