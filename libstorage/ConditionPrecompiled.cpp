#include "ConditionPrecompiled.h"
#include <libethcore/ABI.h>
#include <libdevcore/easylog.h>

using namespace dev;
using namespace dev::precompiled;

void ConditionPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext>) {
	//什么也不做
}

void ConditionPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext>, bool commit) {
	//什么也不做
}

std::string ConditionPrecompiled::toString(std::shared_ptr<PrecompiledContext>) {
	return "Condition";
}

bytes ConditionPrecompiled::call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param) {
	LOG(DEBUG) << "调用Condition:" << toHex(param);

	//解析出函数名
	uint32_t func = getParamFunc(param);
	bytesConstRef data = getParamData(param);

	LOG(DEBUG) << "func:" << std::hex << func;

	dev::eth::ContractABI abi;

	bytes out;

	switch (func) {
	case 0xc559a73a: { //EQ(string,address)
		std::string str;
		Address address;
		abi.abiOut(data, str, address);

		StringPrecompiled::Ptr stringPrecompiled = std::dynamic_pointer_cast<StringPrecompiled>(context->getPrecompiled(address));
		_condition->EQ(str, stringPrecompiled->toString(context));

		break;
	}
	case 0xe44594b9: { //EQ(string,int256)
		std::string str;
		u256 num;
		abi.abiOut(data, str, num);

		_condition->EQ(str, boost::lexical_cast<std::string>(num));

		break;
	}
	case 0xcd30a1d1: { //EQ(string,string)
		std::string str;
		std::string value;
		abi.abiOut(data, str, value);

		_condition->EQ(str, value);

		break;
	}
	case 0x42f8dd31: { //GE(string,int256)
		std::string str;
		u256 value;
		abi.abiOut(data, str, value);

		_condition->GE(str, boost::lexical_cast<std::string>(value));

		break;
	}
	case 0x08ad6333: { //GT(string,int256)
		std::string str;
		u256 value;
		abi.abiOut(data, str, value);

		_condition->GT(str, boost::lexical_cast<std::string>(value));

		break;
	}
	case 0xb6f23857: { //LE(string,int256)
		std::string str;
		u256 value;
		abi.abiOut(data, str, value);

		_condition->LE(str, boost::lexical_cast<std::string>(value));

		break;
	}
	case 0xc31c9b65: { //LT(string,int256)
		std::string str;
		u256 value;
		abi.abiOut(data, str, value);

		_condition->LT(str, boost::lexical_cast<std::string>(value));

		break;
	}
	case 0x9e029900: { //NE(string,address)
		std::string str;
		Address address;
		abi.abiOut(data, str, address);

		StringPrecompiled::Ptr stringPrecompiled = std::dynamic_pointer_cast<StringPrecompiled>(context->getPrecompiled(address));
		_condition->NE(str, stringPrecompiled->toString(context));

		break;
	}
	case 0x39aef024: { //NE(string,int256)
		std::string str;
		u256 num;
		abi.abiOut(data, str, num);

		_condition->NE(str, boost::lexical_cast<std::string>(num));

		break;
	}
	case 0x2783acf5: { //NE(string,string)
		std::string str;
		std::string value;
		abi.abiOut(data, str, value);

		_condition->NE(str, value);

		break;
	}
	case 0x2e0d738a: { //limit(int256)
		u256 num;
		abi.abiOut(data, num);

		_condition->limit(num.convert_to<size_t>());

		break;
	}
	case 0x7ec1cc65: { //limit(int256,int256)
		u256 offset;
		u256 size;
		abi.abiOut(data, offset, size);

		_condition->limit(offset.convert_to<size_t>(), size.convert_to<size_t>());

		break;
	}
	default: {
		break;
	}
	}

	return out;
}
