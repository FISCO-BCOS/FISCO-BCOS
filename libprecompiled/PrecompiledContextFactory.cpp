#include "PrecompiledContextFactory.h"
#include <libprecompiled/StringFactoryPrecompiled.h>
#include <libstorage/DBFactoryPrecompiled.h>
#include <libdevcore/Common.h>
#include <libstorage/MemoryStateDBFactory.h>

using namespace dev;
using namespace dev::precompiled;

void PrecompiledContextFactory::initPrecompiledContext(BlockInfo blockInfo, PrecompiledContext::Ptr context) {
	//StringFactoryPrecompiled
	auto stringFactoryPrecompiled = std::make_shared<dev::precompiled::StringFactoryPrecompiled>();

#if 0
	dev::storage::AMOPStateStorage::Ptr stateStorage = std::make_shared<dev::storage::AMOPStateStorage>();
	stateStorage->setChannelRPCServer(_channelRPCServer);
	//stateStorage->setBlockHash(blockInfo.hash);
	//stateStorage->setNum(blockInfo.number.convert_to<int>());
	stateStorage->setTopic(_AMOPDBTopic);
	stateStorage->setMaxRetry(_maxRetry);
#endif

	//DBFactoryPrecompiled
	dev::storage::MemoryStateDBFactory::Ptr memoryDBFactory = std::make_shared<dev::storage::MemoryStateDBFactory>();
	memoryDBFactory->setStateStorage(_stateStorage);
	memoryDBFactory->setBlockHash(blockInfo.hash);
	memoryDBFactory->setBlockNum(blockInfo.number.convert_to<int>());

	auto dbFactoryPrecompiled = std::make_shared<dev::precompiled::DBFactoryPrecompiled>();
	dbFactoryPrecompiled->setMemoryDBFactory(memoryDBFactory);
	dbFactoryPrecompiled->setStringFactoryPrecompiled(stringFactoryPrecompiled);

	context->setAddress2Precompiled(Address(0x1000), stringFactoryPrecompiled);
	context->setAddress2Precompiled(Address(0x1001), dbFactoryPrecompiled);

	context->setBlockInfo(blockInfo);
}

void PrecompiledContextFactory::setStateStorage(dev::storage::StateStorage::Ptr stateStorage) {
	_stateStorage = stateStorage;
}
