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
#include "include/UserPrecompiled.h"
#include <libdevcore/Common.h>
#include <libprecompiled/CNSPrecompiled.h>
#include <libprecompiled/CRUDPrecompiled.h>
#include <libprecompiled/ChainGovernancePrecompiled.h>
#include <libprecompiled/ConsensusPrecompiled.h>
#include <libprecompiled/ContractLifeCyclePrecompiled.h>
#include <libprecompiled/KVTableFactoryPrecompiled.h>
#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libprecompiled/PermissionPrecompiled.h>
#include <libprecompiled/PrecompiledResult.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libprecompiled/extension/DagTransferPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::executive;
using namespace dev::precompiled;

void ExecutiveContextFactory::setPrecompiledExecResultFactory(
    PrecompiledExecResultFactory::Ptr _precompiledExecResultFactory)
{
    m_precompiledExecResultFactory = _precompiledExecResultFactory;
}

void ExecutiveContextFactory::initExecutiveContext(
    BlockInfo blockInfo, h256 const& stateRoot, ExecutiveContext::Ptr context)
{
    auto memoryTableFactory =
        m_tableFactoryFactory->newTableFactory(blockInfo.hash, blockInfo.number);
    context->setPrecompiledExecResultFactory(m_precompiledExecResultFactory);
    auto tableFactoryPrecompiled = std::make_shared<dev::precompiled::TableFactoryPrecompiled>();
    tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
    context->setAddress2Precompiled(
        Address(0x1000), std::make_shared<dev::precompiled::SystemConfigPrecompiled>());
    context->setAddress2Precompiled(Address(0x1001), tableFactoryPrecompiled);
    context->setAddress2Precompiled(
        Address(0x1002), std::make_shared<dev::precompiled::CRUDPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x1003), std::make_shared<dev::precompiled::ConsensusPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x1004), std::make_shared<dev::precompiled::CNSPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x1005), std::make_shared<dev::precompiled::PermissionPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x1006), std::make_shared<dev::precompiled::ParallelConfigPrecompiled>());
    if (g_BCOSConfig.version() >= V2_3_0)
    {
        context->setAddress2Precompiled(
            Address(0x1007), std::make_shared<dev::precompiled::ContractLifeCyclePrecompiled>());
        auto kvTableFactoryPrecompiled =
            std::make_shared<dev::precompiled::KVTableFactoryPrecompiled>();
        kvTableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        context->setAddress2Precompiled(Address(0x1010), kvTableFactoryPrecompiled);
    }
    if (g_BCOSConfig.version() >= V2_5_0)
    {
        context->setAddress2Precompiled(
            Address(0x1008), std::make_shared<dev::precompiled::ChainGovernancePrecompiled>());
    }
    // register User developed Precompiled contract
    registerUserPrecompiled(context);
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

        auto tableInfo = std::make_shared<storage::TableInfo>();
        tableInfo->name = storage::SYS_CONFIG;
        tableInfo->key = storage::SYS_KEY;
        tableInfo->fields = std::vector<std::string>{"value"};

        auto condition = std::make_shared<dev::storage::Condition>();
        condition->EQ("key", key);
        auto values = m_stateStorage->select(blockInfo.number, tableInfo, key, condition);
        if (!values || values->size() != 1)
        {
            EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("[setTxGasLimitToContext]Select error");
            return;
        }

        auto value = values->get(0);
        if (!value)
        {
            EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("[setTxGasLimitToContext]Null pointer");
            return;
        }

        if (boost::lexical_cast<int>(value->getField("enable_num")) <= blockInfo.number)
        {
            ret = value->getField("value");
        }

        if (ret != "")
        {
            context->setTxGasLimit(boost::lexical_cast<uint64_t>(ret));
            EXECUTIVECONTEXT_LOG(TRACE) << LOG_DESC("[setTxGasLimitToContext]")
                                        << LOG_KV("txGasLimit", context->txGasLimit());
        }
        else
        {
            EXECUTIVECONTEXT_LOG(WARNING)
                << LOG_DESC("[setTxGasLimitToContext]Tx gas limit is null");
        }
    }
    catch (std::exception& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("[setTxGasLimitToContext]Failed")
                                    << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }
}
