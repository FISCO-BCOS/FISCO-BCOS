/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief queue to store the downloading blocks
 * @file DownloadingQueue.cpp
 * @author: jimmyshi
 * @date 2021-05-24
 */
#include "DownloadingQueue.h"
#include "bcos-sync/utilities/Common.h"
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <future>

using namespace std;
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::sync;
using namespace bcos::ledger;

void DownloadingQueue::push(BlocksMsgInterface::Ptr _blocksData)
{
    // push to the blockBuffer firstly
    UpgradableGuard lock(x_blockBuffer);
    if (m_blockBuffer->size() >= m_config->maxDownloadingBlockQueueSize())
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                             << LOG_DESC("DownloadingBlockQueueBuffer is full")
                             << LOG_KV("queueSize", m_blockBuffer->size());
        return;
    }
    UpgradeGuard ulock(lock);
    m_blockBuffer->emplace_back(_blocksData);
}

bool DownloadingQueue::empty()
{
    ReadGuard lock1(x_blockBuffer);
    ReadGuard lock2(x_blocks);
    return (m_blocks.empty() && (!m_blockBuffer || m_blockBuffer->empty()));
}

size_t DownloadingQueue::size()
{
    ReadGuard lock1(x_blockBuffer);
    ReadGuard lock2(x_blocks);
    size_t size = (!m_blockBuffer ? 0 : m_blockBuffer->size()) + m_blocks.size();
    return size;
}

void DownloadingQueue::pop()
{
    WriteGuard lock(x_blocks);
    if (!m_blocks.empty())
    {
        m_blocks.pop();
    }
}

Block::Ptr DownloadingQueue::top(bool isFlushBuffer)
{
    if (isFlushBuffer)
    {
        flushBufferToQueue();
    }
    ReadGuard lock(x_blocks);
    if (!m_blocks.empty())
    {
        return m_blocks.top();
    }
    return nullptr;
}

void DownloadingQueue::clear()
{
    {
        WriteGuard lock(x_blockBuffer);
        m_blockBuffer->clear();
    }
    clearQueue();
}

void DownloadingQueue::clearQueue()
{
    WriteGuard lock(x_blocks);
    BlockQueue emptyQueue;
    swap(m_blocks, emptyQueue);  // Does memory leak here ?
}

void DownloadingQueue::flushBufferToQueue()
{
    WriteGuard lock(x_blockBuffer);
    bool ret = true;
    while (!m_blockBuffer->empty() && ret)
    {
        auto blocksShard = m_blockBuffer->front();
        m_blockBuffer->pop_front();
        ret = flushOneShard(blocksShard);
    }
}

bool DownloadingQueue::flushOneShard(BlocksMsgInterface::Ptr _blocksData)
{
    // pop buffer into queue
    if (m_blocks.size() >= m_config->maxDownloadingBlockQueueSize())
    {
        BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                           << LOG_DESC("DownloadingBlockQueueBuffer is full")
                           << LOG_KV("queueSize", m_blocks.size());

        return false;
    }
    BLKSYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                       << LOG_DESC("Decoding block buffer")
                       << LOG_KV("blocksShardSize", _blocksData->blocksSize());
    size_t blocksSize = _blocksData->blocksSize();
    std::vector<protocol::Block::Ptr> blocks;
    blocks.reserve(blocksSize);
    // prepare block
    for (size_t i = 0; i < blocksSize; i++)
    {
        try
        {
            auto block =
                m_config->blockFactory()->createBlock(_blocksData->blockData(i), true, true);
            blocks.push_back(std::move(block));
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                 << LOG_DESC("Invalid block data")
                                 << LOG_KV("reason", boost::diagnostic_information(e))
                                 << LOG_KV("blockDataSize", _blocksData->blockData(i).size());
            continue;
        }
    }
    WriteGuard lock(x_blocks);
    for (const auto& block : blocks)
    {
        auto blockHeader = block->blockHeader();
        // is NewerBlock
        if (blockHeader->number() > m_config->blockNumber())
        {
            m_blocks.push(block);
            BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                               << LOG_DESC("Flush block to the queue")
                               << LOG_KV("number", blockHeader->number())
                               << LOG_KV("configNum", m_config->blockNumber())
                               << LOG_KV("nodeId", m_config->nodeID()->shortHex());
        }
    }
    if (m_blocks.empty())
    {
        return true;
    }
    BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                       << LOG_DESC("Flush buffer to block queue") << LOG_KV("rcv", blocksSize)
                       << LOG_KV("top", m_blocks.top()->blockHeader()->number())
                       << LOG_KV("downloadBlockQueue", m_blocks.size())
                       << LOG_KV("nodeId", m_config->nodeID()->shortHex());
    return true;
}

void DownloadingQueue::clearFullQueueIfNotHas(BlockNumber _blockNumber)
{
    bool needClear = false;
    {
        ReadGuard lock(x_blocks);
        // Note: size maybe greater than max when many blocks execute failed
        if (m_blocks.size() >= m_config->maxDownloadingBlockQueueSize() &&
            m_blocks.top()->blockHeader()->number() > _blockNumber)
        {
            needClear = true;
        }
    }
    if (needClear)
    {
        clearQueue();
    }
}

bool DownloadingQueue::verifyExecutedBlock(bcos::protocol::Block::Ptr const& _block,
    bcos::protocol::BlockHeader::Ptr const& _blockHeader) const noexcept
{
    // check blockHash(Note: since the ledger check the parentHash before commit, here no need to
    // check the parentHash)
    auto orgBlockHeader = _block->blockHeader();
    if (orgBlockHeader->hash() != _blockHeader->hash())
    {
        BLKSYNC_LOG(ERROR) << LOG_DESC("verifyExecutedBlock failed for inconsistent hash")
                           << LOG_KV("orgHeader", printBlockHeader(orgBlockHeader)) << "\n"
                           << LOG_KV("executedHeader", printBlockHeader(_blockHeader)) << "\n"
                           << printBlockHeaderDiff(orgBlockHeader, _blockHeader);

        return false;
    }
    return true;
}

std::string DownloadingQueue::printBlockHeaderDiff(
    BlockHeader::Ptr const& orgHeader, BlockHeader::Ptr const& execHeader) const noexcept
{
    std::stringstream oss;
    oss << "BlockHeader diff: \n";
    oss << "-orgHash:       " << orgHeader->hash() << "\n"
        << "+exeHash:       " << execHeader->hash() << "\n";
    if (orgHeader->version() != execHeader->version()) [[unlikely]]
    {
        oss << "-orgVersion:    " << orgHeader->version() << "\n"
            << "+exeVersion:    " << execHeader->version() << "\n";
    }
    if (orgHeader->txsRoot() != execHeader->txsRoot()) [[unlikely]]
    {
        oss << "-orgTxsRoot:    " << orgHeader->txsRoot() << "\n"
            << "+exeTxsRoot:    " << execHeader->txsRoot() << "\n";
    }
    if (orgHeader->receiptsRoot() != execHeader->receiptsRoot()) [[likely]]
    {
        oss << "-orgRcptsRoot:  " << orgHeader->receiptsRoot() << "\n"
            << "+exeRcptsRoot:  " << execHeader->receiptsRoot() << "\n";
    }
    if (orgHeader->stateRoot() != execHeader->stateRoot()) [[likely]]
    {
        oss << "-orgStateRoot:  " << orgHeader->stateRoot() << "\n"
            << "+exeStateRoot:  " << execHeader->stateRoot() << "\n";
    }
    if (orgHeader->gasUsed() != execHeader->gasUsed()) [[likely]]
    {
        oss << "-orgGasUsed:    " << orgHeader->gasUsed() << "\n"
            << "+exeGasUsed:    " << execHeader->gasUsed() << "\n";
    }
    return oss.str();
}


std::string DownloadingQueue::printBlockHeader(BlockHeader::Ptr const& _header) const noexcept
{
    std::stringstream oss;
    std::stringstream sealerListStr;
    std::stringstream signatureListStr;
    std::stringstream weightsStr;

    sealerListStr << "size: " << _header->sealerList().size();
    signatureListStr << "size: " << _header->signatureList().size();
    if (c_fileLogLevel == TRACE)
    {
        auto sealerList = _header->sealerList();
        sealerListStr << ", sealer list: ";
        for (auto const& sealer : sealerList)
        {
            sealerListStr << *toHexString(sealer) << ", ";
        }
        auto signatureList = _header->signatureList();
        signatureListStr << ", sign list: ";
        for (auto const& signatureData : signatureList)
        {
            signatureListStr << (*toHexString(signatureData.signature)) << ":"
                             << signatureData.index << ", ";
        }
    }
    auto weightList = _header->consensusWeights();
    for (auto const& weight : weightList)
    {
        weightsStr << weight << ", ";
    }

    auto parentInfo = _header->parentInfo();
    std::stringstream parentInfoStr;
    for (auto const& parent : parentInfo)
    {
        parentInfoStr << parent.blockNumber << ":" << parent.blockHash << ", ";
    }
    oss << LOG_KV("hash", _header->hash()) << LOG_KV("version", _header->version())
        << LOG_KV("txsRoot", _header->txsRoot()) << LOG_KV("receiptsRoot", _header->receiptsRoot())
        << LOG_KV("dbHash", _header->stateRoot()) << LOG_KV("number", _header->number())
        << LOG_KV("gasUsed", _header->gasUsed()) << LOG_KV("timestamp", _header->timestamp())
        << LOG_KV("sealer", _header->sealer()) << LOG_KV("sealerList", sealerListStr.str())
        << LOG_KV("signatureList", signatureListStr.str())
        << LOG_KV("consensusWeights", weightsStr.str()) << LOG_KV("parents", parentInfoStr.str())
        << LOG_KV("extraData", *toHexString(_header->extraData()));
    return oss.str();
}


void DownloadingQueue::applyBlock(Block::Ptr _block)
{
    auto blockHeader = _block->blockHeader();
    // check the block number
    if (blockHeader->number() <= m_config->blockNumber()) [[unlikely]]
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("Download")
                             << LOG_BADGE("BlockSync: checkBlock before apply")
                             << LOG_DESC("Ignore illegal block")
                             << LOG_KV("reason", "number illegal")
                             << LOG_KV("thisNumber", blockHeader->number())
                             << LOG_KV("currentNumber", m_config->blockNumber());
        m_config->setExecutedBlock(m_config->blockNumber());
        return;
    }
    if (blockHeader->number() <= m_config->executedBlock())
    {
        return;
    }
    m_config->setApplyingBlock(blockHeader->number());
    auto startT = utcTime();
    auto self = weak_from_this();
    m_config->scheduler()->executeBlock(_block, true,
        [self, startT, _block](
            Error::Ptr&& _error, protocol::BlockHeader::Ptr&& _blockHeader, bool _sysBlock) {
            auto orgBlockHeader = _block->blockHeader();
            try
            {
                auto downloadQueue = self.lock();
                if (!downloadQueue)
                {
                    return;
                }
                auto config = downloadQueue->m_config;
                // execute/verify exception
                if (_error != nullptr)
                {
                    config->setExecutedBlock(config->blockNumber());
                    // reset the executed number
                    BLKSYNC_LOG(WARNING)
                        << LOG_DESC("applyBlock: executing the downloaded block failed")
                        << LOG_KV("number", orgBlockHeader->number())
                        << LOG_KV("hash", orgBlockHeader->hash().abridged())
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("message", _error->errorMessage());
                    if (_error->errorCode() == bcos::scheduler::SchedulerError::InvalidBlocks)
                    {
                        BLKSYNC_LOG(INFO)
                            << LOG_DESC("fetchAndUpdateLedgerConfig for InvalidBlocks");
                        downloadQueue->fetchAndUpdateLedgerConfig();
                        return;
                    }
                    if (!config->masterNode())
                    {
                        BLKSYNC_LOG(INFO) << LOG_DESC(
                            "applyBlock failed: but do nothing for the node is not the master "
                            "node");
                        return;
                    }
                    {
                        // re-push the block into blockQueue to retry later
                        if (_block->blockHeader()->number() > config->blockNumber())
                        {
                            BLKSYNC_LOG(INFO)
                                << LOG_DESC(
                                       "applyBlock: executing the downloaded block failed, re-push "
                                       "the block into executing queue")
                                << LOG_KV("number", orgBlockHeader->number())
                                << LOG_KV("hash", orgBlockHeader->hash().abridged());
                            WriteGuard l(downloadQueue->x_blocks);
                            downloadQueue->m_blocks.push(_block);
                        }
                    }
                    return;
                }
                if (!downloadQueue->verifyExecutedBlock(_block, _blockHeader))
                {
                    config->setExecutedBlock(config->blockNumber());
                    return;
                }
                auto executedBlock = config->executedBlock();
                if (orgBlockHeader->number() > executedBlock + 1)
                {
                    {
                        WriteGuard lock(downloadQueue->x_blocks);
                        downloadQueue->m_blocks.push(_block);
                    }
                    BLKSYNC_LOG(WARNING)
                        << LOG_BADGE("Download")
                        << LOG_DESC("BlockSync: re-push the appliedBlock for discontinuous")
                        << LOG_KV("executedBlock", executedBlock)
                        << LOG_KV("nextBlock", downloadQueue->m_config->nextBlock())
                        << LOG_KV("number", orgBlockHeader->number())
                        << LOG_KV("hash", orgBlockHeader->hash().abridged());
                    return;
                }
                // Note: continue to execute the next block only after sysBlock is submitted
                if (!_sysBlock)
                {
                    config->setExecutedBlock(orgBlockHeader->number());
                    if (downloadQueue->m_applyFinishedHandler)
                    {
                        // if still have block not execute
                        downloadQueue->m_applyFinishedHandler(!downloadQueue->empty());
                    }
                }
                auto signature = orgBlockHeader->signatureList();
                BLKSYNC_LOG(INFO) << METRIC << LOG_BADGE("Download")
                                  << LOG_DESC("BlockSync: applyBlock success")
                                  << LOG_KV("number", orgBlockHeader->number())
                                  << LOG_KV("hash", orgBlockHeader->hash().abridged())
                                  << LOG_KV("signatureSize", signature.size())
                                  << LOG_KV("txsSize", _block->transactionsSize())
                                  << LOG_KV("nextBlock", downloadQueue->m_config->nextBlock())
                                  << LOG_KV(
                                         "executedBlock", downloadQueue->m_config->executedBlock())
                                  << LOG_KV("timeCost", (utcTime() - startT))
                                  << LOG_KV("node", downloadQueue->m_config->nodeID()->shortHex())
                                  << LOG_KV("sysBlock", _sysBlock);
                // verify and commit the block
                downloadQueue->updateCommitQueue(_block);
            }
            catch (std::exception const& e)
            {
                BLKSYNC_LOG(WARNING) << LOG_DESC("applyBlock exception")
                                     << LOG_KV("number", orgBlockHeader->number())
                                     << LOG_KV("hash", orgBlockHeader->hash().abridged())
                                     << LOG_KV("message", boost::diagnostic_information(e));
            }
        });
}

bool DownloadingQueue::checkAndCommitBlock(bcos::protocol::Block::Ptr _block)
{
    auto blockHeader = _block->blockHeader();
    // check the block number
    if (blockHeader->number() != m_config->nextBlock())
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync: checkBlock")
                             << LOG_DESC("Ignore illegal block")
                             << LOG_KV("reason", "number illegal")
                             << LOG_KV("thisNumber", blockHeader->number())
                             << LOG_KV("currentNumber", m_config->blockNumber());
        m_config->setExecutedBlock(m_config->blockNumber());
        return false;
    }
    auto signature = blockHeader->signatureList();
    BLKSYNC_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("checkAndCommitBlock")
                      << LOG_KV("number", blockHeader->number())
                      << LOG_KV("signatureSize", signature.size())
                      << LOG_KV("currentNumber", m_config->blockNumber())
                      << LOG_KV("hash", blockHeader->hash().abridged());

    auto self = weak_from_this();
    m_config->consensus()->asyncCheckBlock(_block, [self, _block, blockHeader](
                                                       Error::Ptr _error, bool _ret) {
        try
        {
            auto downloadQueue = self.lock();
            if (!downloadQueue)
            {
                return;
            }
            if (_error)
            {
                BLKSYNC_LOG(WARNING)
                    << LOG_DESC("asyncCheckBlock error")
                    << LOG_KV("blockNumber", blockHeader->number())
                    << LOG_KV("hash", blockHeader->hash().abridged())
                    << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
                downloadQueue->m_config->setExecutedBlock(blockHeader->number() - 1);
                return;
            }
            if (_ret)
            {
                BLKSYNC_LOG(INFO) << BLOCK_NUMBER(blockHeader->number())
                                  << LOG_DESC("asyncCheckBlock success, try to commit the block")
                                  << LOG_KV("hash", blockHeader->hash().abridged());
                downloadQueue->commitBlock(_block);
                return;
            }
            downloadQueue->m_config->setExecutedBlock(blockHeader->number() - 1);
            BLKSYNC_LOG(WARNING) << LOG_DESC("asyncCheckBlock failed")
                                 << LOG_KV("blockNumber", blockHeader->number())
                                 << LOG_KV("hash", blockHeader->hash().abridged());
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(WARNING) << LOG_DESC("asyncCheckBlock exception")
                                 << LOG_KV("blockNumber", blockHeader->number())
                                 << LOG_KV("hash", blockHeader->hash().abridged())
                                 << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
    return true;
}

void DownloadingQueue::updateCommitQueue(Block::Ptr _block)
{
    {
        WriteGuard lock(x_commitQueue);
        m_commitQueue.push(_block);
    }
    tryToCommitBlockToLedger();
}

void DownloadingQueue::tryToCommitBlockToLedger()
{
    WriteGuard lock(x_commitQueue);
    if (m_commitQueue.empty())
    {
        return;
    }
    // remove expired block
    while (!m_commitQueue.empty() &&
           m_commitQueue.top()->blockHeader()->number() <= m_config->blockNumber())
    {
        m_commitQueue.pop();
    }
    // try to commit the block
    if (!m_commitQueue.empty() &&
        m_commitQueue.top()->blockHeader()->number() == m_config->nextBlock())
    {
        auto block = m_commitQueue.top();
        m_commitQueue.pop();
        checkAndCommitBlock(block);
    }
}


void DownloadingQueue::commitBlock(bcos::protocol::Block::Ptr _block)
{
    auto blockHeader = _block->blockHeader();
    BLKSYNC_LOG(INFO) << LOG_DESC("commitBlock") << LOG_KV("number", blockHeader->number())
                      << LOG_KV("txsNum", _block->transactionsSize())
                      << LOG_KV("hash", blockHeader->hash().abridged());
    // empty block
    if (_block->transactionsSize() == 0)
    {
        BLKSYNC_LOG(INFO) << LOG_DESC("commitBlock: receive empty block, commitBlockState directly")
                          << LOG_KV("number", blockHeader->number())
                          << LOG_KV("hash", blockHeader->hash().abridged());
        commitBlockState(_block);
        return;
    }
    // commit transaction firstly
    auto self = weak_from_this();
    try
    {
        auto downloadingQueue = self.lock();
        if (!downloadingQueue)
        {
            return;
        }
        downloadingQueue->commitBlockState(_block);
    }
    catch (std::exception const& e)
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC("commitBlock exception")
                             << LOG_KV("message", boost::diagnostic_information(e));
    }
}

void DownloadingQueue::commitBlockState(bcos::protocol::Block::Ptr _block)
{
    auto blockHeader = _block->blockHeader();
    BLKSYNC_LOG(INFO) << LOG_DESC("commitBlockState") << LOG_KV("number", blockHeader->number())
                      << LOG_KV("hash", blockHeader->hash().abridged());
    auto startT = utcTime();
    auto self = weak_from_this();
    m_config->scheduler()->commitBlock(blockHeader, [self, startT, _block, blockHeader](
                                                        Error::Ptr&& _error,
                                                        LedgerConfig::Ptr&& _ledgerConfig) {
        try
        {
            auto downloadingQueue = self.lock();
            if (!downloadingQueue)
            {
                return;
            }
            if (_error != nullptr)
            {
                BLKSYNC_LOG(WARNING)
                    << LOG_DESC("commitBlockState failed")
                    << LOG_KV("executedBlock", downloadingQueue->m_config->executedBlock())
                    << LOG_KV("number", blockHeader->number())
                    << LOG_KV("hash", blockHeader->hash().abridged())
                    << LOG_KV("code", _error->errorCode())
                    << LOG_KV("message", _error->errorMessage());
                downloadingQueue->onCommitFailed(_error, _block);
                return;
            }
            _ledgerConfig->setTxsSize(_block->transactionsSize());
            _ledgerConfig->setSealerId(blockHeader->sealer());
            // reset the blockNumber
            _ledgerConfig->setBlockNumber(blockHeader->number());
            _ledgerConfig->setHash(blockHeader->hash());
            // notify the txpool the transaction result
            // reset the config for the consensus and the blockSync module
            // broadcast the status to all the peers
            // clear the expired cache
            downloadingQueue->finalizeBlock(_block, _ledgerConfig);
            auto executedBlock = downloadingQueue->m_config->executedBlock();
            if (executedBlock < blockHeader->number())
            {
                downloadingQueue->m_config->setExecutedBlock(blockHeader->number());
            }
            BLKSYNC_LOG(INFO) << BLOCK_NUMBER(blockHeader->number()) << METRIC
                              << LOG_DESC("commitBlockState success")
                              << LOG_KV("number", blockHeader->number())
                              << LOG_KV("hash", blockHeader->hash().abridged())
                              << LOG_KV(
                                     "executedBlock", downloadingQueue->m_config->executedBlock())
                              << LOG_KV("commitBlockTimeCost", (utcTime() - startT))
                              << LOG_KV("node", downloadingQueue->m_config->nodeID()->shortHex())
                              << LOG_KV("txsSize", _block->transactionsSize())
                              << LOG_KV("sealer", blockHeader->sealer());
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(WARNING) << LOG_DESC("commitBlock exception")
                                 << LOG_KV("number", blockHeader->number())
                                 << LOG_KV("hash", blockHeader->hash().abridged())
                                 << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}


void DownloadingQueue::finalizeBlock(bcos::protocol::Block::Ptr, LedgerConfig::Ptr _ledgerConfig)
{
    if (m_newBlockHandler)
    {
        m_newBlockHandler(std::move(_ledgerConfig));
    }
    // try to commit the next block
    tryToCommitBlockToLedger();
}

void DownloadingQueue::clearExpiredQueueCache()
{
    clearExpiredCache(m_blocks, x_blocks);
    clearExpiredCache(m_commitQueue, x_commitQueue);
}

void DownloadingQueue::clearExpiredCache(BlockQueue& _queue, SharedMutex& _lock)
{
    WriteGuard lock(_lock);
    while (!_queue.empty() && _queue.top()->blockHeader()->number() <= m_config->blockNumber())
    {
        _queue.pop();
    }
}

void DownloadingQueue::onCommitFailed(
    bcos::Error::Ptr _error, bcos::protocol::Block::Ptr _failedBlock)
{
    auto blockHeader = _failedBlock->blockHeader();
    // case invalidBlocks
    if (_error->errorCode() == bcos::scheduler::SchedulerError::InvalidBlocks)
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC(
                                    "onCommitFailed: the block has already been committed, return "
                                    "directly")
                             << LOG_KV("executedBlock", m_config->executedBlock())
                             << LOG_KV("number", blockHeader->number())
                             << LOG_KV("hash", blockHeader->hash().abridged())
                             << LOG_KV("code", _error->errorCode())
                             << LOG_KV("message", _error->errorMessage());
        fetchAndUpdateLedgerConfig();
        // Note: When an InvalidBlocks error occurs, there may be uncommitted blocks in the
        // commitQueue, so need to call tryToCommitBlockToLedger and then commit the block
        tryToCommitBlockToLedger();
        return;
    }
    if (blockHeader->number() <= m_config->blockNumber())
    {
        BLKSYNC_LOG(INFO) << LOG_DESC("onCommitFailed: drop the expired block")
                          << LOG_KV("number", blockHeader->number())
                          << LOG_KV("hash", blockHeader->hash().abridged())
                          << LOG_KV("executedBlock", m_config->executedBlock());
        return;
    }
    BLKSYNC_LOG(INFO) << LOG_DESC("onCommitFailed") << LOG_KV("number", blockHeader->number())
                      << LOG_KV("hash", blockHeader->hash().abridged())
                      << LOG_KV("executedBlock", m_config->executedBlock());

    // re-push failedBlock to commitQueue
    {
        WriteGuard lock(x_commitQueue);
        m_commitQueue.push(_failedBlock);
    }
    if (_error->errorCode() == bcos::scheduler::SchedulerError::BlockIsCommitting)
    {
        BLKSYNC_LOG(INFO) << LOG_DESC(
                                 "onCommitFailed for BlockIsCommitting: re-push failed "
                                 "block to commitQueue")
                          << LOG_KV("hash", blockHeader->hash().abridged())
                          << LOG_KV("executedBlock", m_config->executedBlock());
        // retry after 20ms
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        tryToCommitBlockToLedger();
        return;
    }
    // fetchAndUpdateLedgerConfig in case of the blocks commit success while get-system-config
    // failed
    fetchAndUpdateLedgerConfig();
    m_config->setExecutedBlock(blockHeader->number() - 1);
    auto topBlock = top();
    bcos::protocol::BlockNumber topNumber = std::numeric_limits<bcos::protocol::BlockNumber>::max();
    if (topBlock)
    {
        topNumber = topBlock->blockHeader()->number();
    }
    size_t rePushedBlockCount = 0;
    {
        // re-push un-committed block into m_blocks
        // Note: this operation is low performance and low frequency
        WriteGuard lock1(x_commitQueue);
        WriteGuard lock2(x_blocks);
        if (m_commitQueue.empty())
        {
            return;
        }
        // Note: since the applied block will be re-pushed into m_commitQueue again, no-need to
        // write-back the poped block into commitQueue here
        while (!m_commitQueue.empty())
        {
            auto topCommitBlock = m_commitQueue.top();
            if (topCommitBlock->blockHeader()->number() >= topNumber)
            {
                break;
            }
            rePushedBlockCount++;
            m_blocks.push(topCommitBlock);
            m_commitQueue.pop();
        }
    }
    auto blocksTop = m_blocks.top();
    BLKSYNC_LOG(INFO) << LOG_DESC("onCommitFailed: update commitQueue and executingQueue")
                      << LOG_KV("commitQueueSize", m_commitQueue.size())
                      << LOG_KV("blocksQueueSize", m_blocks.size())
                      << LOG_KV("topNumber", topNumber)
                      << LOG_KV("topBlock", blocksTop ? (blocksTop->blockHeader()->number()) : -1)
                      << LOG_KV("rePushedBlockCount", rePushedBlockCount)
                      << LOG_KV("executedBlock", m_config->executedBlock());
}

void DownloadingQueue::fetchAndUpdateLedgerConfig()
{
    try
    {
        BLKSYNC_LOG(INFO) << LOG_DESC("fetchAndUpdateLedgerConfig");
        m_ledgerFetcher->fetchBlockNumberAndHash();
        m_ledgerFetcher->fetchCompatibilityVersion();
        m_ledgerFetcher->fetchFeatures();
        m_ledgerFetcher->fetchConsensusNodeList();
        // Note: must fetchObserverNode here to notify the latest sealerList and observerList to
        // txpool
        m_ledgerFetcher->fetchObserverNodeList();
        m_ledgerFetcher->fetchCandidateSealerList();
        m_ledgerFetcher->fetchBlockTxCountLimit();
        m_ledgerFetcher->fetchConsensusLeaderPeriod();
        auto ledgerConfig = m_ledgerFetcher->ledgerConfig();
        BLKSYNC_LOG(INFO) << LOG_DESC("fetchAndUpdateLedgerConfig success")
                          << LOG_KV("blockNumber", ledgerConfig->blockNumber())
                          << LOG_KV("hash", ledgerConfig->hash().abridged())
                          << LOG_KV("maxTxsPerBlock", ledgerConfig->blockTxCountLimit())
                          << LOG_KV("consensusNodeList", ledgerConfig->consensusNodeList().size());
        m_config->resetConfig(ledgerConfig);
    }
    catch (std::exception const& e)
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC("fetchAndUpdateLedgerConfig exception")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}