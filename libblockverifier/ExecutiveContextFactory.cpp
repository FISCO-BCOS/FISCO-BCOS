#include "ExecutiveContextFactory.h"
#include <libdevcore/Common.h>
#include <libstorage/CRUDPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/TableFactoryPrecompiled.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::executive;

void ExecutiveContextFactory::initExecutiveContext(
    BlockInfo blockInfo, h256 stateRoot, ExecutiveContext::Ptr context)
{
    // DBFactoryPrecompiled
    dev::storage::MemoryTableFactory::Ptr memoryTableFactory =
        std::make_shared<dev::storage::MemoryTableFactory>();
    memoryTableFactory->setStateStorage(m_stateStorage);
    memoryTableFactory->setBlockHash(blockInfo.hash);
    memoryTableFactory->setBlockNum(blockInfo.number);

    auto tableFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
    tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);

    context->setAddress2Precompiled(Address(0x1001), tableFactoryPrecompiled);
    context->setAddress2Precompiled(
        Address(0x1002), std::make_shared<dev::blockverifier::CRUDPrecompiled>());
    context->setMemoryTableFactory(memoryTableFactory);

    context->setBlockInfo(blockInfo);
    context->setPrecompiledContract(m_precompiledContract);
    context->setState(m_stateFactoryInterface->getState(stateRoot, memoryTableFactory));
}

void ExecutiveContextFactory::setStateStorage(dev::storage::Storage::Ptr stateStorage)
{
    m_stateStorage = stateStorage;
}

void ExecutiveContextFactory::setStateFactory(
    std::shared_ptr<dev::executive::StateFactoryInterface> stateFactoryInterface)
{
    m_stateFactoryInterface = stateFactoryInterface;
}
