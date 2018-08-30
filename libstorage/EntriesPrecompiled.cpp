#include "EntriesPrecompiled.h"
#include <libethcore/ABI.h>
#include <libdevcore/easylog.h>
#include "EntryPrecompiled.h"

using namespace dev;
using namespace dev::precompiled;

void dev::precompiled::EntriesPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext>) {
	//什么也不做
}

void dev::precompiled::EntriesPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext>, bool commit) {
	//什么也不做
}

std::string dev::precompiled::EntriesPrecompiled::toString(std::shared_ptr<PrecompiledContext>) {
	return "Entries";
}

bytes dev::precompiled::EntriesPrecompiled::call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param) {
	LOG(DEBUG)<< "调用Entries:" << toHex(param);

	//解析出函数名
	uint32_t func = getParamFunc(param);
	bytesConstRef data = getParamData(param);

	LOG(DEBUG) << "func:" << std::hex << func;

	dev::eth::ContractABI abi;

	bytes out;

	switch (func) {
		case 0x846719e0: { //get(int256)
			u256 num;
			abi.abiOut(data, num);

			auto entry = _entries->get(num.convert_to<size_t>());
			EntryPrecompiled::Ptr entryPrecompiled = std::make_shared<EntryPrecompiled>();

			entryPrecompiled->setEntry(entry);
			entryPrecompiled->setStringFactoryPrecompiled(_stringFactoryPrecompiled);

			Address address = context->registerPrecompiled(entryPrecompiled);

			out = abi.abiIn("", address);

			break;
		}
		case 0x949d225d: { //size()
			u256 c = _entries->size();

			out = abi.abiIn("", c);

			break;
		}
		default: {
			break;
		}
	}

	return out;
}
