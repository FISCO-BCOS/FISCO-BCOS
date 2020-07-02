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
#include <libdevcore/OverlayDB.h>
#include <libdevcore/easylog.h>
#include <libprecompiled/Common.h>
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
using namespace dev::precompiled;
using namespace std;

namespace dev
{
namespace ledger
{
bool Ledger::initLedger(const std::string& _configFilePath)
{
#ifndef FISCO_EASYLOG
    BOOST_LOG_SCOPED_THREAD_ATTR(
        "GroupId", boost::log::attributes::constant<std::string>(std::to_string(m_groupId)));
#endif
    Ledger_LOG(INFO) << LOG_DESC("LedgerConstructor") << LOG_KV("configPath", _configFilePath)
                     << LOG_KV("baseDir", m_param->baseDir());
    /// The file group.X.genesis is required, otherwise the program terminates.
    /// load genesis config of group
    initGenesisConfig(_configFilePath);
    GenesisBlockParam genesisParam;
    initGenesisMark(genesisParam);
    /// The file group.X.ini is available by default.
    /// In this case, the configuration item uses the default value.
    /// load ini config of group for TxPool/Sync modules
    std::string iniConfigFileName = _configFilePath;
    boost::replace_last(iniConfigFileName, m_postfixGenesis, m_postfixIni);

    /// you should invoke initGenesisConfig first before invoke initIniConfig
    initIniConfig(iniConfigFileName);
    if (!m_param)
        return false;
    /// init dbInitializer
    Ledger_LOG(INFO) << LOG_BADGE("initLedger") << LOG_BADGE("DBInitializer");
    m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param);
    m_dbInitializer->setChannelRPCServer(m_channelRPCServer);
    // m_dbInitializer
    if (!m_dbInitializer)
        return false;
    m_dbInitializer->initStorageDB();
    /// set group ID for storage
    m_dbInitializer->storage()->setGroupID(m_groupId);
    /// init the DB
    bool ret = initBlockChain(genesisParam);
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
    return (initBlockVerifier() && initTxPool() && initSync() && consensusInitFactory());
}

/**
 * @brief: init configuration related to the ledger with specified configuration file
 * @param configPath: the path of the config file
 */
void Ledger::initGenesisConfig(std::string const& configPath)
{
    try
    {
        Ledger_LOG(INFO) << LOG_BADGE("initGenesisConfig")
                         << LOG_DESC("initConsensusConfig/initDBConfig/initTxConfig")
                         << LOG_KV("configFile", configPath);
        ptree pt;
        /// read the configuration file for a specified group
        read_ini(configPath, pt);
        /// init params related to consensus
        initConsensusConfig(pt);
        /// init params related to tx
        initTxConfig(pt);
        /// use UTCTime directly as timeStamp in case of the clock differences between machines
        m_param->mutableGenesisParam().timeStamp = pt.get<uint64_t>("group.timestamp", UINT64_MAX);
        Ledger_LOG(DEBUG) << LOG_BADGE("initGenesisConfig")
                          << LOG_KV("timestamp", m_param->mutableGenesisParam().timeStamp);
        /// set state db related param
        m_param->mutableStateParam().type = pt.get<std::string>("state.type", "storage");
        // Compatibility with previous versions RC2/RC1
        m_param->mutableStorageParam().type = pt.get<std::string>("storage.type", "LevelDB");
        m_param->mutableStorageParam().topic = pt.get<std::string>("storage.topic", "DB");
        m_param->mutableStorageParam().maxRetry = pt.get<int>("storage.max_retry", 100);
    }
    catch (std::exception& e)
    {
        std::string error_info = "init genesis config failed for " + toString(m_groupId) +
                                 " failed, error_msg: " + boost::diagnostic_information(e);
        Ledger_LOG(ERROR) << LOG_DESC("initGenesisConfig Failed")
                          << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(dev::InitLedgerConfigFailed() << errinfo_comment(error_info));
        exit(1);
    }
}

void Ledger::initIniConfig(std::string const& iniConfigFileName)
{
    Ledger_LOG(INFO) << LOG_BADGE("initIniConfig")
                     << LOG_DESC("initTxPoolConfig/initSyncConfig/initTxExecuteConfig")
                     << LOG_KV("configFile", iniConfigFileName);
    ptree pt;
    if (boost::filesystem::exists(iniConfigFileName))
    {
        /// read the configuration file for a specified group
        read_ini(iniConfigFileName, pt);
    }
    /// db params initialization
    initDBConfig(pt);
    /// init params related to txpool
    initTxPoolConfig(pt);
    /// init params related to sync
    initSyncConfig(pt);
    initTxExecuteConfig(pt);
    /// init params releated to consensus(ttl)
    initConsensusIniConfig(pt);
}

void Ledger::initTxExecuteConfig(ptree const& pt)
{
    if (dev::stringCmpIgnoreCase(m_param->mutableStateParam().type, "storage") == 0)
    {
        m_param->mutableTxParam().enableParallel =
            pt.get<bool>("tx_execute.enable_parallel", false);
    }
    else
    {
        m_param->mutableTxParam().enableParallel = false;
    }
    Ledger_LOG(DEBUG) << LOG_BADGE("InitTxExecuteConfig")
                      << LOG_KV("enableParallel", m_param->mutableTxParam().enableParallel);
}

void Ledger::initTxPoolConfig(ptree const& pt)
{
    try
    {
        m_param->mutableTxPoolParam().txPoolLimit =
            pt.get<int64_t>("tx_pool.limit", SYNC_TX_POOL_SIZE_DEFAULT);
        if (m_param->mutableTxPoolParam().txPoolLimit < 0)
        {
            BOOST_THROW_EXCEPTION(
                ForbidNegativeValue() << errinfo_comment("Please set tx_pool.limit to positive !"));
        }

        Ledger_LOG(DEBUG) << LOG_BADGE("initTxPoolConfig")
                          << LOG_KV("txPoolLimit", m_param->mutableTxPoolParam().txPoolLimit);
    }
    catch (std::exception& e)
    {
        m_param->mutableTxPoolParam().txPoolLimit = SYNC_TX_POOL_SIZE_DEFAULT;
        Ledger_LOG(WARNING) << LOG_BADGE("txPoolLimit") << LOG_DESC("txPoolLimit invalid");
    }
}


void Ledger::initConsensusIniConfig(ptree const& pt)
{
    m_param->mutableConsensusParam().maxTTL = pt.get<int8_t>("consensus.ttl", MAXTTL);
    if (m_param->mutableConsensusParam().maxTTL < 0)
    {
        BOOST_THROW_EXCEPTION(
            ForbidNegativeValue() << errinfo_comment("Please set consensus.ttl to positive !"));
    }

    /// the minimum block generation time(ms)
    m_param->mutableConsensusParam().minBlockGenTime =
        pt.get<signed>("consensus.min_block_generation_time", 500);
    if (m_param->mutableConsensusParam().minBlockGenTime < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.min_block_generation_time to positive !"));
    }

    /// enable dynamic block size
    m_param->mutableConsensusParam().enableDynamicBlockSize =
        pt.get<bool>("consensus.enable_dynamic_block_size", true);
    /// obtain block size increase ratio
    m_param->mutableConsensusParam().blockSizeIncreaseRatio =
        pt.get<float>("consensus.block_size_increase_ratio", 0.5);

    if (m_param->mutableConsensusParam().blockSizeIncreaseRatio < 0 ||
        m_param->mutableConsensusParam().blockSizeIncreaseRatio > 2)
    {
        m_param->mutableConsensusParam().blockSizeIncreaseRatio = 0.5;
    }
    Ledger_LOG(DEBUG) << LOG_BADGE("initConsensusIniConfig")
                      << LOG_KV("maxTTL", std::to_string(m_param->mutableConsensusParam().maxTTL))
                      << LOG_KV("minBlockGenerationTime",
                             m_param->mutableConsensusParam().minBlockGenTime)
                      << LOG_KV("enablDynamicBlockSize",
                             m_param->mutableConsensusParam().enableDynamicBlockSize)
                      << LOG_KV("blockSizeIncreaseRatio",
                             m_param->mutableConsensusParam().blockSizeIncreaseRatio);
}


/// init consensus configurations:
/// 1. consensusType: current support pbft only (default is pbft)
/// 2. maxTransNum: max number of transactions can be sealed into a block
/// 3. intervalBlockTime: average block generation period
/// 4. sealer.${idx}: define the node id of every sealer related to the group
void Ledger::initConsensusConfig(ptree const& pt)
{
    m_param->mutableConsensusParam().consensusType =
        pt.get<std::string>("consensus.consensus_type", "pbft");

    m_param->mutableConsensusParam().maxTransactions =
        pt.get<int64_t>("consensus.max_trans_num", 1000);
    if (m_param->mutableConsensusParam().maxTransactions < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.max_trans_num to positive !"));
    }

    m_param->mutableConsensusParam().minElectTime =
        pt.get<int64_t>("consensus.min_elect_time", 1000);
    if (m_param->mutableConsensusParam().minElectTime < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.min_elect_time to positive !"));
    }

    m_param->mutableConsensusParam().maxElectTime =
        pt.get<int64_t>("consensus.max_elect_time", 2000);
    if (m_param->mutableConsensusParam().maxElectTime < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.max_elect_time to positive !"));
    }

    Ledger_LOG(DEBUG) << LOG_BADGE("initConsensusConfig")
                      << LOG_KV("type", m_param->mutableConsensusParam().consensusType)
                      << LOG_KV("maxTxNum", m_param->mutableConsensusParam().maxTransactions);
    std::stringstream nodeListMark;
    try
    {
        for (auto it : pt.get_child("consensus"))
        {
            if (it.first.find("node.") == 0)
            {
                std::string data = it.second.data();
                boost::to_lower(data);
                Ledger_LOG(INFO) << LOG_BADGE("initConsensusConfig") << LOG_KV("it.first", data);
                // Uniform lowercase nodeID
                dev::h512 nodeID(data);
                m_param->mutableConsensusParam().sealerList.push_back(nodeID);
                // The full output node ID is required.
                nodeListMark << data << ",";
            }
        }
    }
    catch (std::exception& e)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initConsensusConfig")
                          << LOG_DESC("Parse consensus section failed")
                          << LOG_KV("EINFO", boost::diagnostic_information(e));
    }
    m_param->mutableGenesisParam().nodeListMark = nodeListMark.str();
}

/// init sync related configurations
/// 1. idleWaitMs: default is 30ms
void Ledger::initSyncConfig(ptree const& pt)
{
    try
    {
        m_param->mutableSyncParam().idleWaitMs =
            pt.get<signed>("sync.idle_wait_ms", SYNC_IDLE_WAIT_DEFAULT);
        if (m_param->mutableSyncParam().idleWaitMs <= 0)
        {
            BOOST_THROW_EXCEPTION(ForbidNegativeValue()
                                  << errinfo_comment("Please set sync.idle_wait_ms to positive !"));
        }

        auto eachBlockDownloadRequestTimeOut =
            pt.get<signed>("sync.each_block_download_timeout", 500);
        if (eachBlockDownloadRequestTimeOut <= 0)
        {
            BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                      "Please set sync.each_block_download_timeout !"));
        }
        m_param->mutableSyncParam().eachBlockDownloadRequestTimeOut =
            eachBlockDownloadRequestTimeOut;

        Ledger_LOG(DEBUG) << LOG_BADGE("initSyncConfig")
                          << LOG_KV("idleWaitMs", m_param->mutableSyncParam().idleWaitMs);
    }
    catch (std::exception& e)
    {
        m_param->mutableSyncParam().idleWaitMs = SYNC_IDLE_WAIT_DEFAULT;
        Ledger_LOG(WARNING) << LOG_BADGE("initSyncConfig") << LOG_DESC("idleWaitMs invalid");
    }
}

/// init db related configurations:
/// dbType: leveldb/AMDB, storage type, default is "AMDB"
/// mpt: true/false, enable mpt or not, default is true
/// dbpath: data to place all data of the group, default is "data"
void Ledger::initDBConfig(ptree const& pt)
{
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        m_param->mutableStorageParam().type = pt.get<std::string>("storage.type", "RocksDB");
        m_param->mutableStorageParam().topic = pt.get<std::string>("storage.topic", "DB");
        m_param->mutableStorageParam().maxRetry = pt.get<int>("storage.max_retry", 100);
        if (!dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "LevelDB"))
        {
            m_param->mutableStorageParam().type = "RocksDB";
            Ledger_LOG(WARNING) << "LevelDB is deprecated!! RocksDB is now recommended, because "
                                   "RocksDB is better than LevelDB in performance.";
        }
    }
    m_param->mutableStorageParam().path = m_param->baseDir() + "/block";
    m_param->mutableStorageParam().maxCapacity = pt.get<int>("storage.max_capacity", 256);

    if (m_param->mutableStorageParam().maxCapacity < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue()
                              << errinfo_comment("Please set storage.max_capacity to positive !"));
    }

    m_param->mutableStorageParam().maxForwardBlock = pt.get<int>("storage.max_forward_block", 10);
    if (m_param->mutableStorageParam().maxForwardBlock < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set storage.max_forward_block to positive !"));
    }

    if (m_param->mutableStorageParam().maxRetry <= 0)
    {
        m_param->mutableStorageParam().maxRetry = 100;
    }
    /// set state db related param
    m_param->mutableStateParam().type = pt.get<std::string>("state.type", "storage");

    // read db config from config eg:mysqlip mysqlport and so on
    m_param->mutableStorageParam().dbType = pt.get<std::string>("storage.db_type", "mysql");
    m_param->mutableStorageParam().dbIP = pt.get<std::string>("storage.db_ip", "127.0.0.1");
    m_param->mutableStorageParam().dbPort = pt.get<int>("storage.db_port", 3306);
    m_param->mutableStorageParam().dbUsername = pt.get<std::string>("storage.db_username", "");
    m_param->mutableStorageParam().dbPasswd = pt.get<std::string>("storage.db_passwd", "");
    m_param->mutableStorageParam().dbName = pt.get<std::string>("storage.db_name", "");
    m_param->mutableStorageParam().dbCharset = pt.get<std::string>("storage.db_charset", "utf8mb4");
    m_param->mutableStorageParam().initConnections = pt.get<int>("storage.init_connections", 15);
    m_param->mutableStorageParam().maxConnections = pt.get<int>("storage.max_connections", 50);

    Ledger_LOG(DEBUG) << LOG_BADGE("initDBConfig")
                      << LOG_KV("storageDB", m_param->mutableStorageParam().type)
                      << LOG_KV("storagePath", m_param->mutableStorageParam().path)
                      << LOG_KV("baseDir", m_param->baseDir())
                      << LOG_KV("dbtype", m_param->mutableStorageParam().dbType)
                      << LOG_KV("dbip", m_param->mutableStorageParam().dbIP)
                      << LOG_KV("dbport", m_param->mutableStorageParam().dbPort)
                      << LOG_KV("dbcharset", m_param->mutableStorageParam().dbCharset)
                      << LOG_KV("initconnections", m_param->mutableStorageParam().initConnections)
                      << LOG_KV("maxconnections", m_param->mutableStorageParam().maxConnections);
}

/// init tx related configurations
/// 1. gasLimit: default is 300000000
void Ledger::initTxConfig(boost::property_tree::ptree const& pt)
{
    m_param->mutableTxParam().txGasLimit = pt.get<int64_t>("tx.gas_limit", 300000000);
    if (m_param->mutableTxParam().txGasLimit < 0)
    {
        BOOST_THROW_EXCEPTION(
            ForbidNegativeValue() << errinfo_comment("Please set tx.gas_limit to positive !"));
    }

    Ledger_LOG(DEBUG) << LOG_BADGE("initTxConfig")
                      << LOG_KV("txGasLimit", m_param->mutableTxParam().txGasLimit);
}

/// init mark of this group
void Ledger::initGenesisMark(GenesisBlockParam& genesisParam)
{
    std::stringstream s;
    s << int(m_groupId) << "-";
    s << m_param->mutableGenesisParam().nodeListMark << "-";
    s << m_param->mutableConsensusParam().consensusType << "-";
    if (g_BCOSConfig.version() <= RC2_VERSION)
    {
        s << m_param->mutableStorageParam().type << "-";
    }
    s << m_param->mutableStateParam().type << "-";
    s << m_param->mutableConsensusParam().maxTransactions << "-";
    s << m_param->mutableTxParam().txGasLimit;
    m_param->mutableGenesisParam().genesisMark = s.str();
    genesisParam = GenesisBlockParam{m_param->mutableGenesisParam().genesisMark,
        m_param->mutableConsensusParam().sealerList, m_param->mutableConsensusParam().observerList,
        m_param->mutableConsensusParam().consensusType, m_param->mutableStorageParam().type,
        m_param->mutableStateParam().type, m_param->mutableConsensusParam().maxTransactions,
        m_param->mutableTxParam().txGasLimit, m_param->mutableGenesisParam().timeStamp};
    Ledger_LOG(DEBUG) << LOG_BADGE("initMark")
                      << LOG_KV("genesisMark", m_param->mutableGenesisParam().genesisMark);
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
    std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();
    blockChain->setStateStorage(m_dbInitializer->storage());
    blockChain->setTableFactoryFactory(m_dbInitializer->tableFactoryFactory());
    m_blockChain = blockChain;
    bool ret = m_blockChain->checkAndBuildGenesisBlock(_genesisParam);
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
    std::shared_ptr<PBFTSealer> pbftSealer = std::make_shared<PBFTSealer>(m_service, m_txPool,
        m_blockChain, m_sync, m_blockVerifier, protocol_id, m_param->baseDir(), m_keyPair,
        m_param->mutableConsensusParam().sealerList);

    pbftSealer->setEnableDynamicBlockSize(m_param->mutableConsensusParam().enableDynamicBlockSize);
    pbftSealer->setBlockSizeIncreaseRatio(m_param->mutableConsensusParam().blockSizeIncreaseRatio);

    /// set params for PBFTEngine
    std::shared_ptr<PBFTEngine> pbftEngine =
        std::dynamic_pointer_cast<PBFTEngine>(pbftSealer->consensusEngine());
    /// set the range of block generation time
    pbftEngine->setEmptyBlockGenTime(g_BCOSConfig.c_intervalBlockTime);
    pbftEngine->setMinBlockGenerationTime(m_param->mutableConsensusParam().minBlockGenTime);

    pbftEngine->setOmitEmptyBlock(g_BCOSConfig.c_omitEmptyBlock);
    pbftEngine->setMaxTTL(m_param->mutableConsensusParam().maxTTL);
    return pbftSealer;
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
                      << LOG_KV("idleWaitMs", m_param->mutableSyncParam().idleWaitMs);
    if (!m_txPool || !m_blockChain || !m_blockVerifier)
    {
        Ledger_LOG(ERROR) << LOG_BADGE("initLedger") << LOG_DESC("#initSync Failed");
        return false;
    }
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    dev::h256 genesisHash = m_blockChain->getBlockByNumber(int64_t(0))->headerHash();
    auto syncMaster =
        std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain, m_blockVerifier,
            protocol_id, m_keyPair.pub(), genesisHash, m_param->mutableSyncParam().idleWaitMs);

    syncMaster->setEachBlockDownloadingRequestTimeout(
        m_param->mutableSyncParam().eachBlockDownloadRequestTimeOut);
    m_sync = syncMaster;

    Ledger_LOG(DEBUG) << LOG_BADGE("initLedger") << LOG_DESC("initSync SUCC");
    return true;
}
}  // namespace ledger
}  // namespace dev
