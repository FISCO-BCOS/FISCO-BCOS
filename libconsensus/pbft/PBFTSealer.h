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
 * @brief : implementation of PBFT consensus
 * @file: PBFTConsensus.h
 * @author: yujiechen
 * @date: 2018-09-28
 *
 *
 * @author: yujiechen
 * @file:PBFTSealer.h
 * @date: 2018-10-26
 * @modifications: rename PBFTConsensus.h to PBFTSealer.h
 */
#pragma once
#include "PBFTEngine.h"
#include <libconsensus/Sealer.h>
#include <sstream>
namespace dev
{
namespace consensus
{
class PBFTSealer : public Sealer
{
public:
    PBFTSealer(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, std::string const& _baseDir, KeyPair const& _key_pair,
        h512s const& _minerList = h512s())
      : Sealer(_txPool, _blockChain, _blockSync)
    {
        m_consensusEngine = std::make_shared<PBFTEngine>(_service, _txPool, _blockChain, _blockSync,
            _blockVerifier, _protocolId, _baseDir, _key_pair, _minerList);
        m_pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(m_consensusEngine);
        /// called by viewchange procedure to reset block when timeout
        m_pbftEngine->onViewChange(boost::bind(&PBFTSealer::resetBlockForViewChange, this));
        /// called by the next leader to reset block when it receives the prepare block
        m_pbftEngine->onNotifyNextLeaderReset(
            boost::bind(&PBFTSealer::resetBlockForNextLeader, this, _1));
    }

    void start() override;
    void stop() override;
    /// can reset the sealing block or not?
    bool shouldResetSealing() override
    {
        /// only the leader need reset sealing in PBFT
        return Sealer::shouldResetSealing() &&
               (m_pbftEngine->getLeader().second == m_pbftEngine->nodeIdx());
    }

protected:
    void handleBlock() override;
    bool shouldSeal() override;
    // only the leader can generate the latest block
    bool shouldHandleBlock() override
    {
        return m_sealing.block.blockHeader().number() == (m_blockChain->number() + 1) &&
               (m_pbftEngine->getLeader().second == m_pbftEngine->nodeIdx());
    }

    bool reachBlockIntervalTime() override { return m_pbftEngine->reachBlockIntervalTime(); }
    /// in case of the next leader packeted the number of maxTransNum transactions before the last
    /// block is consensused
    bool canHandleBlockForNextLeader() override
    {
        return m_pbftEngine->canHandleBlockForNextLeader();
    }

private:
    /// reset block when view changes
    void resetBlockForViewChange()
    {
        {
            DEV_WRITE_GUARDED(x_sealing)
            resetSealingBlock();
        }
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }

    /// reset block for the next leader
    void resetBlockForNextLeader(dev::h256Hash const& filter)
    {
        {
            DEV_WRITE_GUARDED(x_sealing)
            resetSealingBlock(filter, true);
        }
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }

    void setBlock();

protected:
    std::shared_ptr<PBFTEngine> m_pbftEngine;
};
}  // namespace consensus
}  // namespace dev
