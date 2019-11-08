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
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief : parse ledger configuration
 * @file: LedgerParam.cpp
 * @author: xingqiangbai
 * @date: 2019-10-16
 */
#include "LedgerParam.h"
#include "libconsensus/Common.h"
#include <libblockchain/BlockChainInterface.h>
#include <libconfig/GlobalConfigure.h>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/function_output_iterator.hpp>
#include <boost/property_tree/ini_parser.hpp>

using namespace boost::property_tree;
using namespace std;
using namespace dev;

namespace dev
{
namespace ledger
{
void LedgerParam::parseGenesisConfig(const std::string& _genesisFile)
{
    try
    {
        LedgerParam_LOG(INFO) << LOG_BADGE("parseGenesisConfig")
                              << LOG_DESC("initConsensusConfig/initStorageConfig/initTxConfig")
                              << LOG_KV("configFile", _genesisFile);
        ptree pt;
        // read the configuration file for a specified group
        read_ini(_genesisFile, pt);
        initConsensusConfig(pt);
        initEventLogFilterManagerConfig(pt);
        /// use UTCTime directly as timeStamp in case of the clock differences between machines
        mutableGenesisParam().timeStamp = pt.get<uint64_t>("group.timestamp", UINT64_MAX);
        LedgerParam_LOG(DEBUG) << LOG_BADGE("parseGenesisConfig")
                               << LOG_KV("timestamp", mutableGenesisParam().timeStamp);
        mutableStateParam().type = pt.get<std::string>("state.type", "storage");
        // Compatibility with previous versions RC2/RC1
        mutableStorageParam().type = pt.get<std::string>("storage.type", "LevelDB");
        mutableStorageParam().topic = pt.get<std::string>("storage.topic", "DB");
        mutableStorageParam().maxRetry = pt.get<uint>("storage.max_retry", 60);
        m_groupID = pt.get<int>("group.id", 0);
    }
    catch (std::exception& e)
    {
        std::string error_info = "init genesis config failed for " + toString(m_groupID) +
                                 " failed, error_msg: " + boost::diagnostic_information(e);
        LedgerParam_LOG(ERROR) << LOG_DESC("parseGenesisConfig Failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(dev::InitLedgerConfigFailed() << errinfo_comment(error_info));
    }
    m_genesisBlockParam = generateGenesisMark();
}

blockchain::GenesisBlockParam LedgerParam::generateGenesisMark()
{
    std::stringstream s;
    s << int(m_groupID) << "-";
    s << mutableGenesisParam().nodeListMark << "-";
    s << mutableConsensusParam().consensusType << "-";
    if (g_BCOSConfig.version() <= RC2_VERSION)
    {
        s << mutableStorageParam().type << "-";
    }
    s << mutableStateParam().type << "-";
    s << mutableConsensusParam().maxTransactions << "-";
    s << mutableTxParam().txGasLimit;
    LedgerParam_LOG(DEBUG) << LOG_BADGE("initMark") << LOG_KV("genesisMark", s.str());
    return blockchain::GenesisBlockParam{s.str(), mutableConsensusParam().sealerList,
        mutableConsensusParam().observerList, mutableConsensusParam().consensusType,
        mutableStorageParam().type, mutableStateParam().type,
        mutableConsensusParam().maxTransactions, mutableTxParam().txGasLimit,
        mutableGenesisParam().timeStamp};
}

void LedgerParam::parseIniConfig(const std::string& _iniConfigFile, const std::string& _dataPath)
{
    std::string prefix = _dataPath + "/group" + std::to_string(m_groupID);
    if (_dataPath == "")
    {
        prefix = "./group" + std::to_string(m_groupID);
    }
    m_baseDir = prefix;
    LedgerParam_LOG(INFO) << LOG_BADGE("initIniConfig")
                          << LOG_DESC("initTxPoolConfig/initSyncConfig/initTxExecuteConfig")
                          << LOG_KV("configFile", _iniConfigFile);
    ptree pt;
    if (boost::filesystem::exists(_iniConfigFile))
    {
        read_ini(_iniConfigFile, pt);
    }
    else
    {
        BOOST_THROW_EXCEPTION(
            dev::FileError() << errinfo_comment(_iniConfigFile + "not found, exit"));
    }
    initStorageConfig(pt);
    initTxPoolConfig(pt);
    initSyncConfig(pt);
    initTxExecuteConfig(pt);
    // init params releated to consensus(ttl)
    initConsensusIniConfig(pt);
}

void LedgerParam::init(const std::string& _configFilePath, const std::string& _dataPath)
{
    LedgerParam_LOG(INFO) << LOG_DESC("LedgerConstructor") << LOG_KV("configPath", _configFilePath)
                          << LOG_KV("baseDir", baseDir());
    /// The file group.X.genesis is required, otherwise the program terminates.
    /// load genesis config of group

    parseGenesisConfig(_configFilePath);
    // The file group.X.ini is available by default.
    std::string iniConfigFileName = _configFilePath;
    boost::replace_last(iniConfigFileName, "genesis", "ini");

    parseIniConfig(iniConfigFileName, _dataPath);
}

void LedgerParam::initTxExecuteConfig(ptree const& pt)
{
    if (dev::stringCmpIgnoreCase(mutableStateParam().type, "storage") == 0)
    {
        mutableTxParam().enableParallel = pt.get<bool>("tx_execute.enable_parallel", false);
    }
    else
    {
        mutableTxParam().enableParallel = false;
    }
    LedgerParam_LOG(DEBUG) << LOG_BADGE("InitTxExecuteConfig")
                           << LOG_KV("enableParallel", mutableTxParam().enableParallel);
}

void LedgerParam::initTxPoolConfig(ptree const& pt)
{
    try
    {
        mutableTxPoolParam().txPoolLimit =
            pt.get<int64_t>("tx_pool.limit", SYNC_TX_POOL_SIZE_DEFAULT);
        if (mutableTxPoolParam().txPoolLimit < 0)
        {
            BOOST_THROW_EXCEPTION(
                ForbidNegativeValue() << errinfo_comment("Please set tx_pool.limit to positive !"));
        }

        LedgerParam_LOG(DEBUG) << LOG_BADGE("initTxPoolConfig")
                               << LOG_KV("txPoolLimit", mutableTxPoolParam().txPoolLimit);
    }
    catch (std::exception& e)
    {
        mutableTxPoolParam().txPoolLimit = SYNC_TX_POOL_SIZE_DEFAULT;
        LedgerParam_LOG(WARNING) << LOG_BADGE("txPoolLimit") << LOG_DESC("txPoolLimit invalid");
    }
}


void LedgerParam::initConsensusIniConfig(ptree const& pt)
{
    mutableConsensusParam().maxTTL = pt.get<int8_t>("consensus.ttl", consensus::MAXTTL);
    if (mutableConsensusParam().maxTTL < 0)
    {
        BOOST_THROW_EXCEPTION(
            ForbidNegativeValue() << errinfo_comment("Please set consensus.ttl to positive !"));
    }

    // the minimum block generation time(ms)
    mutableConsensusParam().minBlockGenTime =
        pt.get<signed>("consensus.min_block_generation_time", 500);
    if (mutableConsensusParam().minBlockGenTime < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.min_block_generation_time to positive !"));
    }

    // enable dynamic block size
    mutableConsensusParam().enableDynamicBlockSize =
        pt.get<bool>("consensus.enable_dynamic_block_size", true);
    // obtain block size increase ratio
    mutableConsensusParam().blockSizeIncreaseRatio =
        pt.get<float>("consensus.block_size_increase_ratio", 0.5);

    if (mutableConsensusParam().blockSizeIncreaseRatio < 0 ||
        mutableConsensusParam().blockSizeIncreaseRatio > 2)
    {
        mutableConsensusParam().blockSizeIncreaseRatio = 0.5;
    }
    LedgerParam_LOG(DEBUG)
        << LOG_BADGE("initConsensusIniConfig")
        << LOG_KV("maxTTL", std::to_string(mutableConsensusParam().maxTTL))
        << LOG_KV("minBlockGenerationTime", mutableConsensusParam().minBlockGenTime)
        << LOG_KV("enablDynamicBlockSize", mutableConsensusParam().enableDynamicBlockSize)
        << LOG_KV("blockSizeIncreaseRatio", mutableConsensusParam().blockSizeIncreaseRatio);
}


/// init consensus configurations:
/// 1. consensusType: current support pbft only (default is pbft)
/// 2. maxTransNum: max number of transactions can be sealed into a block
/// 3. intervalBlockTime: average block generation period
/// 4. sealer.${idx}: define the node id of every sealer related to the group
void LedgerParam::initConsensusConfig(ptree const& pt)
{
    mutableConsensusParam().consensusType = pt.get<std::string>("consensus.consensus_type", "pbft");

    mutableConsensusParam().maxTransactions = pt.get<int64_t>("consensus.max_trans_num", 1000);
    if (mutableConsensusParam().maxTransactions < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.max_trans_num to positive !"));
    }

    mutableConsensusParam().minElectTime = pt.get<int64_t>("consensus.min_elect_time", 1000);
    if (mutableConsensusParam().minElectTime < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.min_elect_time to positive !"));
    }

    mutableConsensusParam().maxElectTime = pt.get<int64_t>("consensus.max_elect_time", 2000);
    if (mutableConsensusParam().maxElectTime < 0)
    {
        BOOST_THROW_EXCEPTION(ForbidNegativeValue() << errinfo_comment(
                                  "Please set consensus.max_elect_time to positive !"));
    }
    mutableTxParam().txGasLimit = pt.get<int64_t>("tx.gas_limit", 300000000);
    if (mutableTxParam().txGasLimit < 0)
    {
        BOOST_THROW_EXCEPTION(
            ForbidNegativeValue() << errinfo_comment("Please set tx.gas_limit to positive !"));
    }

    LedgerParam_LOG(DEBUG) << LOG_BADGE("initConsensusConfig")
                           << LOG_KV("type", mutableConsensusParam().consensusType)
                           << LOG_KV("maxTxNum", mutableConsensusParam().maxTransactions)
                           << LOG_KV("txGasLimit", mutableTxParam().txGasLimit);
    std::stringstream nodeListMark;
    try
    {
        for (auto it : pt.get_child("consensus"))
        {
            if (it.first.find("node.") == 0)
            {
                std::string data = it.second.data();
                boost::to_lower(data);
                LedgerParam_LOG(INFO)
                    << LOG_BADGE("initConsensusConfig") << LOG_KV("it.first", data);
                // Uniform lowercase nodeID
                dev::h512 nodeID(data);
                mutableConsensusParam().sealerList.push_back(nodeID);
                // The full output node ID is required.
                nodeListMark << data << ",";
            }
        }
    }
    catch (std::exception& e)
    {
        LedgerParam_LOG(ERROR) << LOG_BADGE("initConsensusConfig")
                               << LOG_DESC("Parse consensus section failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
    }
    mutableGenesisParam().nodeListMark = nodeListMark.str();
}

void LedgerParam::initSyncConfig(ptree const& pt)
{
    try
    {
        // idleWaitMs: default is 30ms
        mutableSyncParam().idleWaitMs = pt.get<uint>("sync.idle_wait_ms", SYNC_IDLE_WAIT_DEFAULT);
        if (mutableSyncParam().idleWaitMs < 0)
        {
            BOOST_THROW_EXCEPTION(ForbidNegativeValue()
                                  << errinfo_comment("Please set sync.idle_wait_ms to positive !"));
        }

        LedgerParam_LOG(DEBUG) << LOG_BADGE("initSyncConfig")
                               << LOG_KV("idleWaitMs", mutableSyncParam().idleWaitMs);
    }
    catch (std::exception& e)
    {
        mutableSyncParam().idleWaitMs = SYNC_IDLE_WAIT_DEFAULT;
        LedgerParam_LOG(WARNING) << LOG_BADGE("initSyncConfig") << LOG_DESC("idleWaitMs invalid");
    }
}

std::string LedgerParam::uriEncode(const std::string& keyWord)
{
    std::vector<std::string> keyWordList;
    string keyWordEncode;
    std::transform(keyWord.begin(), keyWord.end(), std::back_inserter(keyWordList),
        [](std::string::value_type v) -> std::string {
            if (isalnum(v))
            {
                return std::string() + v;
            }
            std::ostringstream enc;
            enc << '%' << std::setw(2) << std::setfill('0') << std::hex << std::uppercase
                << int(static_cast<unsigned char>(v));
            return enc.str();
        });
    for (const auto& item : keyWordList)
    {
        keyWordEncode.append(item);
    }
    return keyWordEncode;
}


void LedgerParam::initStorageConfig(ptree const& pt)
{
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        mutableStorageParam().type = pt.get<std::string>("storage.type", "RocksDB");
        mutableStorageParam().topic = pt.get<std::string>("storage.topic", "DB");
        mutableStorageParam().maxRetry = pt.get<uint>("storage.max_retry", 60);
        mutableStorageParam().binaryLog = pt.get<bool>("storage.binary_log", false);
        mutableStorageParam().CachedStorage = pt.get<bool>("storage.cached_storage", true);
        if (!dev::stringCmpIgnoreCase(mutableStorageParam().type, "LevelDB"))
        {
            mutableStorageParam().type = "RocksDB";
            LedgerParam_LOG(WARNING) << "LevelDB is deprecated! RocksDB is recommended.";
        }
    }
    mutableStorageParam().path = baseDir() + "/block";
    if (!dev::stringCmpIgnoreCase(mutableStorageParam().type, "RocksDB"))
    {
        mutableStorageParam().path += "/RocksDB";
    }
    else if (!dev::stringCmpIgnoreCase(mutableStorageParam().type, "Scalable"))
    {
        mutableStorageParam().path += "/Scalable";
    }
    mutableStorageParam().maxCapacity = pt.get<uint>("storage.max_capacity", 32);
    auto scrollThresholdMultiple = pt.get<uint>("storage.scroll_threshold_multiple", 2);
    mutableStorageParam().scrollThreshold =
        scrollThresholdMultiple > 0 ? scrollThresholdMultiple * g_BCOSConfig.c_blockLimit : 2000;

    mutableStorageParam().maxForwardBlock = pt.get<uint>("storage.max_forward_block", 10);

    if (mutableStorageParam().maxRetry <= 0)
    {
        mutableStorageParam().maxRetry = 60;
    }

    mutableStorageParam().dbType = pt.get<std::string>("storage.db_type", "mysql");
    mutableStorageParam().dbIP = pt.get<std::string>("storage.db_ip", "127.0.0.1");
    mutableStorageParam().dbPort = pt.get<int>("storage.db_port", 3306);
    mutableStorageParam().dbUsername = pt.get<std::string>("storage.db_username", "");
    mutableStorageParam().dbPasswd = uriEncode(pt.get<std::string>("storage.db_passwd", ""));
    mutableStorageParam().dbName = pt.get<std::string>("storage.db_name", "");
    mutableStorageParam().dbCharset = pt.get<std::string>("storage.db_charset", "utf8mb4");
    mutableStorageParam().initConnections = pt.get<int>("storage.init_connections", 15);
    mutableStorageParam().maxConnections = pt.get<int>("storage.max_connections", 50);

    LedgerParam_LOG(INFO) << LOG_BADGE("initStorageConfig")
                          << LOG_KV("stateType", mutableStateParam().type)
                          << LOG_KV("storageDB", mutableStorageParam().type)
                          << LOG_KV("storagePath", mutableStorageParam().path)
                          << LOG_KV("baseDir", baseDir())
                          << LOG_KV("dbtype", mutableStorageParam().dbType)
                          << LOG_KV("dbip", mutableStorageParam().dbIP)
                          << LOG_KV("dbport", mutableStorageParam().dbPort)
                          << LOG_KV("dbcharset", mutableStorageParam().dbCharset)
                          << LOG_KV("initconnections", mutableStorageParam().initConnections)
                          << LOG_KV("maxconnections", mutableStorageParam().maxConnections)
                          << LOG_KV("scrollThreshold", mutableStorageParam().scrollThreshold);
}

void LedgerParam::initEventLogFilterManagerConfig(boost::property_tree::ptree const& pt)
{
    mutableEventLogFilterManagerParams().maxBlockRange =
        pt.get<int64_t>("event_filter.max_block_range", MAX_BLOCK_RANGE_EVENT_FILTER);

    mutableEventLogFilterManagerParams().maxBlockPerProcess =
        pt.get<int64_t>("event_filter.max_block_per_process", MAX_BLOCK_PER_PROCESS);

    LedgerParam_LOG(DEBUG) << LOG_BADGE("max_block_range")
                           << LOG_KV("maxBlockRange",
                                  mutableEventLogFilterManagerParams().maxBlockRange)
                           << LOG_KV("maxBlockPerProcess",
                                  mutableEventLogFilterManagerParams().maxBlockPerProcess);
}

}  // namespace ledger
}  // namespace dev
