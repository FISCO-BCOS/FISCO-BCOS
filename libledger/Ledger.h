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
 * @file: Ledger.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "LedgerInterface.h"
#include <initializer/Param.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/Service.h>
namespace dev
{
namespace ledger
{
class Ledger : public LedgerInterface
{
public:
    Ledger(std::shared_ptr<dev::p2p::P2PInterface> service) : m_service(service)
    {
        assert(m_service);
    }

    virtual ~Ledger(){};
    /// init the ledger(called by initializer)
    bool initLedger(std::shared_ptr<dev::initializer::LedgerParamInterface> param) override;
    std::shared_ptr<dev::txpool::TxPoolInterface> txPool() const override { return m_txPool; }

    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier() const override
    {
        return m_blockVerifier;
    }
    std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain() const override
    {
        return m_blockChain;
    }
    std::shared_ptr<dev::consensus::ConsensusInterface> consensus() const override
    {
        return m_consensus->consensusEngine();
    }
    std::shared_ptr<dev::sync::SyncInterface> sync() const override { return m_sync; }

protected:
    virtual bool initTxPool(dev::initializer::TxPoolParam const& param);
    virtual bool initBlockVerifier();
    virtual bool initBlockChain(dev::initializer::BlockChainParam const& param);
    virtual bool initConsensus(dev::initializer::ConsensusParam const& param);
    virtual bool initSync(dev::initializer::SyncParam const& param);

private:
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    std::shared_ptr<dev::consensus::Consensus> m_consensus;
    std::shared_ptr<dev::sync::SyncInterface> m_sync;
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
};
}  // namespace ledger
}  // namespace dev
