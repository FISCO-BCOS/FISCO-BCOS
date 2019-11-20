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

/**
 * @brief : implementation of Ledger
 * @file: Ledger.cpp
 * @author: yujiechen
 * @date: 2018-10-23
 */
#include "Ledger.h"
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libconfig/GlobalConfigure.h>
#include <libconsensus/pbft/PBFTEngine.h>
#include <libconsensus/pbft/PBFTSealer.h>
#include <libconsensus/raft/RaftEngine.h>
#include <libconsensus/raft/RaftSealer.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>
#include <boost/property_tree/ini_parser.hpp>

using namespace boost::property_tree;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::consensus;
using namespace dev::sync;
using namespace dev::precompiled;
using namespace std;

namespace dev
{
namespace ledger
{
bool Ledger::initLedger(std::shared_ptr<LedgerParamInterface> _ledgerParams)
{
#ifndef FISCO_EASYLOG
    BOOST_LOG_SCOPED_THREAD_ATTR(
        "GroupId", boost::log::attributes::constant<std::string>(std::to_string(m_groupId)));
#endif
    if (!_ledgerParams)
    {
        return false;
    }
    m_param = _ledgerParams;
    /// init dbInitializer
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("DBInitializer");
    m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param, m_groupId);
    m_dbInitializer->setChannelRPCServer(m_channelRPCServer);
    // m_dbInitializer
    if (!m_dbInitializer)
        return false;
    m_dbInitializer->initStorageDB();
    /// init the DB
    bool ret = initBlockChain(m_param->mutableGenesisBlockParam());
    if (!ret)
        return false;
    dev::h256 genesisHash = m_blockChain->getBlockByNumber(0)->headerHash();
    m_dbInitializer->initState(genesisHash);
    if (!m_dbInitializer->stateFactory())
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger")
                          << LOG_DESC("initBlockChain Failed for init stateFactory failed");
        return false;
    }
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockChain->setStateFactory(m_dbInitializer->stateFactory());
    /// init blockVerifier, txPool, sync and consensus
    return (initBlockVerifier() && initTxPool() && initSync() && consensusInitFactory() &&
            initEventLogFilterManager());
}

/// init txpool
bool Ledger::initTxPool()
{
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::TxPool);
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("initTxPool")
                      << LOG_KV("txPoolLimit", m_param->mutableTxPoolParam().txPoolLimit);
    if (!m_blockChain)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_DESC("initTxPool Failed");
        return false;
    }
    m_txPool = std::make_shared<dev::txpool::TxPool>(
        m_service, m_blockChain, protocol_id, m_param->mutableTxPoolParam().txPoolLimit);
    m_txPool->setMaxBlockLimit(g_BCOSConfig.c_blockLimit);
    m_txPool->start();
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_DESC("initTxPool SUCC");
    return true;
}

/// init blockVerifier
bool Ledger::initBlockVerifier()
{
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockVerifier");
    if (!m_blockChain || !m_dbInitializer->executiveContextFactory())
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockVerifier Failed");
        return false;
    }

    bool enableParallel = false;
    if (m_param->mutableTxParam().enableParallel)
    {
        enableParallel = true;
    }
    std::shared_ptr<BlockVerifier> blockVerifier = std::make_shared<BlockVerifier>(enableParallel);
    /// set params for blockverifier
    blockVerifier->setExecutiveContextFactory(m_dbInitializer->executiveContextFactory());
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockVerifier->setNumberHash(boost::bind(&BlockChainImp::numberHash, blockChain, _1));
    m_blockVerifier = blockVerifier;
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockVerifier SUCC");
    return true;
}

bool Ledger::initBlockChain(GenesisBlockParam& _genesisParam)
{
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockChain");
    if (!m_dbInitializer->storage())
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger")
                          << LOG_DESC("initBlockChain Failed for init storage failed");
        return false;
    }
    auto currenrBlockNumber = getBlockNumberFromStorage(m_dbInitializer->storage());
    bool shouldBuild = true;
    if (currenrBlockNumber != -1)
    {  // -1 means first init
        shouldBuild = false;
    }
    std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();

    // write the hex-string block data when use external or mysql
    if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "External") ||
        !dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "MySQL"))
    {
        Ledger_LOG(DEBUG) << LOG_DESC("set enableHexBlock to be true")
                          << LOG_KV("version", g_BCOSConfig.version())
                          << LOG_KV("storageType", m_param->mutableStorageParam().type);
        blockChain->setEnableHexBlock(true);
    }
    // >= v2.2.0
    else if (g_BCOSConfig.version() >= V2_2_0)
    {
        Ledger_LOG(DEBUG) << LOG_DESC("set enableHexBlock to be false")
                          << LOG_KV("version", g_BCOSConfig.version());
        blockChain->setEnableHexBlock(false);
    }
    // < v2.2.0
    else
    {
        Ledger_LOG(DEBUG) << LOG_DESC("set enableHexBlock to be true")
                          << LOG_KV("version", g_BCOSConfig.version());
        blockChain->setEnableHexBlock(true);
    }

    blockChain->setStateStorage(m_dbInitializer->storage());
    blockChain->setTableFactoryFactory(m_dbInitializer->tableFactoryFactory());
    m_blockChain = blockChain;
    bool ret = m_blockChain->checkAndBuildGenesisBlock(_genesisParam, shouldBuild);
    if (!ret)
    {
        /// It is a subsequent block without same extra data, so do reset.
        Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockChain")
                          << LOG_DESC("The configuration item will be reset");
        m_param->mutableConsensusParam().consensusType = _genesisParam.consensusType;
        if (g_BCOSConfig.version() <= RC2_VERSION)
        {
            m_param->mutableStorageParam().type = _genesisParam.storageType;
        }
        m_param->mutableStateParam().type = _genesisParam.stateType;
    }
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_DESC("initBlockChain SUCC");
    return true;
}

ConsensusInterface::Ptr Ledger::createConsensusEngine(dev::PROTOCOL_ID const& _protocolId)
{
    if (dev::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") == 0)
    {
        return std::make_shared<PBFTEngine>(m_service, m_txPool, m_blockChain, m_sync,
            m_blockVerifier, _protocolId, m_keyPair, m_param->mutableConsensusParam().sealerList);
    }
    return nullptr;
}

/**
 * @brief: create PBFTEngine
 * @param param: Ledger related params
 * @return std::shared_ptr<ConsensusInterface>: created consensus engine
 */
std::shared_ptr<Sealer> Ledger::createPBFTSealer()
{
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("createPBFTSealer");
    if (!m_txPool || !m_blockChain || !m_sync || !m_blockVerifier || !m_dbInitializer)
    {
        Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_DESC("createPBFTSealer Failed");
        return nullptr;
    }

    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::PBFT);
    /// create consensus engine according to "consensusType"
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("createPBFTSealer")
                      << LOG_KV("baseDir", m_param->baseDir()) << LOG_KV("Protocol", protocol_id);
    std::shared_ptr<PBFTSealer> pbftSealer =
        std::make_shared<PBFTSealer>(m_txPool, m_blockChain, m_sync);

    ConsensusInterface::Ptr pbftEngine = createConsensusEngine(protocol_id);
    if (!pbftEngine)
    {
        BOOST_THROW_EXCEPTION(dev::InitLedgerConfigFailed() << errinfo_comment(
                                  "create PBFTEngine failed, maybe unsupported consensus type " +
                                  m_param->mutableConsensusParam().consensusType));
    }
    pbftSealer->setConsensusEngine(pbftEngine);
    pbftSealer->setEnableDynamicBlockSize(m_param->mutableConsensusParam().enableDynamicBlockSize);
    pbftSealer->setBlockSizeIncreaseRatio(m_param->mutableConsensusParam().blockSizeIncreaseRatio);
    initPBFTEngine(pbftSealer);
    return pbftSealer;
}

void Ledger::initPBFTEngine(Sealer::Ptr _sealer)
{
    /// set params for PBFTEngine
    PBFTEngine::Ptr pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(_sealer->consensusEngine());
    /// set the range of block generation time
    pbftEngine->setEmptyBlockGenTime(g_BCOSConfig.c_intervalBlockTime);
    pbftEngine->setMinBlockGenerationTime(m_param->mutableConsensusParam().minBlockGenTime);

    pbftEngine->setOmitEmptyBlock(g_BCOSConfig.c_omitEmptyBlock);
    pbftEngine->setMaxTTL(m_param->mutableConsensusParam().maxTTL);
    pbftEngine->setBaseDir(m_param->baseDir());
}

std::shared_ptr<Sealer> Ledger::createRaftSealer()
{
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("createRaftSealer");
    if (!m_txPool || !m_blockChain || !m_sync || !m_blockVerifier || !m_dbInitializer)
    {
        Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_DESC("createRaftSealer Failed");
        return nullptr;
    }

    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::Raft);
    /// create consensus engine according to "consensusType"
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("createRaftSealer")
                      << LOG_KV("Protocol", protocol_id);
    // auto intervalBlockTime = g_BCOSConfig.c_intervalBlockTime;
    // std::shared_ptr<Sealer> raftSealer = std::make_shared<RaftSealer>(m_service, m_txPool,
    //    m_blockChain, m_sync, m_blockVerifier, m_keyPair, intervalBlockTime,
    //    intervalBlockTime + 1000, protocol_id, m_param->mutableConsensusParam().sealerList);
    std::shared_ptr<Sealer> raftSealer =
        std::make_shared<RaftSealer>(m_service, m_txPool, m_blockChain, m_sync, m_blockVerifier,
            m_keyPair, m_param->mutableConsensusParam().minElectTime,
            m_param->mutableConsensusParam().maxElectTime, protocol_id,
            m_param->mutableConsensusParam().sealerList);
    return raftSealer;
}

/// init consensus
bool Ledger::consensusInitFactory()
{
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("consensusInitFactory");
    if (dev::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "raft") == 0)
    {
        /// create RaftSealer
        m_sealer = createRaftSealer();
        if (!m_sealer)
        {
            return false;
        }
        return true;
    }

    if (dev::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") != 0)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger")
                          << LOG_KV("UnsupportConsensusType",
                                 m_param->mutableConsensusParam().consensusType)
                          << " use PBFT as default";
    }

    /// create PBFTSealer
    m_sealer = createPBFTSealer();
    if (!m_sealer)
    {
        return false;
    }
    return true;
}

/// init sync
bool Ledger::initSync()
{
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_BADGE("initSync")
                      << LOG_KV("idleWaitMs", m_param->mutableSyncParam().idleWaitMs)
                      << LOG_KV("gossipInterval", m_param->mutableSyncParam().gossipInterval)
                      << LOG_KV("gossipPeers", m_param->mutableSyncParam().gossipPeers);
    if (!m_txPool || !m_blockChain || !m_blockVerifier)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_DESC("#initSync Failed");
        return false;
    }
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    dev::h256 genesisHash = m_blockChain->getBlockByNumber(int64_t(0))->headerHash();
    m_sync = std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain, m_blockVerifier,
        protocol_id, m_keyPair.pub(), genesisHash, m_param->mutableSyncParam().idleWaitMs,
        m_param->mutableSyncParam().gossipInterval, m_param->mutableSyncParam().gossipPeers,
        m_param->mutableSyncParam().enableSendBlockStatusByTree,
        m_param->mutableSyncParam().syncTreeWidth);
    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_DESC("initSync SUCC");
    return true;
}

// init EventLogFilterManager
bool Ledger::initEventLogFilterManager()
{
    if (!m_blockChain)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initEventLogFilterManager")
                          << LOG_DESC("blockChain not exist.");
        return false;
    }

    m_eventLogFilterManger = std::make_shared<event::EventLogFilterManager>(m_blockChain,
        m_param->mutableEventLogFilterManagerParams().maxBlockRange,
        m_param->mutableEventLogFilterManagerParams().maxBlockPerProcess);

    Ledger_LOG(DEBUG) << LOG_BADGE("initEventLogFilterManager")
                      << LOG_DESC("initEventLogFilterManager SUCC");
    return true;
}
}  // namespace ledger
}  // namespace dev
