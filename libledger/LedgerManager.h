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
 * @file: LedgerManager.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "Common.h"
#include "Ledger.h"
#include "LedgerInterface.h"
#include <libethcore/Protocol.h>
#include <map>


#define LedgerManager_LOG(LEVEL) LOG(LEVEL) << "[LEDGERMANAGER] "

namespace dev
{
namespace ledger
{
enum class LedgerStatus
{
    INEXISTENT,
    RUNNING,
    STOPPED,
    STOPPING,
    DELETED,
};

class LedgerManager
{
public:
    LedgerManager() = default;
    virtual ~LedgerManager() = default;

    /**
     * @brief : init a single ledger with the given params
     *
     * @param _groupId : the groupId of the ledger need to be inited
     * @return true: init single ledger succeed
     * @return false: init single ledger failed
     */
    bool insertLedger(dev::GROUP_ID const& _groupId, std::shared_ptr<LedgerInterface> ledger)
    {
        RecursiveGuard l(x_ledgerManager);
        auto ret = m_ledgerMap.insert(std::make_pair(_groupId, ledger));
        if (ret.second)
        {
            m_groupListCache.insert(_groupId);
        }

        return ret.second;
    }

    bool isLedgerExist(dev::GROUP_ID const& _groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        return m_ledgerMap.count(_groupId) != 0;
    }

    bool isLedgerHaltedBefore(dev::GROUP_ID const& _groupID)
    {
        RecursiveGuard l(x_ledgerManager);
        auto status = queryGroupStatus(_groupID);
        // if a group was marked as `STOPPED` or `DELETED`, it was halted before
        if (status == LedgerStatus::STOPPED || status == LedgerStatus::DELETED)
        {
            return true;
        }
        // the group who marked as `STOPPING` had tried to stop itself before
        // reboot of the node. Now, it's stopped physically, so change the mark
        // to `STOPPED`
        if (status == LedgerStatus::STOPPING)
        {
            setGroupStatus(_groupID, LedgerStatus::STOPPED);
            return true;
        }

        return false;
    }

    /**
     * @brief : create a single ledger by group ID
     * @param _groupID : the ledger need to be created
     * @param _timestamp: timestamp of the genesis block
     * @param _sealerList: sealers of the group
     */
    void generateGroup(dev::GROUP_ID _groupId, const std::string& _timestamp,
        const std::set<std::string>& _sealerList);

    /**
     * @brief : start a single ledger by group ID
     * @param _groupID : the ledger need to be started
     */
    void startByGroupID(dev::GROUP_ID const& _groupID);

    /**
     * @brief: stop the ledger by group ID
     * @param _groupID: the groupId of the ledger need to be stopped
     */
    void stopByGroupID(dev::GROUP_ID const& _groupID);

    /**
     * @brief: delete the ledger by group ID
     * @param _groupID: the groupId of the ledger need to be deleted
     */
    void removeByGroupID(dev::GROUP_ID const& _groupID);

    /**
     * @brief: recover the ledger by group ID
     * @param _groupID: the groupId of the ledger need to be recovered
     */
    void recoverByGroupID(dev::GROUP_ID const& _groupID);

    /**
     * @brief: query the ledger status by group ID
     * @param _groupID: the groupId of the ledger
     */
    LedgerStatus queryGroupStatus(dev::GROUP_ID const& groupID);

    /**
     * @brief: change the ledger status by group ID
     * @param _groupID: the groupId of the ledger
     * @param _status: new status of the ledger
     */
    void setGroupStatus(dev::GROUP_ID const& _groupID, LedgerStatus _status);

    /// start all the ledgers that have been created
    virtual void startAll()
    {
        RecursiveGuard l(x_ledgerManager);
        for (auto item : m_ledgerMap)
        {
            if (!item.second)
                continue;
            item.second->startAll();
            setGroupStatus(item.first, LedgerStatus::RUNNING);
        }
    }
    /// stop all the ledgers that have been started
    virtual void stopAll()
    {
        RecursiveGuard l(x_ledgerManager);
        for (auto item : m_ledgerMap)
        {
            if (!item.second)
                continue;
            item.second->stopAll();
        }
    }
    /// get pointer of txPool by group id
    std::shared_ptr<dev::txpool::TxPoolInterface> txPool(dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->txPool();
    }

    /// get pointer of blockverifier by group id
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier(
        dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockVerifier();
    }

    /// get ledger
    std::shared_ptr<LedgerInterface> ledger(dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        auto it = m_ledgerMap.find(groupId);
        return (it == m_ledgerMap.end() ? nullptr : it->second);
    }

    /// get pointer of blockchain by group id
    std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain(dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockChain();
    }
    /// get pointer of consensus by group id
    std::shared_ptr<dev::consensus::ConsensusInterface> consensus(dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->consensus();
    }
    /// get pointer of blocksync by group id
    std::shared_ptr<dev::sync::SyncInterface> sync(dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->sync();
    }
    /// get ledger params by group id
    std::shared_ptr<LedgerParamInterface> getParamByGroupId(dev::GROUP_ID const& groupId)
    {
        RecursiveGuard l(x_ledgerManager);
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->getParam();
    }

    std::set<dev::GROUP_ID> getGroupListForRpc() const;
    std::set<dev::GROUP_ID> const& getGroupList() const
    {
        RecursiveGuard l(x_ledgerManager);
        return m_groupListCache;
    }

private:
    void checkGroupStatus(dev::GROUP_ID const& _groupID, LedgerStatus _allowedStatus);
    std::string getGroupStatusFilePath(dev::GROUP_ID const& _groupID) const;

    std::string generateGenesisConfig(dev::GROUP_ID _groupId, const std::string& _timestamp,
        const std::set<std::string>& _sealerList);
    std::string generateGroupConfig();

    void clearLedger(dev::GROUP_ID const& _groupID);

    mutable RecursiveMutex x_ledgerManager;
    /// cache for the group List
    std::set<dev::GROUP_ID> m_groupListCache;

    /// map used to store the mappings between groupId and created ledger objects
    std::map<dev::GROUP_ID, std::shared_ptr<LedgerInterface>> m_ledgerMap;
};
}  // namespace ledger
}  // namespace dev
