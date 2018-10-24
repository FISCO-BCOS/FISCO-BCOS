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
 */
#pragma once
#include "PBFTEngine.h"
#include <libconsensus/Consensus.h>
#include <sstream>
namespace dev
{
namespace consensus
{
class PBFTConsensus : public Consensus
{
public:
    PBFTConsensus(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, std::string const& _baseDir, KeyPair const& _key_pair,
        h512s const& _minerList = h512s())
      : Consensus(_txPool, _blockChain, _blockSync)
    {
        m_consensusEngine = std::make_shared<PBFTEngine>(_service, _txPool, _blockChain, _blockSync,
            _blockVerifier, _protocolId, _baseDir, _key_pair, _minerList);
        m_pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(m_consensusEngine);
        m_pbftEngine->onViewChange([this]() {
            DEV_WRITE_GUARDED(x_sealing)
            {
                LOG(DEBUG) << "#### callbacked after viewchange";
                if (shouldResetSealing())
                {
                    LOG(DEBUG) << "#### reset the sealing, number = "
                               << m_sealing.block.header().number();
                    resetSealingBlock();
                }
                m_signalled.notify_all();
            }
        });
    }
    void start() override;
    void stop() override;

protected:
    void handleBlock() override;
    bool shouldSeal() override;
    virtual bool reachBlockIntervalTime() override
    {
        return m_pbftEngine->reachBlockIntervalTime();
    }
    uint64_t calculateMaxPackTxNum() override;

private:
    void setBlock();

protected:
    std::shared_ptr<PBFTEngine> m_pbftEngine;
};
}  // namespace consensus
}  // namespace dev
