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
#include <initializer/Param.h>
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
namespace dev
{
namespace ledger
{
void Ledger::initLedger(
    std::unordered_map<dev::Address, dev::eth::PrecompiledContract> const& preCompile,
    dev::blockverifier::BlockVerifier::NumberHashCallBackFunction const& pFunc)
{
    assert(m_param);
    m_groupId = m_param->groupId();
    /// init the DB
    m_dbInitializer->initDBModules(preCompile);
    /// init blockChain
    initBlockChain();
    /// intit blockVerifier
    initBlockVerifier(pFunc);
    /// init txPool
    initTxPool();
    /// init sync
    initSync();
    /// init consensus
    consensusInitFactory();
}

/// init txpool
void Ledger::initTxPool()
{
    assert(m_blockChain);
    dev::eth::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::TxPool);
    m_txPool = std::make_shared<dev::txpool::TxPool>(
        m_service, m_blockChain, protocol_id, m_param->txPoolParam().txPoolLimit);
}

/// init blockVerifier
void Ledger::initBlockVerifier(BlockVerifier::NumberHashCallBackFunction const& pFunc)
{
    std::shared_ptr<BlockVerifier> blockVerifier = std::make_shared<BlockVerifier>();
    m_blockVerifier = blockVerifier;
    /// set params for blockverifier
    blockVerifier->setExecutiveContextFactory(m_dbInitializer->executiveContextFactory());
    blockVerifier->setNumberHash(pFunc);
}

/// TODO: init blockchain
void Ledger::initBlockChain()
{
    std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();
    m_blockChain = std::shared_ptr<BlockChainInterface>(blockChain.get());
    blockChain->setMemoryTableFactory(m_dbInitializer->memoryTableFactory());
}

/**
 * @brief: create PBFTEngine
 * @param param: Ledger related params
 * @return std::shared_ptr<ConsensusInterface>: created consensus engine
 */
std::shared_ptr<Consensus> Ledger::createPBFTConsensus()
{
    assert(m_txPool && m_blockChain && m_sync && m_blockVerifier);
    dev::eth::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::PBFT);
    /// create consensus engine according to "consensusType"
    std::shared_ptr<Consensus> pbftConsensus = std::make_shared<PBFTConsensus>(m_service, m_txPool,
        m_blockChain, m_sync, m_blockVerifier, protocol_id, m_param->baseDir(), m_param->keyPair(),
        m_param->consensusParam().minerList);
    pbftConsensus->setMaxBlockTransactions(m_param->consensusParam().maxTransactions);
    /// set params for PBFTEngine
    std::shared_ptr<PBFTEngine> pbftEngine =
        std::dynamic_pointer_cast<PBFTEngine>(pbftConsensus->consensusEngine());
    pbftEngine->setIntervalBlockTime(m_param->consensusParam().intervalBlockTime);
    return pbftConsensus;
}

/// init consensus
void Ledger::consensusInitFactory()
{
    /// default create pbft consensus
    if (dev::stringCmpIgnoreCase(m_param->consensusParam().consensusType, "pbft") == 0)
        m_consensus = createPBFTConsensus();
}

void Ledger::initSync()
{
    assert(m_txPool && m_blockChain && m_blockVerifier);
    dev::eth::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    m_sync = std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain, m_blockVerifier,
        protocol_id, m_param->keyPair().pub(), m_param->genesisParam().genesisHash,
        m_param->syncParam().idleWaitMs);
}
}  // namespace ledger
}  // namespace dev