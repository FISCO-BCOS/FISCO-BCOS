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
 * @file: ConsensusEngineBase.cpp
 * @author: yujiechen
 * @date: 2018-09-28
 */
#include "ConsensusEngineBase.h"
using namespace dev::eth;
using namespace dev::db;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::p2p;
namespace dev
{
namespace consensus
{
void ConsensusEngineBase::start()
{
    if (m_startConsensusEngine)
    {
        ENGINE_LOG(WARNING) << "[ConsensusEngineBase has already been started]";
        return;
    }
    ENGINE_LOG(INFO) << "[Start ConsensusEngineBase]";
    /// start  a thread to execute doWork()&&workLoop()
    startWorking();
    m_startConsensusEngine = true;
}

void ConsensusEngineBase::stop()
{
    if (m_startConsensusEngine == false)
    {
        return;
    }
    ENGINE_LOG(INFO) << "[Stop ConsensusEngineBase]";
    m_startConsensusEngine = false;
    doneWorking();
    if (isWorking())
    {
        stopWorking();
        // will not restart worker, so terminate it
        terminate();
    }
    ENGINE_LOG(INFO) << "ConsensusEngineBase stopped";
}

/// update m_sealing and receiptRoot
dev::blockverifier::ExecutiveContext::Ptr ConsensusEngineBase::executeBlock(Block& block)
{
    auto parentBlock = m_blockChain->getBlockByNumber(m_blockChain->number());
    BlockInfo parentBlockInfo{parentBlock->header().hash(), parentBlock->header().number(),
        parentBlock->header().stateRoot()};
    /// reset execute context
    return m_blockVerifier->executeBlock(block, parentBlockInfo);
}

void ConsensusEngineBase::checkBlockValid(Block const& block)
{
    h256 block_hash = block.blockHeader().hash();
    /// check transaction num
    if (block.getTransactionSize() > maxBlockTransactions())
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: overthreshold transaction num")
                          << LOG_KV("blockTransactionLimit", maxBlockTransactions())
                          << LOG_KV("blockTransNum", block.getTransactionSize());
        BOOST_THROW_EXCEPTION(
            OverThresTransNum() << errinfo_comment("overthreshold transaction num"));
    }

    /// check the timestamp
    if (block.blockHeader().timestamp() > utcTime() && !m_allowFutureBlocks)
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: future timestamp")
                          << LOG_KV("timestamp", block.blockHeader().timestamp())
                          << LOG_KV("utcTime", utcTime()) << LOG_KV("hash", block_hash.abridged());
        BOOST_THROW_EXCEPTION(DisabledFutureTime() << errinfo_comment("Future time Disabled"));
    }
    /// check the block number
    if (block.blockHeader().number() <= m_blockChain->number())
    {
        ENGINE_LOG(DEBUG) << LOG_DESC("checkBlockValid: old height")
                          << LOG_KV("highNumber", m_blockChain->number())
                          << LOG_KV("blockNumber", block.blockHeader().number())
                          << LOG_KV("hash", block_hash.abridged());
        BOOST_THROW_EXCEPTION(InvalidBlockHeight() << errinfo_comment("Invalid block height"));
    }

    /// check the existence of the parent block (Must exist)
    if (!m_blockChain->getBlockByHash(
            block.blockHeader().parentHash(), block.blockHeader().number() - 1))
    {
        ENGINE_LOG(ERROR) << LOG_DESC("checkBlockValid: Parent doesn't exist")
                          << LOG_KV("hash", block_hash.abridged())
                          << LOG_KV("number", block.blockHeader().number());
        BOOST_THROW_EXCEPTION(ParentNoneExist() << errinfo_comment("Parent Block Doesn't Exist"));
    }
    if (block.blockHeader().number() > 1)
    {
        if (m_blockChain->numberHash(block.blockHeader().number() - 1) !=
            block.blockHeader().parentHash())
        {
            ENGINE_LOG(DEBUG)
                << LOG_DESC("checkBlockValid: Invalid block for unconsistent parentHash")
                << LOG_KV("block.parentHash", block.blockHeader().parentHash().abridged())
                << LOG_KV("parentHash",
                       m_blockChain->numberHash(block.blockHeader().number() - 1).abridged());
            BOOST_THROW_EXCEPTION(
                WrongParentHash() << errinfo_comment("Invalid block for unconsistent parentHash"));
        }
    }
}

void ConsensusEngineBase::updateConsensusNodeList()
{
    try
    {
        std::stringstream s2;
        s2 << "[updateConsensusNodeList] Sealers:";
        {
            WriteGuard l(m_sealerListMutex);
            m_sealerList = m_blockChain->sealerList();
            /// to make sure the index of all sealers are consistent
            std::sort(m_sealerList.begin(), m_sealerList.end());
            for (dev::h512 node : m_sealerList)
                s2 << node.abridged() << ",";
        }
        s2 << "Observers:";
        dev::h512s observerList = m_blockChain->observerList();
        for (dev::h512 node : observerList)
            s2 << node.abridged() << ",";
        ENGINE_LOG(TRACE) << s2.str();

        if (m_lastNodeList != s2.str())
        {
            ENGINE_LOG(TRACE) << "[updateConsensusNodeList] update P2P List done.";
            updateNodeListInP2P();
            m_lastNodeList = s2.str();
        }
    }
    catch (std::exception& e)
    {
        ENGINE_LOG(ERROR)
            << "[updateConsensusNodeList] update consensus node list failed [EINFO]:  "
            << boost::diagnostic_information(e);
    }
}

void ConsensusEngineBase::updateNodeListInP2P()
{
    dev::h512s nodeList = m_blockChain->sealerList() + m_blockChain->observerList();
    std::pair<GROUP_ID, MODULE_ID> ret = getGroupAndProtocol(m_protocolId);
    m_service->setNodeListByGroupID(ret.first, nodeList);
}

}  // namespace consensus
}  // namespace dev
