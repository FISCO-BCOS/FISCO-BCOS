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
#include <libconsensus/pbft/PBFTEngine.h>
#include <libconsensus/pbft/PBFTSealer.h>
#include <libdevcore/OverlayDB.h>
#include <libdevcore/easylog.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>
#include <boost/property_tree/ini_parser.hpp>
using namespace boost::property_tree;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::consensus;
using namespace dev::sync;
namespace dev
{
namespace ledger
{
void Ledger::initLedger(
    std::unordered_map<dev::Address, dev::eth::PrecompiledContract> const& preCompile)
{
    assert(m_param);
    /// init dbInitializer
    m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param);
    /// init the DB
    m_dbInitializer->initDBModules(preCompile);
    /// init blockChain
    initBlockChain();
    /// intit blockVerifier
    initBlockVerifier();
    /// init txPool
    initTxPool();
    /// init sync
    initSync();
    /// init consensus
    consensusInitFactory();
}

/**
 * @brief: init configuration related to the ledger with specified configuration file
 * @param configPath: the path of the config file
 */
void Ledger::initConfig(std::string const& configPath)
{
    try
    {
        ptree pt;
        /// read the configuration file for a specified group
        read_ini(configPath, pt);
        /// init params related to txpool
        initTxPoolConfig(pt);
        /// init params related to consensus
        initConsensusConfig(pt);
        /// init params related to sync
        initSyncConfig(pt);
        /// db params initialization
        initDBConfig(pt);
        initGenesisConfig(pt);
    }
    catch (std::exception& e)
    {
        std::string error_info =
            "init config failed for " + toString(m_groupId) + " failed, error_msg:" + e.what();
        LOG(ERROR) << error_info;
        BOOST_THROW_EXCEPTION(dev::InitLedgerConfigFailed() << errinfo_comment(error_info));
    }
}

void Ledger::initTxPoolConfig(ptree const& pt)
{
    m_param->mutableTxPoolParam().txPoolLimit = pt.get<uint64_t>("txPool.limit", 102400);
    LOG(DEBUG) << "init txPool.limit = " << m_param->mutableTxPoolParam().txPoolLimit;
}

/// init consensus configurations:
/// 1. consensusType: current support pbft only (default is pbft)
/// 2. maxTransNum: max number of transactions can be sealed into a block
/// 3. intervalBlockTime: average block generation period
/// 4. miner.${idx}: define the node id of every miner related to the group
void Ledger::initConsensusConfig(ptree const& pt)
{
    m_param->mutableConsensusParam().consensusType =
        pt.get<std::string>("consensus.consensusType", "pbft");
    LOG(DEBUG) << "Init consensus.consensusType = "
               << m_param->mutableConsensusParam().consensusType;

    m_param->mutableConsensusParam().maxTransactions =
        pt.get<uint64_t>("consensus.maxTransNum", 1000);
    LOG(DEBUG) << "Init consensus.maxTransNum = "
               << m_param->mutableConsensusParam().maxTransactions;

    m_param->mutableConsensusParam().intervalBlockTime =
        pt.get<unsigned>("consensus.intervalBlockTime", 1000);

    LOG(DEBUG) << "init consensus.intervalBlockTime ="
               << m_param->mutableConsensusParam().intervalBlockTime;
    try
    {
        for (auto it : pt.get_child("consensus"))
        {
            if (it.first.find("miner.") == 0)
            {
                LOG(DEBUG) << "Add " << it.first << " : " << it.second.data();
                h512 miner(it.second.data());
                m_param->mutableConsensusParam().minerList.push_back(miner);
            }
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Parse consensus section failed:" << e.what();
    }
}

/// init sync related configurations
/// 1. idleWaitMs: default is 30ms
void Ledger::initSyncConfig(ptree const& pt)
{
    m_param->mutableSyncParam().idleWaitMs = pt.get<unsigned>("sync.idleWaitMs", 30);
    LOG(DEBUG) << "Init Sync.idleWaitMs = " << m_param->mutableSyncParam().idleWaitMs;
}

/// init db related configurations:
/// dbType: leveldb/AMDB, storage type, default is "AMDB"
/// mpt: true/false, enable mpt or not, default is true
/// dbpath: data to place all data of the group, default is "data"
void Ledger::initDBConfig(ptree const& pt)
{
    /// init the basic config
    m_param->setDBType(pt.get<std::string>("statedb.dbType", "AMDB"));
    m_param->setMptState(pt.get<bool>("statedb.mpt", true));
    std::string baseDir = m_param->baseDir() + "/" + pt.get<std::string>("statedb.dbpath", "data");
    m_param->setBaseDir(baseDir);
}

/// init genesis configuration
/// 1. hash: hash of the genesis
void Ledger::initGenesisConfig(ptree const& pt)
{
    m_param->mutableGenesisParam().genesisHash = pt.get<dev::h256>("genesis.hash", h256());
    m_param->mutableGenesisParam().accountStartNonce = pt.get<u256>("genesis.nonce", u256(0x0));
}

/// init txpool
void Ledger::initTxPool()
{
    assert(m_blockChain);
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::TxPool);
    m_txPool = std::make_shared<dev::txpool::TxPool>(
        m_service, m_blockChain, protocol_id, m_param->mutableTxPoolParam().txPoolLimit);
}

/// init blockVerifier
void Ledger::initBlockVerifier()
{
    std::shared_ptr<BlockVerifier> blockVerifier = std::make_shared<BlockVerifier>();
    m_blockVerifier = blockVerifier;
    /// set params for blockverifier
    blockVerifier->setExecutiveContextFactory(m_dbInitializer->executiveContextFactory());
    std::shared_ptr<BlockChainImp> blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(m_blockChain);
    blockVerifier->setNumberHash(boost::bind(&BlockChainImp::numberHash, blockChain, _1));
}

/// TODO: init blockchain
void Ledger::initBlockChain()
{
    std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();
    m_blockChain = std::shared_ptr<BlockChainInterface>(blockChain.get());
}

/**
 * @brief: create PBFTEngine
 * @param param: Ledger related params
 * @return std::shared_ptr<ConsensusInterface>: created consensus engine
 */
std::shared_ptr<Sealer> Ledger::createPBFTSealer()
{
    assert(m_txPool && m_blockChain && m_sync && m_blockVerifier);
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::PBFT);
    /// create consensus engine according to "consensusType"
    LOG(DEBUG) << "create PBFTConsensus, baseDir:" << m_param->baseDir()
               << " , protocol_id:" << protocol_id;
    std::shared_ptr<Sealer> pbftSealer =
        std::make_shared<PBFTSealer>(m_service, m_txPool, m_blockChain, m_sync, m_blockVerifier,
            protocol_id, m_param->baseDir(), m_keyPair, m_param->mutableConsensusParam().minerList);
    pbftSealer->setMaxBlockTransactions(m_param->mutableConsensusParam().maxTransactions);
    /// set params for PBFTEngine
    std::shared_ptr<PBFTEngine> pbftEngine =
        std::dynamic_pointer_cast<PBFTEngine>(pbftSealer->consensusEngine());
    pbftEngine->setIntervalBlockTime(m_param->mutableConsensusParam().intervalBlockTime);
    return pbftSealer;
}

/// init consensus
void Ledger::consensusInitFactory()
{
    LOG(DEBUG) << "#### Configurated Consensus type:"
               << m_param->mutableConsensusParam().consensusType;
    /// default create pbft consensus
    if (dev::stringCmpIgnoreCase(m_param->mutableConsensusParam().consensusType, "pbft") == 0)
    {
        m_sealer = createPBFTSealer();
    }
    else
    {
        std::string error_msg =
            "Unsupported Consensus type:" + m_param->mutableConsensusParam().consensusType;
        LOG(ERROR) << error_msg;
        BOOST_THROW_EXCEPTION(dev::InvalidConsensusType() << errinfo_comment(error_msg));
    }
}

/// init sync
void Ledger::initSync()
{
    assert(m_txPool && m_blockChain && m_blockVerifier);
    dev::PROTOCOL_ID protocol_id = getGroupProtoclID(m_groupId, ProtocolID::BlockSync);
    m_sync = std::make_shared<SyncMaster>(m_service, m_txPool, m_blockChain, m_blockVerifier,
        protocol_id, m_keyPair.pub(), m_param->mutableGenesisParam().genesisHash,
        m_param->mutableSyncParam().idleWaitMs);
}
}  // namespace ledger
}  // namespace dev
