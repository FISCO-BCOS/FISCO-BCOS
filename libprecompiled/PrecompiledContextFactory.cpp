#include "PrecompiledContextFactory.h"
#include <libprecompiled/CRUDPrecompiled.h>
#include <libstorage/DBFactoryPrecompiled.h>
#include <libdevcore/Common.h>
#include <libstorage/MemoryDBFactory.h>

using namespace dev;
using namespace dev::precompiled;

void PrecompiledContextFactory::initPrecompiledContext(BlockInfo blockInfo, PrecompiledContext::Ptr context) {

#if 0
	dev::storage::AMOPStorage::Ptr stateStorage = std::make_shared<dev::storage::AMOPStorage>();
	stateStorage->setChannelRPCServer(_channelRPCServer);
	//stateStorage->setBlockHash(blockInfo.hash);
	//stateStorage->setNum(blockInfo.number.convert_to<int>());
	stateStorage->setTopic(_AMOPDBTopic);
	stateStorage->setMaxRetry(_maxRetry);
#endif

	//DBFactoryPrecompiled
	dev::storage::MemoryDBFactory::Ptr memoryDBFactory = std::make_shared<dev::storage::MemoryDBFactory>();
	memoryDBFactory->setStateStorage(_stateStorage);
	memoryDBFactory->setBlockHash(blockInfo.hash);
	memoryDBFactory->setBlockNum(blockInfo.number.convert_to<int>());

	auto dbFactoryPrecompiled = std::make_shared<dev::precompiled::DBFactoryPrecompiled>();
	dbFactoryPrecompiled->setMemoryDBFactory(memoryDBFactory);

	auto crudPrecompiled = std::make_shared<dev::precompiled::CRUDPrecompiled>();
	crudPrecompiled->setMemoryDBFactory(memoryDBFactory);

	context->setAddress2Precompiled(Address(0x1001), dbFactoryPrecompiled);
	context->setAddress2Precompiled(Address(0x1002), crudPrecompiled);

	context->setBlockInfo(blockInfo);
}

void PrecompiledContextFactory::setStateStorage(dev::storage::Storage::Ptr stateStorage) {
	_stateStorage = stateStorage;
}
