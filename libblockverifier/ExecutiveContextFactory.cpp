#include "ExecutiveContextFactory.h"
#include <libdevcore/Common.h>
#include <libstorage/CRUDPrecompiled.h>
#include <libstorage/DBFactoryPrecompiled.h>
#include <libstorage/MemoryDBFactory.h>

using namespace dev;
using namespace dev::blockverifier;

void ExecutiveContextFactory::initExecutiveContext(
    BlockInfo blockInfo, ExecutiveContext::Ptr context)
{
#if 0
	dev::storage::AMOPStorage::Ptr stateStorage = std::make_shared<dev::storage::AMOPStorage>();
	stateStorage->setChannelRPCServer(_channelRPCServer);
	//stateStorage->setBlockHash(blockInfo.hash);
	//stateStorage->setNum(blockInfo.number.convert_to<int>());
	stateStorage->setTopic(_AMOPDBTopic);
	stateStorage->setMaxRetry(_maxRetry);
#endif

    // DBFactoryPrecompiled
    dev::storage::MemoryDBFactory::Ptr memoryDBFactory =
        std::make_shared<dev::storage::MemoryDBFactory>();
    memoryDBFactory->setStateStorage(m_stateStorage);
    memoryDBFactory->setBlockHash(blockInfo.hash);
    memoryDBFactory->setBlockNum(blockInfo.number.convert_to<int>());

    auto dbFactoryPrecompiled = std::make_shared<dev::precompiled::DBFactoryPrecompiled>();
    dbFactoryPrecompiled->setMemoryDBFactory(memoryDBFactory);

    context->setAddress2Precompiled(Address(0x1001), dbFactoryPrecompiled);
    context->setAddress2Precompiled(
        Address(0x1002), std::make_shared<dev::precompiled::CRUDPrecompiled>());

    context->setBlockInfo(blockInfo);
}

void ExecutiveContextFactory::setStateStorage(dev::storage::Storage::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
}

void ExecutiveContextFactory::setOverlayDB(OverlayDB const& db)
{
    m_db = db;
}
