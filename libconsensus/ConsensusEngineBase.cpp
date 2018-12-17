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
        ENGINE_LOG(WARNING) << "[#ConsensusEngineBase has already been started]";
        return;
    }
    ENGINE_LOG(INFO) << "[#Start ConsensusEngineBase]";
    /// start  a thread to execute doWork()&&workLoop()
    startWorking();
    m_startConsensusEngine = true;
}

void ConsensusEngineBase::stop()
{
    if (m_startConsensusEngine == false)
    {
        ENGINE_LOG(WARNING) << " [#ConsensusEngineBase has already been stopped]";
        return;
    }
    ENGINE_LOG(INFO) << "[#Stop ConsensusEngineBase]";
    m_startConsensusEngine = false;
    doneWorking();
    if (isWorking())
        stopWorking();
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
    /// check the timestamp
    if (block.blockHeader().timestamp() > utcTime() && !m_allowFutureBlocks)
    {
        ENGINE_LOG(DEBUG) << "[#checkBlockValid] Future timestamp: [timestamp/utcTime/hash]:  "
                          << block.blockHeader().timestamp() << "/" << utcTime() << "/"
                          << block_hash << std::endl;
        BOOST_THROW_EXCEPTION(DisabledFutureTime() << errinfo_comment("Future time Disabled"));
    }
    /// check the block number
    if (block.blockHeader().number() <= m_blockChain->number())
    {
        ENGINE_LOG(DEBUG) << "[#checkBlockValid] Old height: [blockNumber/number/hash]:  "
                          << block.blockHeader().number() << "/" << m_blockChain->number() << "/"
                          << block_hash << std::endl;
        BOOST_THROW_EXCEPTION(InvalidBlockHeight() << errinfo_comment("Invalid block height"));
    }
    /// check existence of this block (Must non-exist)
    if (blockExists(block_hash))
    {
        ENGINE_LOG(DEBUG) << "[#checkBlockValid] Block already exist: [hash]:  " << block_hash
                          << std::endl;
        BOOST_THROW_EXCEPTION(ExistedBlock() << errinfo_comment("Block Already Existed, drop now"));
    }
    /// check the existence of the parent block (Must exist)
    if (!blockExists(block.blockHeader().parentHash()))
    {
        ENGINE_LOG(DEBUG) << "[#checkBlockValid] Parent doesn't exist: [hash]:  " << block_hash
                          << std::endl;
        BOOST_THROW_EXCEPTION(ParentNoneExist() << errinfo_comment("Parent Block Doesn't Exist"));
    }
    if (block.blockHeader().number() > 1)
    {
        if (m_blockChain->numberHash(block.blockHeader().number() - 1) !=
            block.blockHeader().parentHash())
        {
            ENGINE_LOG(DEBUG) << "[#checkBlockValid] Invalid block for unconsistent parentHash: "
                                 "[block.parentHash/parentHash]:  "
                              << toHex(block.blockHeader().parentHash()) << "/"
                              << toHex(m_blockChain->numberHash(block.blockHeader().number() - 1))
                              << std::endl;
        }
    }
}

void ConsensusEngineBase::updateNodeListInP2P()
{
    dev::h512s nodeList = m_blockChain->minerList() + m_blockChain->observerList();
    std::pair<GROUP_ID, MODULE_ID> ret = getGroupAndProtocol(m_protocolId);
    m_service->setNodeListByGroupID(ret.first, nodeList);
}

}  // namespace consensus
}  // namespace dev
