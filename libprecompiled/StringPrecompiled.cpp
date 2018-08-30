#include "StringPrecompiled.h"
#include <libethcore/ABI.h>
#include <libdevcore/easylog.h>

using namespace dev;
using namespace dev::precompiled;

void StringPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext>) {
}

void StringPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext>, bool commit) {
}

std::string StringPrecompiled::toString(std::shared_ptr<PrecompiledContext>) {
	return _str;
}

bytes StringPrecompiled::call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param) {
	uint32_t func = getParamFunc(param);
	bytesConstRef data = getParamData(param);

	dev::eth::ContractABI abi;

	bytes out;

	switch(func) {
	case 0x1d730734: { //charAt(int256)
		u256 index = 0;
		abi.abiOut(data, index);

		byte b = _str[index.convert_to<size_t>()];

		out = abi.abiIn("", b);

		break;
	}
	case 0x2214419b: { //concat(address)
		Address address;
		abi.abiOut(data, address);

		std::string str = context->toString(address);
		_str += str;

		break;
	}
	case 0xf632a809: { //concat(bytes1)
		byte b = 0;
		abi.abiOut(data, b);

		if(b != 0) {
			_str += b;
		}

		break;
	}
	case 0x7881c417: { //concat(string)
		std::string str;
		abi.abiOut(data, str);

		_str += str;

		break;
	}
	case 0x5dbe47e8: { //contains(address)
		Address address;
		abi.abiOut(data, address);

		std::string str = context->toString(address);
		auto pos = _str.find(str);
		if(pos != _str.npos) {
			out = abi.abiIn("", u256(1));
		}
		else {
			out = abi.abiIn("", u256(0));
		}

		break;
	}
	case 0xd556a894: { //contains(bytes1)
		byte b = 0;
		abi.abiOut(data, b);

		auto pos = _str.find(b);
		if(pos != _str.npos) {
			out = abi.abiIn("", u256(1));
		}
		else {
			out = abi.abiIn("", u256(0));
		}

		break;
	}
	case 0x6833d54f: { //contains(string)
		std::string str;
		abi.abiOut(data, str);

		auto pos = _str.find(str);
		if(pos != _str.npos) {
			out = abi.abiIn("", u256(1));
		}
		else {
			out = abi.abiIn("", u256(0));
		}

		break;
	}
	case 0xb9737cbe: { //equal(address)
		Address address;
		abi.abiOut(data, address);

		std::string str = context->toString(address);
		if(_str.compare(str)) {
			out = abi.abiIn("", u256(1));
		}
		else {
			out = abi.abiIn("", u256(0));
		}

		break;
	}
	case 0x7f241421: { //equal(string)
		std::string str;
		abi.abiOut(data, str);

		if (_str.compare(str)) {
			out = abi.abiIn("", u256(1));
		} else {
			out = abi.abiIn("", u256(0));
		}

		break;
	}
	case 0x681fe70c: { //isEmpty()
		out = abi.abiIn("", u256(_str.empty()));

		break;
	}
	case 0x1f7b6d32: { //length()
		out = abi.abiIn("", u256(_str.size()));

		break;
	}
	case 0xa833386b: { //toBytes32()
		dev::string32 s32 = dev::eth::toString32(_str);

		out = abi.abiIn("", s32);

		break;
	}
	case 0x3f96f3a3: { //toInt()
		u256 num = boost::lexical_cast<u256>(_str);

		out = abi.abiIn("", num);

		break;
	}
	case 0x47e46806: { //toString()
		out = abi.abiIn("", _str);
	}
	default: {
		LOG(ERROR) << "未知调用:" << func;
	}
	}

	return out;
}
