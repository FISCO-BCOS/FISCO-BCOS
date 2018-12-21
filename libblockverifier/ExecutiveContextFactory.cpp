/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file ExecutiveContext.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#include "ExecutiveContextFactory.h"
#include <libdevcore/Common.h>
#include <libstorage/CNSPrecompiled.h>
#include <libstorage/CRUDPrecompiled.h>
#include <libstorage/ConsensusPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/SystemConfigPrecompiled.h>
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

    context->setAddress2Precompiled(
        Address(0x1000), std::make_shared<dev::blockverifier::SystemConfigPrecompiled>());
    context->setAddress2Precompiled(Address(0x1001), tableFactoryPrecompiled);
    context->setAddress2Precompiled(
        Address(0x1002), std::make_shared<dev::blockverifier::CRUDPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x1003), std::make_shared<dev::blockverifier::ConsensusPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x1004), std::make_shared<dev::blockverifier::CNSPrecompiled>());
    context->setMemoryTableFactory(memoryTableFactory);

    context->setBlockInfo(blockInfo);
    context->setPrecompiledContract(m_precompiledContract);
    context->setState(m_stateFactoryInterface->getState(stateRoot, memoryTableFactory));
    setTxGasLimitToContext(context);
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

void ExecutiveContextFactory::setTxGasLimitToContext(ExecutiveContext::Ptr context)
{
    // get value from db
    try
    {
        std::string key = "tx_gas_limit";
        BlockInfo blockInfo = context->blockInfo();
        std::string ret;

        auto values =
            m_stateStorage->select(blockInfo.hash, blockInfo.number, storage::SYS_CONFIG, key);
        if (!values || values->size() != 1)
        {
            EXECUTIVECONTEXT_LOG(ERROR) << "[#setTxGasLimitToContext] select error.";
            return;
        }

        auto value = values->get(0);
        if (!value)
        {
            EXECUTIVECONTEXT_LOG(ERROR) << "[#setTxGasLimitToContext] null point.";
            return;
        }

        if (boost::lexical_cast<int>(value->getField("enable_num")) <= blockInfo.number)
        {
            ret = value->getField("value");
        }

        if (ret != "")
        {
            context->setTxGasLimit(boost::lexical_cast<uint64_t>(ret));
            EXECUTIVECONTEXT_LOG(TRACE) << "[#setTxGasLimitToContext] tx_gas_limit:" << context->txGasLimit();
        }
        else
        {
            EXECUTIVECONTEXT_LOG(ERROR) << "[#setTxGasLimitToContext] tx_gas_limit is null.";
        }
    }
    catch (std::exception& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR)
            << "[#setTxGasLimitToContext] failed [EINFO]: " << boost::diagnostic_information(e);
    }
}
