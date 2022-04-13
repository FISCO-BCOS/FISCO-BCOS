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
#include <libprecompiled/GasChargeManagePrecompiled.h>
#include <libprecompiled/KVTableFactoryPrecompiled.h>
#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libprecompiled/PermissionPrecompiled.h>
#include <libprecompiled/PrecompiledResult.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libprecompiled/WorkingSealerManagerPrecompiled.h>
#include <libprecompiled/extension/CryptoPrecompiled.h>
#include <libprecompiled/extension/DagTransferPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::executive;
using namespace dev::precompiled;
ExecutiveContextFactory::ExecutiveContextFactory(
    std::shared_ptr<dev::precompiled::PrecompiledExecResultFactory> _precompiledExecResultFactory)
  : m_precompiledExecResultFactory(_precompiledExecResultFactory),
    m_buildInPrecompiledContract(std::make_shared<tbb::concurrent_unordered_map<Address,
            std::shared_ptr<precompiled::Precompiled>, std::hash<Address>>>())
{
    // init eth precompiled
    m_precompiledContract.insert(std::make_pair(
        dev::Address(1), dev::eth::PrecompiledContract(
                             3000, 0, dev::eth::PrecompiledRegistrar::executor("ecrecover"))));
    m_precompiledContract.insert(std::make_pair(dev::Address(2),
        dev::eth::PrecompiledContract(60, 12, dev::eth::PrecompiledRegistrar::executor("sha256"))));
    m_precompiledContract.insert(std::make_pair(
        dev::Address(3), dev::eth::PrecompiledContract(
                             600, 120, dev::eth::PrecompiledRegistrar::executor("ripemd160"))));
    m_precompiledContract.insert(
        std::make_pair(dev::Address(4), dev::eth::PrecompiledContract(15, 3,
                                            dev::eth::PrecompiledRegistrar::executor("identity"))));
    if (g_BCOSConfig.version() >= V2_5_0)
    {
        m_precompiledContract.insert({dev::Address{0x5},
            dev::eth::PrecompiledContract(dev::eth::PrecompiledRegistrar::pricer("modexp"),
                dev::eth::PrecompiledRegistrar::executor("modexp"))});
        m_precompiledContract.insert(
            {dev::Address{0x6}, dev::eth::PrecompiledContract(150, 0,
                                    dev::eth::PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
        m_precompiledContract.insert(
            {dev::Address{0x7}, dev::eth::PrecompiledContract(6000, 0,
                                    dev::eth::PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
        m_precompiledContract.insert({dev::Address{0x8},
            dev::eth::PrecompiledContract(
                dev::eth::PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
                dev::eth::PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
        m_precompiledContract.insert({dev::Address{0x9},
            dev::eth::PrecompiledContract(
                dev::eth::PrecompiledRegistrar::pricer("blake2_compression"),
                dev::eth::PrecompiledRegistrar::executor("blake2_compression"))});
    }
    // init fisco build-in precompiled
    m_buildInPrecompiledContract->insert(std::make_pair(
        SYS_CONFIG_ADDRESS, std::make_shared<dev::precompiled::SystemConfigPrecompiled>()));
    m_buildInPrecompiledContract->insert(std::make_pair(
        TABLE_FACTORY_ADDRESS, std::make_shared<dev::precompiled::TableFactoryPrecompiled>()));
    m_buildInPrecompiledContract->insert(
        std::make_pair(CRUD_ADDRESS, std::make_shared<dev::precompiled::CRUDPrecompiled>()));
    m_buildInPrecompiledContract->insert(std::make_pair(
        CONSENSUS_ADDRESS, std::make_shared<dev::precompiled::ConsensusPrecompiled>()));
    m_buildInPrecompiledContract->insert(
        std::make_pair(CNS_ADDRESS, std::make_shared<dev::precompiled::CNSPrecompiled>()));
    m_buildInPrecompiledContract->insert(std::make_pair(
        PERMISSION_ADDRESS, std::make_shared<dev::precompiled::PermissionPrecompiled>()));
    m_buildInPrecompiledContract->insert(std::make_pair(
        PARALLEL_CONFIG_ADDRESS, std::make_shared<dev::precompiled::ParallelConfigPrecompiled>()));
    if (g_BCOSConfig.version() >= V2_3_0)
    {
        m_buildInPrecompiledContract->insert(std::make_pair(CONTRACT_LIFECYCLE_ADDRESS,
            std::make_shared<dev::precompiled::ContractLifeCyclePrecompiled>()));
        m_buildInPrecompiledContract->insert(std::make_pair(KVTABLE_FACTORY_ADDRESS,
            std::make_shared<dev::precompiled::KVTableFactoryPrecompiled>()));
    }
    if (g_BCOSConfig.version() >= V2_5_0)
    {
        m_buildInPrecompiledContract->insert(std::make_pair(CHAINGOVERNANCE_ADDRESS,
            std::make_shared<dev::precompiled::ChainGovernancePrecompiled>()));
    }
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        // register workingSealerManagerPrecompiled for VRF-based-rPBFT
        m_buildInPrecompiledContract->insert(std::make_pair(WORKING_SEALER_MGR_ADDRESS,
            std::make_shared<dev::precompiled::WorkingSealerManagerPrecompiled>()));
    }
    if (g_BCOSConfig.version() >= V2_8_0)
    {
        m_buildInPrecompiledContract->insert(
            std::make_pair(CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>()));
    }
    if (g_BCOSConfig.version() >= V2_9_0)
    {
        m_buildInPrecompiledContract->insert(std::make_pair(
            GASCHARGEMANAGE_ADDRESS, std::make_shared<GasChargeManagePrecompiled>()));
    }
    // register User developed Precompiled contract
    registerUserPrecompiled(m_buildInPrecompiledContract);
    for (auto const& it : *m_buildInPrecompiledContract)
    {
        auto precompiled = it.second;
        precompiled->setPrecompiledExecResultFactory(m_precompiledExecResultFactory);
    }
}

ExecutiveContext::Ptr ExecutiveContextFactory::createExecutiveContext(
    BlockInfo blockInfo, h256 const& stateRoot)
{
    auto context = std::make_shared<ExecutiveContext>(m_buildInPrecompiledContract);
    context->setPrecompiledExecResultFactory(m_precompiledExecResultFactory);
    // update memoryTableFactory for tableFactoryPrecompiled
    auto memoryTableFactory =
        m_tableFactoryFactory->newTableFactory(blockInfo.hash, blockInfo.number);
    auto tableFactoryPrecompiled =
        std::dynamic_pointer_cast<dev::precompiled::TableFactoryPrecompiled>(
            m_buildInPrecompiledContract->at(TABLE_FACTORY_ADDRESS));
    tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);

    // register parallelConfigPrecompiled
    auto parallelConfigPrecompiled = m_buildInPrecompiledContract->at(PARALLEL_CONFIG_ADDRESS);
    context->registerParallelPrecompiled(parallelConfigPrecompiled);
    if (g_BCOSConfig.version() >= V2_3_0)
    {
        auto kvTableFactoryPrecompiled =
            std::dynamic_pointer_cast<dev::precompiled::KVTableFactoryPrecompiled>(
                m_buildInPrecompiledContract->at(KVTABLE_FACTORY_ADDRESS));
        kvTableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
    }
    context->setMemoryTableFactory(memoryTableFactory);
    context->setBlockInfo(blockInfo);
    context->setPrecompiledContract(m_precompiledContract);
    context->setState(m_stateFactoryInterface->getState(stateRoot, memoryTableFactory));
    context->setStateStorage(m_stateStorage);
    setTxGasLimitToContext(context);
    return context;
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
        auto configRet = getSysConfigByKey(m_stateStorage, key, blockInfo.number);
        auto ret = configRet->first;
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
