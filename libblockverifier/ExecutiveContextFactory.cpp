#include "ExecutiveContextFactory.h"
#include <libdevcore/Common.h>
#include <libstorage/CRUDPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/TableFactoryPrecompiled.h>

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
    dev::storage::MemoryTableFactory::Ptr memoryTableFactory =
        std::make_shared<dev::storage::MemoryTableFactory>();
    memoryTableFactory->setStateStorage(m_stateStorage);
    memoryTableFactory->setBlockHash(blockInfo.hash);
    memoryTableFactory->setBlockNum(blockInfo.number.convert_to<int>());

    auto tableFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
    tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);

    context->setAddress2Precompiled(Address(0x1001), tableFactoryPrecompiled);
    context->setAddress2Precompiled(
        Address(0x1002), std::make_shared<dev::blockverifier::CRUDPrecompiled>());

    context->setBlockInfo(blockInfo);
}

void ExecutiveContextFactory::setStateStorage(dev::storage::Storage::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
}

void ExecutiveContextFactory::setOverlayDB(OverlayDB& db)
{
    m_db = db;
}
