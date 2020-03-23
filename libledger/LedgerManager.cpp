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

string LedgerManager::getGroupStatusFilePath(GROUP_ID const& _groupID) const
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

LedgerStatus LedgerManager::queryGroupStatus(dev::GROUP_ID const& _groupID)
{
    RecursiveGuard l(x_ledgerManager);
    if (!fs::exists(getGroupStatusFilePath(_groupID)))
    {
        return LedgerStatus::INEXISTENT;
    }

    string status = "";
    ifstream statusFile;
    statusFile.open(getGroupStatusFilePath(_groupID));
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
        RecursiveGuard l(x_ledgerManager);
        ofstream statusFile;
        statusFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        statusFile.open(getGroupStatusFilePath(_groupID));
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
    }
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
        clearLedger(_groupID);
        throw;
    }
    setGroupStatus(_groupID, LedgerStatus::RUNNING);
}

void LedgerManager::stopByGroupID(GROUP_ID const& _groupID)
{
    RecursiveGuard l(x_ledgerManager);
    checkGroupStatus(_groupID, LedgerStatus::RUNNING);

    setGroupStatus(_groupID, LedgerStatus::STOPPING);
    clearLedger(_groupID);
    setGroupStatus(_groupID, LedgerStatus::STOPPED);
}

void LedgerManager::clearLedger(GROUP_ID const& _groupID)
{
    // the caller of this function should acquire x_legerManager first!

    // first, remove ledger handler to stop service
    m_groupListCache.erase(_groupID);
    auto ledger = m_ledgerMap[_groupID];
    m_ledgerMap.erase(_groupID);

    // second, stop all service
    ledger->stopAll();
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
