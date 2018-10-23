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
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "LedgerInterface.h"
#include <initializer/ParamInterface.h>
#include <map>
namespace dev
{
namespace ledger
{
class LedgerManager
{
public:
    virtual bool initAllLedgers(
        std::vector<std::shared_ptr<dev::initializer::LedgerParamInterface>> allLedgerParams);
    virtual bool initSignleLedger(
        std::shared_ptr<dev::initializer::LedgerParamInterface> ledgerParam);

    inline std::shared_ptr<dev::txpool::TxPoolInterface> txPool(uint16_t groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->txPool();
    }

    inline std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier(
        uint16_t groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockVerifier();
    }

    inline std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain(uint16_t groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->blockChain();
    }
    inline std::shared_ptr<dev::consensus::ConsensusInterface> consensus(uint16_t groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->consensus();
    }
    inline std::shared_ptr<dev::sync::SyncInterface> sync(uint16_t groupId)
    {
        if (!m_ledgerMap.count(groupId))
            return nullptr;
        return m_ledgerMap[groupId]->sync();
    }

private:
    std::map<int16_t, std::shared_ptr<LedgerInterface>> m_ledgerMap;
};
}  // namespace ledger
}  // namespace dev