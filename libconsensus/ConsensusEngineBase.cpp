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
            auto sealerList = m_blockChain->sealerList();
            UpgradableGuard l(m_sealerListMutex);
            if (sealerList != m_sealerList)
            {
                UpgradeGuard ul(l);
                m_sealerList = sealerList;
                m_sealerListUpdated = true;
                m_lastSealerListUpdateNumber = m_blockChain->number();
            }
            else if (m_blockChain->number() != m_lastSealerListUpdateNumber)
            {
                m_sealerListUpdated = false;
            }
            /// to make sure the index of all sealers are consistent
            std::sort(m_sealerList.begin(), m_sealerList.end());
            for (dev::h512 node : m_sealerList)
                s2 << node.abridged() << ",";
        }
        s2 << "Observers:";
        dev::h512s observerList = m_blockChain->observerList();
        for (dev::h512 node : observerList)
            s2 << node.abridged() << ",";

        if (m_lastNodeList != s2.str())
        {
            ENGINE_LOG(DEBUG) << LOG_DESC(
                                     "updateConsensusNodeList: nodeList updated, updated nodeList:")
                              << s2.str();

            // get all nodes
            auto sealerList = m_blockChain->sealerList();
            dev::h512s nodeList = sealerList + observerList;
            std::sort(nodeList.begin(), nodeList.end());
            if (m_blockSync->syncTreeRouterEnabled())
            {
                if (m_sealerListUpdated)
                {
                    m_blockSync->updateConsensusNodeInfo(sealerList, nodeList);
                }
                else
                {
                    // update the nodeList
                    m_blockSync->updateNodeListInfo(nodeList);
                }
            }
            m_service->setNodeListByGroupID(m_groupId, nodeList);

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

void ConsensusEngineBase::resetConfig()
{
    updateMaxBlockTransactions();
    auto node_idx = MAXIDX;
    m_accountType = NodeAccountType::ObserverAccount;
    size_t nodeNum = 0;
    updateConsensusNodeList();
    {
        ReadGuard l(m_sealerListMutex);
        for (size_t i = 0; i < m_sealerList.size(); i++)
        {
            if (m_sealerList[i] == m_keyPair.pub())
            {
                m_accountType = NodeAccountType::SealerAccount;
                node_idx = i;
                break;
            }
        }
        nodeNum = m_sealerList.size();
    }
    if (nodeNum < 1)
    {
        ENGINE_LOG(ERROR) << LOG_DESC(
            "Must set at least one pbft sealer, current number of sealers is zero");
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(
            EmptySealers() << errinfo_comment("Must set at least one pbft sealer!"));
    }
    // update m_nodeNum
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    m_f = (m_nodeNum - 1) / 3;
    m_cfgErr = (node_idx == MAXIDX);
    m_idx = node_idx;
}

}  // namespace consensus
}  // namespace dev
