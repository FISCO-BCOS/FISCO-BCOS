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
namespace dev
{
namespace ledger
{
template <typename T>
class LedgerManager
{
public:
    virtual inline bool initSingleLedger(
        std::unordered_map<dev::Address, dev::eth::PrecompiledContract> const& preCompile,
        std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair, std::string const& _baseDir = "data",
        std::string const& configFileName = "")
    {
        if (m_ledgerMap.count(_groupId) == 0)
        {
            std::shared_ptr<LedgerInterface> ledger =
                std::make_shared<T>(service, _groupId, _keyPair, _baseDir, configFileName);
            LOG(DEBUG) << "Init Ledger for group:" << _groupId;
            ledger->initLedger(preCompile);
            m_ledgerMap.insert(std::make_pair(_groupId, ledger));
            return true;
        }
        else
        {
            LOG(WARNING) << "Group " << _groupId << " has been created already";
            return false;
        }
    }

    virtual inline bool startByGroupID(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return false;
        m_ledgerMap[groupId]->startAll();
        return true;
    }

    virtual inline bool stopByGroupID(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return false;
        m_ledgerMap[groupId]->stopAll();
        return true;
    }

    virtual inline void startAll()
    {
        for (auto item : m_ledgerMap)
            item.second->startAll();
    }

    virtual inline void stopAll()
    {
        for (auto item : m_ledgerMap)
            item.second->stopAll();
    }
    /// get pointer of txPool by group id
    inline std::shared_ptr<dev::txpool::TxPoolInterface> txPool(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->txPool();
    }

    /// get pointer of blockverifier by group id
    inline std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier(
        dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockVerifier();
    }
    /// get pointer of blockchain by group id
    inline std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain(
        dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockChain();
    }
    /// get pointer of consensus by group id
    inline std::shared_ptr<dev::consensus::ConsensusInterface> consensus(
        dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->consensus();
    }
    /// get pointer of blocksync by group id
    inline std::shared_ptr<dev::sync::SyncInterface> sync(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->sync();
    }

    inline std::shared_ptr<LedgerParamInterface> getParamByGroupId(dev::GROUP_ID const& groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->getParam();
    }

private:
    std::map<dev::GROUP_ID, std::shared_ptr<LedgerInterface>> m_ledgerMap;
};
}  // namespace ledger
}  // namespace dev
