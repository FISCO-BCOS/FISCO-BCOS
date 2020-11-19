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
 * @brief : header file of Raft consensus sealer
 * @file: RaftSealer.h
 * @author: catli
 * @date: 2018-12-05
 */
#pragma once
#include "Common.h"
#include "RaftEngine.h"
#include <libconsensus/Sealer.h>
#include <memory>

namespace bcos
{
namespace consensus
{
class RaftSealer : public Sealer
{
public:
    RaftSealer(std::shared_ptr<bcos::p2p::P2PInterface> _service,
        std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<bcos::sync::SyncInterface> _blockSync,
        std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> _blockVerifier,
        KeyPair const& _keyPair, unsigned _minElectTime, unsigned _maxElectTime,
        bcos::PROTOCOL_ID const& _protocolId, bcos::h512s const& _sealerList = bcos::h512s())
      : Sealer(_txPool, _blockChain, _blockSync)
    {
        m_consensusEngine = std::make_shared<RaftEngine>(_service, _txPool, _blockChain, _blockSync,
            _blockVerifier, _keyPair, _minElectTime, _maxElectTime, _protocolId, _sealerList);
        m_raftEngine = std::dynamic_pointer_cast<RaftEngine>(m_consensusEngine);

        /// set thread name for RaftSealer
        std::string threadName = "RaftSeal-" + std::to_string(m_raftEngine->groupId());
        setName(threadName);
    }
    void start() override;
    void stop() override;

protected:
    void handleBlock() override;
    bool shouldSeal() override;
    void reset()
    {
        resetSealingBlock();
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }

    bool reachBlockIntervalTime() override;

private:
    std::shared_ptr<RaftEngine> m_raftEngine;
};
}  // namespace consensus
}  // namespace bcos
