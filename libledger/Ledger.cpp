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
#include <libconsensus/rotating_pbft/RotatingPBFTEngine.h>
#include <libconsensus/rotating_pbft/vrf_rpbft/VRFBasedrPBFTEngine.h>
#include <libconsensus/rotating_pbft/vrf_rpbft/VRFBasedrPBFTSealer.h>
#include <libflowlimit/RateLimiter.h>
#include <libnetwork/PeerWhitelist.h>
#include <libsync/SyncMaster.h>
#include <libsync/SyncMsgPacketFactory.h>
#include <libtxpool/TxPool.h>
#include <boost/property_tree/ini_parser.hpp>

using namespace boost::property_tree;
using namespace bcos::blockverifier;
using namespace bcos::blockchain;
using namespace bcos::consensus;
using namespace bcos::sync;
using namespace bcos::precompiled;
using namespace std;

namespace bcos
{
namespace ledger
{
bool Ledger::initLedger(std::shared_ptr<LedgerParamInterface> _ledgerParams)
{
    BOOST_LOG_SCOPED_THREAD_ATTR(
        "GroupId", boost::log::attributes::constant<std::string>(std::to_string(m_groupId)));
    if (!_ledgerParams)
    {
        return false;
    }
    m_param = _ledgerParams;
    /// init dbInitializer
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("DBInitializer");
    m_dbInitializer = std::make_shared<bcos::ledger::DBInitializer>(m_param, m_groupId);
    m_dbInitializer->setChannelRPCServer(m_channelRPCServer);

    setSDKAllowList(m_param->mutablePermissionParam().sdkAllowList);

    // m_dbInitializer
    if (!m_dbInitializer)
        return false;
    m_dbInitializer->initStorageDB();
    /// init the DB
    bool ret = initBlockChain();
    if (!ret)
        return false;
    m_dbInitializer->initState();
    if (!m_dbInitializer->stateFactory())
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger")
                          << LOG_DESC("initBlockChain Failed for init stateFactory failed");
        return false;
    }
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockChain->setStateFactory(m_dbInitializer->stateFactory());
    // setSyncNum for cachedStorage
    m_dbInitializer->setSyncNumForCachedStorage(m_blockChain->number());

    // the network statistic has been enabled
    if (g_BCOSConfig.enableStat())
    {
        // init network statistic handler
        initNetworkStatHandler();
    }

    auto channelRPCServer = std::weak_ptr<bcos::ChannelRPCServer>(m_channelRPCServer);
    m_handler = blockChain->onReady([this, channelRPCServer](int64_t number) {
        LOG(INFO) << "Push block notify: " << std::to_string(m_groupId) << "-" << number;
        auto channelRpcServer = channelRPCServer.lock();
        if (channelRpcServer)
        {
            channelRpcServer->blockNotify(m_groupId, number);
        }
    });

    initNetworkBandWidthLimiter();
    initQPSLimit();
    /// init blockVerifier, txPool, sync and consensus
    return (initBlockVerifier() && initTxPool() && initSync() && consensusInitFactory() &&
            initEventLogFilterManager());
}

void Ledger::reloadSDKAllowList()
{
    // Note: here must catch the exception in case of sdk allowlist reload failed
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(m_param->iniConfigPath(), pt);
        bcos::h512s sdkAllowList;
        m_param->parseSDKAllowList(sdkAllowList, pt);
        setSDKAllowList(sdkAllowList);
        Ledger_LOG(INFO) << LOG_DESC("reloadSDKAllowList")
                         << LOG_KV("config", m_param->iniConfigPath())
                         << LOG_KV("allowListSize", sdkAllowList.size());
    }
    catch (std::exception const& e)
    {
        Ledger_LOG(ERROR) << LOG_DESC("reloadSDKAllowList failed")
                          << LOG_KV("EINFO", boost::diagnostic_information(e));
    }
}

void Ledger::setSDKAllowList(bcos::h512s const& _sdkList)
{
    Ledger_LOG(INFO) << LOG_DESC("setSDKAllowList") << LOG_KV("groupId", m_groupId)
                     << LOG_KV("sdkAllowListSize", _sdkList.size());
    PeerWhitelist::Ptr sdkAllowList = std::make_shared<PeerWhitelist>(_sdkList, true);
    if (m_channelRPCServer)
    {
        m_channelRPCServer->registerSDKAllowListByGroupId(m_groupId, sdkAllowList);
    }
}

void Ledger::initNetworkStatHandler()
{
    Ledger_LOG(INFO) << LOG_BADGE("initNetworkStatHandler");
    m_networkStatHandler = std::make_shared<bcos::stat::NetworkStatHandler>();
    m_networkStatHandler->setGroupId(m_groupId);
    m_networkStatHandler->setConsensusMsgType(m_param->mutableConsensusParam().consensusType);
    m_service->appendNetworkStatHandlerByGroupID(m_groupId, m_networkStatHandler);
    if (!m_channelRPCServer)
    {
        return;
    }
    if (!m_channelRPCServer->networkStatHandler())
    {
        return;
    }
    m_channelRPCServer->networkStatHandler()->appendGroupP2PStatHandler(
        m_groupId, m_networkStatHandler);
    // set channelNetworkStatHandler
    m_service->setChannelNetworkStatHandler(m_channelRPCServer->networkStatHandler());
}

void Ledger::initNetworkBandWidthLimiter()
{
    // Default: disable network bandwidth limit
    if (m_param->mutableFlowControlParam().outGoingBandwidthLimit ==
        m_param->mutableFlowControlParam().maxDefaultValue)
    {
        Ledger_LOG(INFO)
            << LOG_BADGE("initNetworkBandWidthLimiter: outGoingBandwidthLimit has been disabled")
            << LOG_KV("groupId", m_groupId);
        return;
    }

    m_networkBandwidthLimiter = std::make_shared<bcos::flowlimit::RateLimiter>(
        m_param->mutableFlowControlParam().outGoingBandwidthLimit);
    m_networkBandwidthLimiter->setMaxPermitsSize(g_BCOSConfig.c_maxPermitsSize);
    if (m_service)
    {
        m_service->registerGroupBandwidthLimiter(m_groupId, m_networkBandwidthLimiter);
    }
    if (m_channelRPCServer && m_channelRPCServer->networkBandwidthLimiter())
    {
        m_service->setNodeBandwidthLimiter(m_channelRPCServer->networkBandwidthLimiter());
    }
    Ledger_LOG(INFO) << LOG_BADGE("initNetworkBandWidthLimiter")
                     << LOG_KV("outGoingBandwidthLimit",
                            m_param->mutableFlowControlParam().outGoingBandwidthLimit)
                     << LOG_KV("groupId", m_groupId);
}

void Ledger::initQPSLimit()
{
    // Default: disable QPS limitation
    if (m_param->mutableFlowControlParam().maxQPS ==
        m_param->mutableFlowControlParam().maxDefaultValue)
    {
        Ledger_LOG(INFO) << LOG_BADGE("QPSLimit has been disabled") << LOG_KV("groupId", m_groupId);
        return;
    }
    auto rpcQPSLimiter = m_channelRPCServer->qpsLimiter();
    if (!rpcQPSLimiter)
    {
        Ledger_LOG(INFO) << LOG_BADGE("Disable QPSLimit") << LOG_KV("groupId", m_groupId);
        return;
    }
    auto qpsLimiter =
        std::make_shared<bcos::flowlimit::RateLimiter>(m_param->mutableFlowControlParam().maxQPS);
    // register QPS
    rpcQPSLimiter->registerQPSLimiterByGroupID(m_groupId, qpsLimiter);
    Ledger_LOG(INFO) << LOG_BADGE("initQPSLimit") << LOG_KV("groupId", m_groupId)
                     << LOG_KV("qpsLimiter", m_param->mutableFlowControlParam().maxQPS);
}

/// init txpool
bool Ledger::initTxPool()
{
    bcos::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::TxPool);
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("initTxPool")
                     << LOG_KV("txPoolLimit", m_param->mutableTxPoolParam().txPoolLimit);
    if (!m_blockChain)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_DESC("initTxPool Failed");
        return false;
    }
    auto txPool = std::make_shared<bcos::txpool::TxPool>(m_service, m_blockChain, protocol_id,
        m_param->mutableTxPoolParam().txPoolLimit, m_param->mutableTxPoolParam().notifyWorkerNum);
    txPool->setMaxBlockLimit(g_BCOSConfig.c_blockLimit);
    txPool->setMaxMemoryLimit(m_param->mutableTxPoolParam().maxTxPoolMemorySize);
    m_txPool = txPool;
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_DESC("initTxPool SUCC");
    return true;
}

/// init blockVerifier
bool Ledger::initBlockVerifier()
{
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockVerifier");
    if (!m_blockChain || !m_dbInitializer->executiveContextFactory())
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockVerifier Failed");
        return false;
    }

    std::shared_ptr<BlockVerifier> blockVerifier =
        std::make_shared<BlockVerifier>(m_param->mutableTxParam().enableParallel);
    /// set params for blockverifier
    blockVerifier->setExecutiveContextFactory(m_dbInitializer->executiveContextFactory());
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockVerifier->setNumberHash(boost::bind(&BlockChainImp::numberHash, blockChain, _1));
    blockVerifier->setEvmFlags(m_param->mutableGenesisParam().evmFlags);

    m_blockVerifier = blockVerifier;
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockVerifier SUCC")
                     << LOG_KV("evmFlags", m_param->mutableGenesisParam().evmFlags);
    return true;
}

bool Ledger::initBlockChain()
{
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("initBlockChain");
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
    if (!bcos::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "External") ||
        !bcos::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "MySQL"))
    {
        if (g_BCOSConfig.version() < V2_6_0)
        {
            Ledger_LOG(INFO) << LOG_DESC("set enableHexBlock to be true");
            blockChain->setEnableHexBlock(true);
        }
        // supported_version >= v2.6.0, store block and nonce in bytes in mysql
        else
        {
            blockChain->setEnableHexBlock(false);
        }
        Ledger_LOG(INFO) << LOG_DESC("initBlockChain") << LOG_KV("version", g_BCOSConfig.version())
                         << LOG_KV("storageType", m_param->mutableStorageParam().type);
    }
    // >= v2.2.0
    else if (g_BCOSConfig.version() >= V2_2_0)
    {
        Ledger_LOG(INFO) << LOG_DESC("set enableHexBlock to be false")
                         << LOG_KV("version", g_BCOSConfig.version());
        blockChain->setEnableHexBlock(false);
    }
    // < v2.2.0
    else
    {
        Ledger_LOG(INFO) << LOG_DESC("set enableHexBlock to be true")
                         << LOG_KV("version", g_BCOSConfig.version());
        blockChain->setEnableHexBlock(true);
    }

    blockChain->setStateStorage(m_dbInitializer->storage());
    blockChain->setTableFactoryFactory(m_dbInitializer->tableFactoryFactory());

    m_blockChain = blockChain;
    m_blockChain->checkAndBuildGenesisBlock(m_param, shouldBuild);
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_DESC("initBlockChain SUCC");
    return true;
}

ConsensusInterface::Ptr Ledger::createConsensusEngine(bcos::PROTOCOL_ID const& _protocolId)
{
    if (bcos::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") == 0)
    {
        Ledger_LOG(INFO) << LOG_DESC("createConsensusEngine: create PBFTEngine");
        return std::make_shared<PBFTEngine>(m_service, m_txPool, m_blockChain, m_sync,
            m_blockVerifier, _protocolId, m_keyPair, m_param->mutableConsensusParam().sealerList);
    }
    if (normalrPBFTEnabled())
    {
        Ledger_LOG(INFO) << LOG_DESC("createConsensusEngine: create RotatingPBFTEngine");
        return std::make_shared<RotatingPBFTEngine>(m_service, m_txPool, m_blockChain, m_sync,
            m_blockVerifier, _protocolId, m_keyPair, m_param->mutableConsensusParam().sealerList);
    }
    if (vrfBasedrPBFTEnabled())
    {
        // Note: since WorkingSealerManagerPrecompiled is enabled after v2.6.0,
        //       vrf based rpbft is supported after v2.6.0
        if (g_BCOSConfig.version() >= V2_6_0)
        {
            Ledger_LOG(INFO) << LOG_DESC("createConsensusEngine: create VRFBasedrPBFTEngine");
            return std::make_shared<VRFBasedrPBFTEngine>(m_service, m_txPool, m_blockChain, m_sync,
                m_blockVerifier, _protocolId, m_keyPair,
                m_param->mutableConsensusParam().sealerList);
        }
        else
        {
            BOOST_THROW_EXCEPTION(bcos::InitLedgerConfigFailed() << errinfo_comment(
                                      m_param->mutableConsensusParam().consensusType +
                                      " is supported after when supported_version >= v2.6.0!"));
        }
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
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("createPBFTSealer");
    if (!m_txPool || !m_blockChain || !m_sync || !m_blockVerifier || !m_dbInitializer)
    {
        Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_DESC("createPBFTSealer Failed");
        return nullptr;
    }

    bcos::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::PBFT);
    /// create consensus engine according to "consensusType"
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("createPBFTSealer")
                     << LOG_KV("baseDir", m_param->baseDir()) << LOG_KV("Protocol", protocol_id);
    std::shared_ptr<PBFTSealer> pbftSealer;
    if (vrfBasedrPBFTEnabled())
    {
        pbftSealer = std::make_shared<VRFBasedrPBFTSealer>(m_txPool, m_blockChain, m_sync);
        Ledger_LOG(INFO) << LOG_BADGE("initLedger")
                         << LOG_DESC("createPBFTSealer for VRF-based rPBFT")
                         << LOG_KV("consensusType", m_param->mutableConsensusParam().consensusType);
    }
    else
    {
        pbftSealer = std::make_shared<PBFTSealer>(m_txPool, m_blockChain, m_sync);
        Ledger_LOG(INFO) << LOG_BADGE("initLedger")
                         << LOG_DESC("createPBFTSealer for PBFT or rPBFT")
                         << LOG_KV("consensusType", m_param->mutableConsensusParam().consensusType);
    }

    ConsensusInterface::Ptr pbftEngine = createConsensusEngine(protocol_id);
    if (!pbftEngine)
    {
        BOOST_THROW_EXCEPTION(bcos::InitLedgerConfigFailed() << errinfo_comment(
                                  "create PBFTEngine failed, maybe unsupported consensus type " +
                                  m_param->mutableConsensusParam().consensusType));
    }
    // only when supported_version>=v2.6.0, support adjust consensus interval at runtime
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        pbftEngine->setSupportConsensusTimeAdjust(true);
    }
    else
    {
        pbftEngine->setSupportConsensusTimeAdjust(false);
    }
    pbftSealer->setConsensusEngine(pbftEngine);
    pbftSealer->setEnableDynamicBlockSize(m_param->mutableConsensusParam().enableDynamicBlockSize);
    pbftSealer->setBlockSizeIncreaseRatio(m_param->mutableConsensusParam().blockSizeIncreaseRatio);
    initPBFTEngine(pbftSealer);
    initrPBFTEngine(pbftSealer);
    return pbftSealer;
}

bcos::eth::BlockFactory::Ptr Ledger::createBlockFactory()
{
    if (!m_param->mutableConsensusParam().enablePrepareWithTxsHash)
    {
        return std::make_shared<bcos::eth::BlockFactory>();
    }
    // only create PartiallyBlockFactory when using pbft or rpbft
    if (bcos::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") == 0 ||
        normalrPBFTEnabled() || vrfBasedrPBFTEnabled())
    {
        return std::make_shared<bcos::eth::PartiallyBlockFactory>();
    }
    return std::make_shared<bcos::eth::BlockFactory>();
}

void Ledger::initPBFTEngine(Sealer::Ptr _sealer)
{
    /// set params for PBFTEngine
    PBFTEngine::Ptr pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(_sealer->consensusEngine());

    // set the range of block generation time
    // supported_version < v2.6.0, the consensus time is c_intervalBlockTime
    pbftEngine->setEmptyBlockGenTime(g_BCOSConfig.c_intervalBlockTime);
    pbftEngine->setMinBlockGenerationTime(m_param->mutableConsensusParam().minBlockGenTime);
    pbftEngine->setOmitEmptyBlock(g_BCOSConfig.c_omitEmptyBlock);
    pbftEngine->setMaxTTL(m_param->mutableConsensusParam().maxTTL);
    pbftEngine->setBaseDir(m_param->baseDir());
    pbftEngine->setEnableTTLOptimize(m_param->mutableConsensusParam().enableTTLOptimize);
    pbftEngine->setEnablePrepareWithTxsHash(
        m_param->mutableConsensusParam().enablePrepareWithTxsHash);
}

// init rotating-pbft engine
void Ledger::initrPBFTEngine(bcos::consensus::Sealer::Ptr _sealer)
{
    if (!normalrPBFTEnabled() && !vrfBasedrPBFTEnabled())
    {
        return;
    }
    RotatingPBFTEngine::Ptr rotatingPBFT =
        std::dynamic_pointer_cast<RotatingPBFTEngine>(_sealer->consensusEngine());
    rotatingPBFT->setMaxRequestMissedTxsWaitTime(
        m_param->mutableConsensusParam().maxRequestMissedTxsWaitTime);
    rotatingPBFT->setMaxRequestPrepareWaitTime(
        m_param->mutableConsensusParam().maxRequestPrepareWaitTime);
    if (m_param->mutableConsensusParam().broadcastPrepareByTree)
    {
        rotatingPBFT->createTreeTopology(m_param->mutableConsensusParam().treeWidth);
        rotatingPBFT->setPrepareStatusBroadcastPercent(
            m_param->mutableConsensusParam().prepareStatusBroadcastPercent);
        Ledger_LOG(INFO) << LOG_DESC("createTreeTopology")
                         << LOG_KV("treeWidth", m_param->mutableConsensusParam().treeWidth);
    }
}

std::shared_ptr<Sealer> Ledger::createRaftSealer()
{
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("createRaftSealer");
    if (!m_txPool || !m_blockChain || !m_sync || !m_blockVerifier || !m_dbInitializer)
    {
        Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_DESC("createRaftSealer Failed");
        return nullptr;
    }

    bcos::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::Raft);
    /// create consensus engine according to "consensusType"
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("createRaftSealer")
                     << LOG_KV("Protocol", protocol_id);
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
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("consensusInitFactory");
    // create RaftSealer
    if (bcos::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "raft") == 0)
    {
        m_sealer = createRaftSealer();
    }
    // create PBFTSealer
    else if (bcos::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") ==
                 0 ||
             normalrPBFTEnabled() || vrfBasedrPBFTEnabled())
    {
        m_sealer = createPBFTSealer();
    }
    else
    {
        BOOST_THROW_EXCEPTION(
            bcos::InitLedgerConfigFailed()
            << errinfo_comment("create consensusEngine failed, maybe unsupported consensus type " +
                               m_param->mutableConsensusParam().consensusType +
                               ", supported consensus type are pbft, raft, rpbft"));
    }
    if (!m_sealer)
    {
        BOOST_THROW_EXCEPTION(
            bcos::InitLedgerConfigFailed() << errinfo_comment("create sealer failed"));
    }
    // set nodeTimeMaintenance
    m_sealer->consensusEngine()->setNodeTimeMaintenance(m_nodeTimeMaintenance);
    // create blockFactory
    auto blockFactory = createBlockFactory();
    m_sealer->setBlockFactory(blockFactory);
    return true;
}

/// init sync
bool Ledger::initSync()
{
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("initSync")
                     << LOG_KV("idleWaitMs", m_param->mutableSyncParam().idleWaitMs)
                     << LOG_KV("gossipInterval", m_param->mutableSyncParam().gossipInterval)
                     << LOG_KV("gossipPeers", m_param->mutableSyncParam().gossipPeers);
    if (!m_txPool || !m_blockChain || !m_blockVerifier)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_DESC("#initSync Failed");
        return false;
    }
    // raft disable enableSendBlockStatusByTree
    bool enableSendBlockStatusByTree = m_param->mutableSyncParam().enableSendBlockStatusByTree;
    bool enableSendTxsByTree = m_param->mutableSyncParam().enableSendTxsByTree;
    if (bcos::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "raft") == 0)
    {
        Ledger_LOG(INFO) << LOG_DESC("initLedger: disable send_by_tree when use raft");
        enableSendBlockStatusByTree = false;
        enableSendTxsByTree = false;
    }
    Ledger_LOG(INFO) << LOG_DESC("Init sync master")
                     << LOG_KV("enableSendBlockStatusByTree", enableSendBlockStatusByTree)
                     << LOG_KV("enableSendTxsByTree", enableSendTxsByTree);

    bcos::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    bcos::h256 genesisHash = m_blockChain->getBlockByNumber(int64_t(0))->headerHash();
    auto syncMaster = std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain,
        m_blockVerifier, protocol_id, m_keyPair.pub(), genesisHash,
        m_param->mutableSyncParam().idleWaitMs, m_param->mutableSyncParam().gossipInterval,
        m_param->mutableSyncParam().gossipPeers, enableSendTxsByTree, enableSendBlockStatusByTree,
        m_param->mutableSyncParam().syncTreeWidth);

    // create and setSyncMsgPacketFactory
    SyncMsgPacketFactory::Ptr syncMsgPacketFactory;
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        syncMsgPacketFactory = std::make_shared<SyncMsgPacketWithAlignedTimeFactory>();
        // create NodeTimeMaintenance
        m_nodeTimeMaintenance = std::make_shared<NodeTimeMaintenance>();
        syncMaster->setNodeTimeMaintenance(m_nodeTimeMaintenance);
    }
    else
    {
        syncMsgPacketFactory = std::make_shared<SyncMsgPacketFactory>();
    }
    syncMaster->setSyncMsgPacketFactory(syncMsgPacketFactory);

    // set the max block queue size for sync module(bytes)
    syncMaster->setMaxBlockQueueSize(m_param->mutableSyncParam().maxQueueSizeForBlockSync);
    syncMaster->setTxsStatusGossipMaxPeers(m_param->mutableSyncParam().txsStatusGossipMaxPeers);
    // set networkBandwidthLimiter
    if (m_networkBandwidthLimiter)
    {
        syncMaster->setBandwidthLimiter(m_networkBandwidthLimiter);
    }
    if (m_channelRPCServer && m_channelRPCServer->networkBandwidthLimiter())
    {
        syncMaster->setNodeBandwidthLimiter(m_channelRPCServer->networkBandwidthLimiter());
    }

    m_sync = syncMaster;
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_DESC("initSync SUCC");
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

    Ledger_LOG(INFO) << LOG_BADGE("initEventLogFilterManager")
                     << LOG_DESC("initEventLogFilterManager SUCC");
    return true;
}
}  // namespace ledger
}  // namespace bcos
