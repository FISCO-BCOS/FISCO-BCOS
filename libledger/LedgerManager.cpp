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
 * @brief : implementation of Ledger manager
 * @file: LedgerManager.cpp
 * @author: yujiechen
 * @date: 2019-5-21
 */
#include "LedgerManager.h"
#ifndef FISCO_DEBUG
#include "gperftools/malloc_extension.h"
#endif
#include <libconfig/GlobalConfigure.h>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

using namespace dev;
using namespace dev::ledger;
using namespace std;
namespace fs = boost::filesystem;

string LedgerManager::generateAndGetGroupStatusFile(GROUP_ID const& _groupID) const
{
    RecursiveGuard l(x_ledgerManager);
    auto statusFilePath = fs::path(g_BCOSConfig.dataDir());
    statusFilePath /= "group" + to_string(_groupID);
    if (!fs::exists(statusFilePath) || !fs::is_directory(statusFilePath))
    {
        fs::create_directories(statusFilePath);
    }
    statusFilePath /= ".group_status";
    return statusFilePath.string();
}

bool LedgerManager::isGroupExist(dev::GROUP_ID const& _groupID) const
{
    RecursiveGuard l(x_ledgerManager);
    auto statusFilePath = fs::path(g_BCOSConfig.dataDir());
    statusFilePath /= "group" + to_string(_groupID);
    if (!fs::exists(statusFilePath) || !fs::is_directory(statusFilePath))
    {
        return false;
    }
    return true;
}

LedgerStatus LedgerManager::queryGroupStatus(dev::GROUP_ID const& _groupID)
{
    RecursiveGuard l(x_ledgerManager);
    if (!isGroupExist(_groupID))
    {
        return LedgerStatus::INEXISTENT;
    }
    if (!fs::exists(generateAndGetGroupStatusFile(_groupID)))
    {
        return LedgerStatus::INEXISTENT;
    }

    string status = "";
    ifstream statusFile;
    statusFile.open(generateAndGetGroupStatusFile(_groupID));
    statusFile >> status;

    if (status == "STOPPED")
    {
        return LedgerStatus::STOPPED;
    }

    if (status == "STOPPING")
    {
        return LedgerStatus::STOPPING;
    }

    if (status == "DELETED")
    {
        return LedgerStatus::DELETED;
    }

    if (status == "RUNNING")
    {
        return LedgerStatus::RUNNING;
    }

    BOOST_THROW_EXCEPTION(UnknownGroupStatus());
}

void LedgerManager::setGroupStatus(dev::GROUP_ID const& _groupID, LedgerStatus _status)
{
    // this method assumes that caller has already acquired x_ledgerManager

    string status;

    switch (_status)
    {
    case LedgerStatus::STOPPED:
        status = "STOPPED";
        break;
    case LedgerStatus::STOPPING:
        status = "STOPPING";
        break;
    case LedgerStatus::DELETED:
        status = "DELETED";
        break;
    case LedgerStatus::RUNNING:
        status = "RUNNING";
        break;
    default:
        break;
    }

    if (!status.empty())
    {
        ofstream statusFile;
        statusFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        statusFile.open(generateAndGetGroupStatusFile(_groupID));
        statusFile << status;
    }
}

void LedgerManager::checkGroupStatus(dev::GROUP_ID const& _groupID, LedgerStatus _allowedStatus)
{
    auto status = queryGroupStatus(_groupID);

    if (status == _allowedStatus)
    {
        return;
    }

    switch (status)
    {
    case LedgerStatus::INEXISTENT:
        BOOST_THROW_EXCEPTION(GroupNotFound());
    case LedgerStatus::RUNNING:
        BOOST_THROW_EXCEPTION(GroupIsRunning());
    case LedgerStatus::DELETED:
        BOOST_THROW_EXCEPTION(GroupAlreadyDeleted());
    case LedgerStatus::STOPPED:
        BOOST_THROW_EXCEPTION(GroupAlreadyStopped());
    case LedgerStatus::STOPPING:
        BOOST_THROW_EXCEPTION(GroupIsStopping());
    case LedgerStatus::UNKNOWN:
        BOOST_THROW_EXCEPTION(UnknownGroupStatus());
    }
}

void LedgerManager::generateGroup(dev::GROUP_ID _groupID, const GroupParams& _params)
{
    RecursiveGuard l(x_ledgerManager);
    checkGroupStatus(_groupID, LedgerStatus::INEXISTENT);

    auto confDir = g_BCOSConfig.confDir();

    string genesisConfFileName = "group." + std::to_string(_groupID) + ".genesis";
    fs::path genesisConfFilePath(confDir + fs::path::separator + genesisConfFileName);
    if (exists(genesisConfFilePath))
    {
        BOOST_THROW_EXCEPTION(GenesisConfAlreadyExists());
    }

    string groupConfFileName = "group." + std::to_string(_groupID) + ".ini";
    fs::path groupConfFilePath(confDir + fs::path::separator + groupConfFileName);
    if (fs::exists(groupConfFilePath))
    {
        BOOST_THROW_EXCEPTION(GroupConfAlreadyExists());
    }

    std::ofstream genesisConfFile, groupConfFile;
    genesisConfFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);
    groupConfFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    genesisConfFile.open(genesisConfFilePath.c_str());
    genesisConfFile << generateGenesisConfig(_groupID, _params);

    groupConfFile.open(groupConfFilePath.c_str());
    groupConfFile << generateGroupConfig();

    setGroupStatus(_groupID, LedgerStatus::STOPPED);
}

string LedgerManager::generateGenesisConfig(dev::GROUP_ID _groupID, const GroupParams& _params)
{
    static string configTemplate =
        "[consensus]\n"
        "    ; consensus algorithm type, now support PBFT(consensus_type=pbft) and "
        "Raft(consensus_type=raft)\n"
        "    consensus_type=pbft\n"
        "    ; the max number of transactions of a block\n"
        "    max_trans_num=1000\n"
        "    ; in seconds, block consensus timeout, at least 3s\n"
        "    consensus_timeout=3\n"
        "    ; the node id of consensusers\n"
        "%1%"
        "[state]\n"
        "    type=storage\n"
        "[tx]\n"
        "    ; transaction gas limit\n"
        "    gas_limit=300000000\n"
        "[group]\n"
        "    id=%2%\n"
        "    timestamp=%3%\n"
        "[evm]\n"
        "    enable_free_storage=%4%\n";

    string nodeIDList;
    int nodeIdx = 0;
    for (auto& sealer : _params.sealers)
    {
        nodeIDList += (boost::format("    node.%1%=%2%\n") % nodeIdx % sealer).str();
        nodeIdx++;
    }

    string config = (boost::format(configTemplate) % nodeIDList.c_str() % _groupID %
                     _params.timestamp % (_params.enableFreeStorage ? "true" : "false"))
                        .str();
    return config;
}

string LedgerManager::generateGroupConfig()
{
    static string configTemplate =
        "[consensus]\n"
        "    ; the ttl for broadcasting pbft message\n"
        "    ;ttl=2\n"
        "    ; min block generation time(ms), the max block generation time is 1000 ms\n"
        "    min_block_generation_time=500\n"
        "    enable_dynamic_block_size=true\n"
        "    enable_ttl_optimization=true\n"
        "    enable_prepare_with_txsHash=true\n"
        "    ; The following is the relevant configuration of rpbft\n"
        "    ; set true to enable broadcast prepare request by tree=true\n"
        "    broadcast_prepare_by_tree=true\n"
        "    ; percent of nodes that broadcast prepare status to, must be between 25 and 100\n"
        "    prepare_status_broadcast_percent=33\n"
        "    ; max wait time before request missed transactions, ms, must be between 5ms and "
        "1000ms\n"
        "    max_request_missedTxs_waitTime=100\n"
        "    ; maximum wait time before requesting a prepare, ms, must be between 10ms and 1000ms\n"
        "    max_request_prepare_waitTime=100\n"

        "[storage]\n"
        "    ; storage db type, rocksdb / mysql / scalable, rocksdb is recommended\n"
        "    type=rocksdb\n"
        "    ; set true to turn on binary log\n"
        "    binary_log=false\n"
        "    ; scroll_threshold=scroll_threshold_multiple*1000, only for scalable\n"
        "    scroll_threshold_multiple=2\n"
        "    ; set fasle to disable CachedStorage\n"
        "    cached_storage=true\n"
        "    ; max cache memeory, MB\n"
        "    max_capacity=32\n"
        "    max_forward_block=10\n"
        "    ; only for external, deprecated in v2.3.0\n"
        "    max_retry=60\n"
        "    topic=DB\n"
        "    ; only for mysql\n"
        "    db_ip=127.0.0.1\n"
        "    db_port=3306\n"
        "    db_username=\n"
        "    db_passwd=\n"
        "    db_name=\n"
        "[tx_pool]\n"
        "    limit=150000\n"
        "    ; transaction pool memory size limit, MB\n"
        "    memory_limit=512\n"
        "    ; number of threads responsible for transaction notification,\n"
        "    ; default is 2, not recommended for more than 8\n"
        "    notify_worker_num=2\n"
        "[sync]\n"
        "    ; max memory size used for block sync, must >= 32MB\n"
        "    max_block_sync_memory_size=512\n"
        "    idle_wait_ms=200\n"
        "    ; send block status by tree-topology, only supported when use pbft\n"
        "    sync_block_by_tree=true\n"
        "    ; send transaction by tree-topology, only supported when use pbft\n"
        "    ; recommend to use when deploy many consensus nodes\n"
        "    send_txs_by_tree=true\n"
        "    ; must between 1000 to 3000\n"
        "    ; only enabled when sync_by_tree is true\n"
        "    gossip_interval_ms=1000\n"
        "    gossip_peers_number=3\n"
        "    ; max number of nodes that broadcast txs status to, recommended less than 5\n"
        "    txs_max_gossip_peers_num=5\n"
        "[flow_control]\n"
        "    ; restrict QPS of the group\n"
        "    ;limit_req=1000\n"
        "    ; restrict the outgoing bandwidth of the group\n"
        "    ; Mb, can be a decimal\n"
        "    ; when the outgoing bandwidth exceeds the limit, the block synchronization operation "
        "will not proceed\n"
        "    ;outgoing_bandwidth_limit=2\n"
        "[sdk_allowlist]\n"
        "    ; When sdk_allowlist is empty, all SDKs can connect to this node\n"
        "    ; when sdk_allowlist is not empty, only the SDK in the allowlist can connect to this "
        "node\n"
        "    ; public_key.0 should be nodeid, nodeid's length is 128\n"
        "    ;public_key.0=\n";
    return configTemplate;
}

void LedgerManager::startByGroupID(GROUP_ID const& _groupID)
{
    RecursiveGuard l(x_ledgerManager);
    checkGroupStatus(_groupID, LedgerStatus::STOPPED);
    assert(m_ledgerMap.count(_groupID) != 0);

    try
    {
        m_ledgerMap[_groupID]->startAll();
    }
    catch (...)
    {
        // Some modules may have already been started, need to release resources
        m_groupListCache.erase(_groupID);
        auto ledger = m_ledgerMap[_groupID];
        m_ledgerMap.erase(_groupID);
        ledger->stopAll();

        throw;
    }
    setGroupStatus(_groupID, LedgerStatus::RUNNING);
}

void LedgerManager::stopByGroupID(GROUP_ID const& _groupID)
{
    shared_ptr<LedgerInterface> ledger;
    {
        RecursiveGuard l(x_ledgerManager);
        checkGroupStatus(_groupID, LedgerStatus::RUNNING);
        setGroupStatus(_groupID, LedgerStatus::STOPPING);
        m_groupListCache.erase(_groupID);
        ledger = m_ledgerMap[_groupID];
        m_ledgerMap.erase(_groupID);
    }

    ledger->stopAll();

    {
        RecursiveGuard l(x_ledgerManager);
        setGroupStatus(_groupID, LedgerStatus::STOPPED);
    }
#ifndef FISCO_DEBUG
    MallocExtension::instance()->ReleaseFreeMemory();
#endif
}

void LedgerManager::removeByGroupID(dev::GROUP_ID const& _groupID)
{
    RecursiveGuard l(x_ledgerManager);
    checkGroupStatus(_groupID, LedgerStatus::STOPPED);

    setGroupStatus(_groupID, LedgerStatus::DELETED);
}

void LedgerManager::recoverByGroupID(dev::GROUP_ID const& _groupID)
{
    RecursiveGuard l(x_ledgerManager);
    checkGroupStatus(_groupID, LedgerStatus::DELETED);

    setGroupStatus(_groupID, LedgerStatus::STOPPED);
}

std::set<dev::GROUP_ID> LedgerManager::getGroupListForRpc() const
{
    std::set<dev::GROUP_ID> groupList;
    for (auto const& ledger : m_ledgerMap)
    {
        // check sealer list
        auto sealerList = ledger.second->blockChain()->sealerList();
        auto it_sealer = find(sealerList.begin(), sealerList.end(), ledger.second->keyPair().pub());
        if (it_sealer != sealerList.end())
        {
            groupList.insert(ledger.second->groupId());
            continue;
        }
        /// check observer list
        auto observerList = ledger.second->blockChain()->observerList();
        auto it_observer =
            find(observerList.begin(), observerList.end(), ledger.second->keyPair().pub());
        if (it_observer != observerList.end())
        {
            groupList.insert(ledger.second->groupId());
        }
    }
    return groupList;
}
