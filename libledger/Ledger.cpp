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
#include <libconfig/SystemConfigMgr.h>
#include <libconsensus/pbft/PBFTEngine.h>
#include <libconsensus/pbft/PBFTSealer.h>
#include <libconsensus/raft/RaftEngine.h>
#include <libconsensus/raft/RaftSealer.h>
#include <libdevcore/OverlayDB.h>
#include <libdevcore/easylog.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ini_parser.hpp>
using namespace boost::property_tree;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::consensus;
using namespace dev::sync;
using namespace dev::config;
namespace dev
{
namespace ledger
{
bool Ledger::initLedger()
{
    if (!m_param)
        return false;
    /// init dbInitializer
    Ledger_LOG(INFO) << "[#initLedger] [DBInitializer]" << std::endl;
    m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param);
    if (!m_dbInitializer)
        return false;
    m_dbInitializer->initStorageDB();
    /// init the DB
    bool ret = initBlockChain();
    if (!ret)
        return false;
    dev::h256 genesisHash = m_blockChain->getBlockByNumber(0)->headerHash();
    m_dbInitializer->initStateDB(genesisHash);
    if (!m_dbInitializer->stateFactory())
    {
        Ledger_LOG(ERROR) << "#[initLedger] [#initBlockChain Failed for init stateFactory failed]"
                          << std::endl;
        return false;
    }
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockChain->setStateFactory(m_dbInitializer->stateFactory());
    /// init blockVerifier, txPool, sync and consensus
    return (initBlockVerifier() && initTxPool() && initSync() && consensusInitFactory());
}

/**
 * @brief: init configuration related to the ledger with specified configuration file
 * @param configPath: the path of the config file
 */
void Ledger::initConfig(std::string const& configPath)
{
    try
    {
        Ledger_LOG(INFO) << "[#initConfig] [initConsensusConfig/initDBConfig/initTxConfig]";
        ptree pt;
        /// read the configuration file for a specified group
        read_ini(configPath, pt);
        /// init params related to consensus
        initConsensusConfig(pt);
        /// db params initialization
        initDBConfig(pt);
        /// init params related to tx
        initTxConfig(pt);
    }
    catch (std::exception& e)
    {
        std::string error_info = "init genesis config failed for " + toString(m_groupId) +
                                 " failed, error_msg: " + boost::diagnostic_information(e);
        LOG(ERROR) << error_info;
        Ledger_LOG(ERROR) << "[#initConfig Failed] [EINFO]:  " << boost::diagnostic_information(e)
                          << std::endl;
        BOOST_THROW_EXCEPTION(dev::InitLedgerConfigFailed() << errinfo_comment(error_info));
        exit(1);
    }
}

void Ledger::initIniConfig(std::string const& iniConfigFileName)
{
    try
    {
        Ledger_LOG(INFO) << "[#initIniConfig] [initTxPoolConfig/initSyncConfig] fileName:"
                         << iniConfigFileName;
        ptree pt;
        /// read the configuration file for a specified group
        read_ini(iniConfigFileName, pt);
        /// init params related to txpool
        initTxPoolConfig(pt);
        /// init params related to sync
        initSyncConfig(pt);
    }
    catch (std::exception& e)
    {
        std::string error_info = "init ini config failed for " + toString(m_groupId) +
                                 " failed, error_msg: " + boost::diagnostic_information(e);
        LOG(ERROR) << error_info;
        Ledger_LOG(ERROR) << "[#initConfig Failed] [EINFO]:  " << boost::diagnostic_information(e)
                          << std::endl;
    }
}

void Ledger::initTxPoolConfig(ptree const& pt)
{
    m_param->mutableTxPoolParam().txPoolLimit = pt.get<uint64_t>("txPool.limit", 102400);
    Ledger_LOG(DEBUG) << "[#initTxPoolConfig] [limit]:  "
                      << m_param->mutableTxPoolParam().txPoolLimit << std::endl;
}

/// init consensus configurations:
/// 1. consensusType: current support pbft only (default is pbft)
/// 2. maxTransNum: max number of transactions can be sealed into a block
/// 3. intervalBlockTime: average block generation period
/// 4. miner.${idx}: define the node id of every miner related to the group
void Ledger::initConsensusConfig(ptree const& pt)
{
    m_param->mutableConsensusParam().consensusType =
        pt.get<std::string>("consensus.consensusType", "pbft");

    m_param->mutableConsensusParam().maxTransactions =
        pt.get<uint64_t>("consensus.maxTransNum", 1000);
    m_param->mutableConsensusParam().maxTTL = pt.get<uint8_t>("consensus.maxTTL", MAXTTL);

    m_param->mutableConsensusParam().minElectTime =
        pt.get<uint64_t>("consensus.minElectTime", 1000);
    m_param->mutableConsensusParam().maxElectTime =
        pt.get<uint64_t>("consensus.maxElectTime", 2000);

    Ledger_LOG(DEBUG) << "[#initConsensusConfig] [type/maxTxNum/maxTTL]:  "
                      << m_param->mutableConsensusParam().consensusType << "/"
                      << m_param->mutableConsensusParam().maxTransactions << "/"
                      << std::to_string(m_param->mutableConsensusParam().maxTTL);

    std::stringstream nodeListMark;
    try
    {
        for (auto it : pt.get_child("consensus"))
        {
            if (it.first.find("node.") == 0)
            {
                std::string data = it.second.data();
                boost::to_lower(data);
                Ledger_LOG(INFO) << "[#initConsensusConfig] [consensus_node_key]: " << it.first
                                 << " [node]: " << data << std::endl;
                // Uniform lowercase nodeID
                dev::h512 nodeID(data);
                m_param->mutableConsensusParam().minerList.push_back(nodeID);
                // The full output node ID is required.
                nodeListMark << data << ",";
            }
        }
    }
    catch (std::exception& e)
    {
        Ledger_LOG(ERROR) << "[#initConsensusConfig]: Parse consensus section failed: "
                          << boost::diagnostic_information(e) << std::endl;
    }
    m_param->mutableGenesisParam().nodeListMark = nodeListMark.str();
}

/// init sync related configurations
/// 1. idleWaitMs: default is 30ms
void Ledger::initSyncConfig(ptree const& pt)
{
    m_param->mutableSyncParam().idleWaitMs =
        pt.get<unsigned>("sync.idleWaitMs", SYNC_IDLE_WAIT_DEFAULT);
    Ledger_LOG(DEBUG) << "[#initSyncConfig] [idleWaitMs]:" << m_param->mutableSyncParam().idleWaitMs
                      << std::endl;
}

/// init db related configurations:
/// dbType: leveldb/AMDB, storage type, default is "AMDB"
/// mpt: true/false, enable mpt or not, default is true
/// dbpath: data to place all data of the group, default is "data"
void Ledger::initDBConfig(ptree const& pt)
{
    /// init the basic config
    /// set storage db related param
    m_param->mutableStorageParam().type = pt.get<std::string>("storage.type", "LevelDB");
    m_param->mutableStorageParam().path = m_param->baseDir() + "/block";
    /// set state db related param
    m_param->mutableStateParam().type = pt.get<std::string>("state.type", "mpt");

    Ledger_LOG(DEBUG) << "[#initDBConfig] [storageDB/storagePath/stateDB/baseDir]:  "
                      << m_param->mutableStorageParam().type << "/"
                      << m_param->mutableStorageParam().path << "/" << m_param->baseDir()
                      << std::endl;
}

/// init tx related configurations
/// 1. gasLimit: default is 300000000
void Ledger::initTxConfig(boost::property_tree::ptree const& pt)
{
    m_param->mutableTxParam().txGasLimit = pt.get<unsigned>("tx.gasLimit", 300000000);
    Ledger_LOG(DEBUG) << "[#initTxConfig] [txGasLimit]:" << m_param->mutableTxParam().txGasLimit;
}

/// init mark of this group
void Ledger::initMark()
{
    std::stringstream s;
    s << int(m_groupId) << "-";
    s << m_param->mutableGenesisParam().nodeListMark << "-";
    s << m_param->mutableConsensusParam().consensusType << "-";
    s << m_param->mutableStorageParam().type << "-";
    s << m_param->mutableStateParam().type << "-";
    s << m_param->mutableConsensusParam().maxTransactions << "-";
    s << m_param->mutableTxParam().txGasLimit;
    m_param->mutableGenesisParam().genesisMark = s.str();
    Ledger_LOG(DEBUG) << "[#initMark] [genesisMark]:  "
                      << m_param->mutableGenesisParam().genesisMark << std::endl;
}

/// init txpool
bool Ledger::initTxPool()
{
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::TxPool);
    Ledger_LOG(DEBUG) << "[#initLedger] [#initTxPool] [Protocol ID]:  " << protocol_id << std::endl;
    if (!m_blockChain)
    {
        Ledger_LOG(ERROR) << "[#initLedger] [#initTxPool Failed]" << std::endl;
        return false;
    }
    m_txPool = std::make_shared<dev::txpool::TxPool>(
        m_service, m_blockChain, protocol_id, m_param->mutableTxPoolParam().txPoolLimit);
    m_txPool->setMaxBlockLimit(SystemConfigMgr::c_blockLimit);
    Ledger_LOG(DEBUG) << "[#initLedger] [#initTxPool SUCC] [Protocol ID]:  " << protocol_id
                      << std::endl;
    return true;
}

/// init blockVerifier
bool Ledger::initBlockVerifier()
{
    Ledger_LOG(DEBUG) << "[#initLedger] [#initBlockVerifier]" << std::endl;
    if (!m_blockChain || !m_dbInitializer->executiveContextFactory())
    {
        Ledger_LOG(ERROR) << "[#initLedger] [#initBlockVerifier Failed]" << std::endl;
        return false;
    }
    std::shared_ptr<BlockVerifier> blockVerifier = std::make_shared<BlockVerifier>();
    /// set params for blockverifier
    blockVerifier->setExecutiveContextFactory(m_dbInitializer->executiveContextFactory());
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockVerifier->setNumberHash(boost::bind(&BlockChainImp::numberHash, blockChain, _1));
    m_blockVerifier = blockVerifier;
    Ledger_LOG(DEBUG) << "[#initLedger] [#initBlockVerifier SUCC]" << std::endl;
    return true;
}

bool Ledger::initBlockChain()
{
    Ledger_LOG(DEBUG) << "[#initLedger] [#initBlockChain]" << std::endl;
    if (!m_dbInitializer->storage())
    {
        Ledger_LOG(ERROR) << "[#initLedger] [#initBlockChain Failed for init storage failed]"
                          << std::endl;
        return false;
    }
    std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();
    blockChain->setStateStorage(m_dbInitializer->storage());
    m_blockChain = blockChain;
    std::string consensusType = m_param->mutableConsensusParam().consensusType;
    std::string storageType = m_param->mutableStorageParam().type;
    std::string stateType = m_param->mutableStateParam().type;
    GenesisBlockParam initParam = {m_param->mutableGenesisParam().genesisMark,
        m_param->mutableConsensusParam().minerList, m_param->mutableConsensusParam().observerList,
        consensusType, storageType, stateType, m_param->mutableConsensusParam().maxTransactions,
        m_param->mutableTxParam().txGasLimit};
    bool ret = m_blockChain->checkAndBuildGenesisBlock(initParam);
    if (!ret)
    {
        /// It is a subsequent block without same extra data, so do reset.
        Ledger_LOG(DEBUG)
            << "[#initLedger] [#initBlockChain] The configuration item will be reset.";
        m_param->mutableConsensusParam().consensusType = initParam.consensusType;
        m_param->mutableStorageParam().type = initParam.storageType;
        m_param->mutableStateParam().type = initParam.stateType;
    }
    Ledger_LOG(DEBUG) << "[#initLedger] [#initBlockChain SUCC]";
    return true;
}

/**
 * @brief: create PBFTEngine
 * @param param: Ledger related params
 * @return std::shared_ptr<ConsensusInterface>: created consensus engine
 */
std::shared_ptr<Sealer> Ledger::createPBFTSealer()
{
    Ledger_LOG(DEBUG) << "[#initLedger] [#createPBFTSealer]" << std::endl;
    if (!m_txPool || !m_blockChain || !m_sync || !m_blockVerifier || !m_dbInitializer)
    {
        Ledger_LOG(DEBUG) << "[#initLedger] [#createPBFTSealer Failed]" << std::endl;
        return nullptr;
    }

    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::PBFT);
    /// create consensus engine according to "consensusType"
    Ledger_LOG(DEBUG) << "[#initLedger] [#createPBFTSealer] [baseDir/Protocol ID]:  "
                      << m_param->baseDir() << "/" << protocol_id << std::endl;
    std::shared_ptr<Sealer> pbftSealer =
        std::make_shared<PBFTSealer>(m_service, m_txPool, m_blockChain, m_sync, m_blockVerifier,
            protocol_id, m_param->baseDir(), m_keyPair, m_param->mutableConsensusParam().minerList);
    std::string ret = m_blockChain->getSystemConfigByKey(SYSTEM_KEY_TX_COUNT_LIMIT);
    pbftSealer->setMaxBlockTransactions(boost::lexical_cast<uint64_t>(ret));
    /// set params for PBFTEngine
    std::shared_ptr<PBFTEngine> pbftEngine =
        std::dynamic_pointer_cast<PBFTEngine>(pbftSealer->consensusEngine());
    pbftEngine->setIntervalBlockTime(SystemConfigMgr::c_intervalBlockTime);
    pbftEngine->setStorage(m_dbInitializer->storage());
    pbftEngine->setOmitEmptyBlock(SystemConfigMgr::c_omitEmptyBlock);
    pbftEngine->setMaxTTL(m_param->mutableConsensusParam().maxTTL);
    return pbftSealer;
}

std::shared_ptr<Sealer> Ledger::createRaftSealer()
{
    Ledger_LOG(DEBUG) << "[#initLedger] [#createRaftSealer]";
    if (!m_txPool || !m_blockChain || !m_sync || !m_blockVerifier || !m_dbInitializer)
    {
        Ledger_LOG(DEBUG) << "[#initLedger] [#createRaftSealer Failed]";
        return nullptr;
    }

    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::Raft);
    /// create consensus engine according to "consensusType"
    Ledger_LOG(DEBUG) << "[#initLedger] [#createRaftSealer] [Protocol ID]:  " << protocol_id;
    // auto intervalBlockTime = dev::config::SystemConfigMgr::c_intervalBlockTime;
    // std::shared_ptr<Sealer> raftSealer = std::make_shared<RaftSealer>(m_service, m_txPool,
    //    m_blockChain, m_sync, m_blockVerifier, m_keyPair, intervalBlockTime,
    //    intervalBlockTime + 1000, protocol_id, m_param->mutableConsensusParam().minerList);
    std::shared_ptr<Sealer> raftSealer =
        std::make_shared<RaftSealer>(m_service, m_txPool, m_blockChain, m_sync, m_blockVerifier,
            m_keyPair, m_param->mutableConsensusParam().minElectTime,
            m_param->mutableConsensusParam().maxElectTime, protocol_id,
            m_param->mutableConsensusParam().minerList);
    /// set params for RaftEngine
    std::shared_ptr<RaftEngine> raftEngine =
        std::dynamic_pointer_cast<RaftEngine>(raftSealer->consensusEngine());
    raftEngine->setStorage(m_dbInitializer->storage());
    return raftSealer;
}

/// init consensus
bool Ledger::consensusInitFactory()
{
    Ledger_LOG(DEBUG) << "[#initLedger] [#consensusInitFactory] [type]:  "
                      << m_param->mutableConsensusParam().consensusType;

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

    if (dev::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") == 0)
    {
        std::string error_msg =
            "Unsupported Consensus type: " + m_param->mutableConsensusParam().consensusType;
        Ledger_LOG(ERROR) << "[#initLedger] [#UnsupportConsensusType]:  "
                          << m_param->mutableConsensusParam().consensusType
                          << " use PBFT as default" << std::endl;
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
    Ledger_LOG(DEBUG) << "[#initLedger] [#initSync]" << std::endl;
    if (!m_txPool || !m_blockChain || !m_blockVerifier)
    {
        Ledger_LOG(DEBUG) << "[#initLedger] [#initSync Failed]" << std::endl;
        return false;
    }
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    dev::h256 genesisHash = m_blockChain->getBlockByNumber(int64_t(0))->headerHash();
    m_sync = std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain, m_blockVerifier,
        protocol_id, m_keyPair.pub(), genesisHash, m_param->mutableSyncParam().idleWaitMs);
    Ledger_LOG(DEBUG) << "[#initLedger] [#initSync SUCC]" << std::endl;
    return true;
}
}  // namespace ledger
}  // namespace dev
