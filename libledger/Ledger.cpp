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
 * @file: Ledger.cpp
 * @author: yujiechen
 * @date: 2018-10-23
 */
#include "Ledger.h"
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libconsensus/pbft/PBFTConsensus.h>
#include <libconsensus/pbft/PBFTEngine.h>
#include <libdevcore/OverlayDB.h>
#include <libethcore/Protocol.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::consensus;
using namespace dev::initializer;
using namespace dev::sync;
using namespace dev::db;
namespace dev
{
namespace ledger
{
void Ledger::initLedger(std::shared_ptr<LedgerParamInterface> param)
{
    assert(param);
    m_groupId = param->groupId();
    /// TODO: init blockChain
    initBlockChain(param);
    /// TODO: intit blockVerifier
    initBlockVerifier(param);
    initTxPool(param);
    initSync(param);
    consensusInitFactory(param);
}

/// init txpool
void Ledger::initTxPool(std::shared_ptr<LedgerParamInterface> param)
{
    assert(m_blockChain);
    dev::eth::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::TxPool);
    m_txPool = std::make_shared<dev::txpool::TxPool>(
        m_service, m_blockChain, protocol_id, param->txPoolParam().txPoolLimit);
}

/// TODO: init blockVerifier
void Ledger::initBlockVerifier(std::shared_ptr<LedgerParamInterface> param) {}

/// TODO: init blockchain
void Ledger::initBlockChain(std::shared_ptr<LedgerParamInterface> param) {}

/**
 * @brief: create PBFTEngine
 *
 * @param param: Ledger related params
 * @return std::shared_ptr<ConsensusInterface>: created consensus engine
 */
std::shared_ptr<Consensus> Ledger::createPBFTConsensus(std::shared_ptr<LedgerParamInterface> param)
{
    assert(m_txPool && m_blockChain && m_sync && m_blockVerifier);
    dev::eth::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::PBFT);
    /// create consensus engine according to "consensusType"
    std::shared_ptr<Consensus> pbftConsensus =
        std::make_shared<PBFTConsensus>(m_service, m_txPool, m_blockChain, m_sync, m_blockVerifier,
            protocol_id, param->baseDir(), param->keyPair(), param->consensusParam().minerList);
    return pbftConsensus;
}

/// init consensus
void Ledger::consensusInitFactory(std::shared_ptr<LedgerParamInterface> param)
{
    /// default create pbft consensus
    m_consensus = createPBFTConsensus(param);
}

void Ledger::initSync(std::shared_ptr<LedgerParamInterface> param)
{
    assert(m_txPool && m_blockChain && m_blockVerifier);
    dev::eth::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    m_sync = std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain, m_blockVerifier,
        protocol_id, param->keyPair().pub(), param->genesisParam().genesisHash,
        param->syncParam().idleWaitMs);
}
}  // namespace ledger
}  // namespace dev