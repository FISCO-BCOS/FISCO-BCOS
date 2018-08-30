#include "StringFactoryPrecompiled.h"
#include <libethcore/ABI.h>
#include <libdevcore/easylog.h>

using namespace dev;
using namespace dev::precompiled;

void dev::precompiled::StringFactoryPrecompiled::beforeBlock(PrecompiledContext::Ptr context) {
}

void dev::precompiled::StringFactoryPrecompiled::afterBlock(PrecompiledContext::Ptr context, bool commit) {
}

std::string dev::precompiled::StringFactoryPrecompiled::toString(PrecompiledContext::Ptr context) {
	return "StringFactory";
}

bytes dev::precompiled::StringFactoryPrecompiled::call(PrecompiledContext::Ptr context, bytesConstRef param) {
	LOG(DEBUG) << "调用StringFactory:" << toHex(param);

	//解析出函数名
	uint32_t func = getParamFunc(param);
	bytesConstRef data = getParamData(param);

	LOG(DEBUG) << "func:" << std::hex << func;

	dev::eth::ContractABI abi;

	bytes out;

	switch (func) {
	case 0xb78f2cb3: { //newString(address)
		Address address;
		abi.abiOut(data, address);

		std::string str = context->toString(address);
		Address newAddress = newString(context, str);

		out = abi.abiIn("", newAddress);

		break;
	}
	case 0xb96d4ba0: { //newString(address,int256,int256)
		Address address;
		u256 begin = 0;
		u256 end = 0;
		abi.abiOut(data, address, begin, end);

		std::string str = context->toString(address);
		str = str.substr(begin.convert_to<size_t>(), end.convert_to<size_t>());
		Address newAddress = newString(context, str);

		out = abi.abiIn("", newAddress);

		break;
	}
	case 0xcbea0803: { //newString(int256)
		u256 num = 0;
		abi.abiOut(data, num);

		std::string str = boost::lexical_cast<std::string>(num);
		Address newAddress = newString(context, str);

		out = abi.abiIn("", newAddress);

		break;
	}
	case 0xa316057e: { //newString(string)
		std::string str;
		abi.abiOut(data, str);

		Address newAddress = newString(context, str);

		out = abi.abiIn("", newAddress);

		break;
	}
	case 0xa40fe5de: { //newString(string,int256,int256)
		std::string str;
		u256 begin = 0;
		u256 end = 0;
		abi.abiOut(data, str, begin, end);

		str = str.substr(begin.convert_to<size_t>(), end.convert_to<size_t>());
		Address newAddress = newString(context, str);

		out = abi.abiIn("", newAddress);

		break;
	}
	default: {
		break;
	}
	}

	return out;
}

Address dev::precompiled::StringFactoryPrecompiled::newString(PrecompiledContext::Ptr context, const std::string& str) {
	auto stringPrecompiled = std::make_shared<StringPrecompiled>(str);
	//stringPrecompiled->setPrecompiledEngine(_precompiledEngine);

	return context->registerPrecompiled(stringPrecompiled);
}
