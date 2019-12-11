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
#include "Ledger.h"
#include "LedgerInterface.h"
#include <libethcore/Protocol.h>
#include <map>
#define LedgerManager_LOG(LEVEL) LOG(LEVEL) << "[LEDGERMANAGER] "
namespace dev
{
namespace ledger
{
class LedgerManager
{
public:
    /**
     * @brief: constructor of LedgerManager
     * @param _service: p2p service handler used to send/receive messages
     * @param _keyPair: the keyPair used to init consensus module
     * @param _preCompile: map that stores the PrecompiledContract (required by blockverifier)
     */
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
        if (_groupId <= 0)
        {
            LedgerManager_LOG(ERROR)
                << "[initSingleLedger] invalid GroupId: " << _groupId << ", must between [1"
                << ", " << maxGroupID << "]";
            return false;
        }
        auto ret = m_ledgerMap.insert(std::make_pair(_groupId, ledger));
        if (ret.second)
        {
            WriteGuard l(x_groupListCache);
            m_groupListCache.insert(_groupId);
        }

        return ret.second;
    }

    bool removeLedger(dev::GROUP_ID const& _groupId)
    {
        bool success = stopByGroupID(_groupId);
        if (!success)
        {
            return false;
        }

        WriteGuard l(x_groupListCache);
        m_groupListCache.erase(_groupId);
        m_ledgerMap.erase(_groupId);
        return true;
    }

    bool isLedgerExist(dev::GROUP_ID const& _groupId) { return m_ledgerMap.count(_groupId); }
    /**
     * @brief : start a single ledger by groupId
     * @param groupId : the ledger need to be started
     * @return true : start success
     * @return false : start failed (maybe the ledger doesn't exist)
     */
    virtual bool startByGroupID(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return false;
        m_ledgerMap[groupId]->startAll();
        return true;
    }

    /**
     * @brief: stop the ledger by group id
     * @param groupId: the groupId of the ledger need to be stopped
     * @return true: stop the ledger succeed
     * @return false: stop the ledger failed
     */
    virtual bool stopByGroupID(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return false;
        m_ledgerMap[groupId]->stopAll();
        return true;
    }

    /// start all the ledgers that have been created
    virtual void startAll()
    {
        for (auto item : m_ledgerMap)
        {
            if (!item.second)
                continue;
            item.second->startAll();
        }
    }
    /// stop all the ledgers that have been started
    virtual void stopAll()
    {
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
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->txPool();
    }

    /// get pointer of blockverifier by group id
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier(
        dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockVerifier();
    }

    /// get ledger
    std::shared_ptr<LedgerInterface> ledger(dev::GROUP_ID const& groupId)
    {
        auto it = m_ledgerMap.find(groupId);
        return (it == m_ledgerMap.end() ? nullptr : it->second);
    }

    /// get pointer of blockchain by group id
    std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockChain();
    }
    /// get pointer of consensus by group id
    std::shared_ptr<dev::consensus::ConsensusInterface> consensus(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->consensus();
    }
    /// get pointer of blocksync by group id
    std::shared_ptr<dev::sync::SyncInterface> sync(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->sync();
    }
    /// get ledger params by group id
    std::shared_ptr<LedgerParamInterface> getParamByGroupId(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->getParam();
    }

    std::set<dev::GROUP_ID> getGroupListForRpc() const;
    std::set<dev::GROUP_ID> const& getGroupList() const
    {
        ReadGuard l(x_groupListCache);
        return m_groupListCache;
    }


private:
    mutable SharedMutex x_groupListCache;
    /// cache for the group List
    std::set<dev::GROUP_ID> m_groupListCache;

    /// map used to store the mappings between groupId and created ledger objects
    std::map<dev::GROUP_ID, std::shared_ptr<LedgerInterface>> m_ledgerMap;
};
}  // namespace ledger
}  // namespace dev
