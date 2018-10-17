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
#include "Common.h"
#include "PBFTEngine.h"
#include "PBFTMsgCache.h"
#include "PBFTReqCache.h"
#include <libconsensus/Consensus.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/LevelDB.h>
#include <libdevcore/concurrent_queue.h>
#include <libp2p/Session.h>
#include <libp2p/SessionFace.h>
#include <sstream>
namespace dev
{
namespace consensus
{
class PBFTConsensus : public Consensus, public std::enable_shared_from_this<PBFTConsensus>
{
public:
    PBFTConsensus(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::consensus::ConsensusEngineBase> _consensusEngine)
      : Consensus(_txPool, _blockChain, _blockSync, _consensusEngine)
    {
        m_pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(m_consensusEngine);
        assert(m_pbftEngine != nullptr);
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
