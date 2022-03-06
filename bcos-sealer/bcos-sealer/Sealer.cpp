/*
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
 * @file Sealer.cpp
 * @author: yujiechen
 * @date: 2021-05-14
 */
#include "Sealer.h"
#include "Common.h"
using namespace bcos;
using namespace bcos::sealer;
using namespace bcos::protocol;

void Sealer::start()
{
    if (m_running)
    {
        SEAL_LOG(INFO) << LOG_DESC("the sealer has already been started");
        return;
    }
    SEAL_LOG(INFO) << LOG_DESC("start the sealer");
    startWorking();
    m_running = true;
}

void Sealer::stop()
{
    if (!m_running)
    {
        SEAL_LOG(INFO) << LOG_DESC("the sealer has already been stopped");
        return;
    }
    SEAL_LOG(INFO) << LOG_DESC("stop the sealer");
    m_running = false;
    m_sealingManager->stop();
    finishWorker();
    if (isWorking())
    {
        stopWorking();
        // will not restart worker, so terminate it
        terminate();
    }
}

void Sealer::init(bcos::consensus::ConsensusInterface::Ptr _consensus)
{
    m_sealerConfig->setConsensusInterface(_consensus);
}

void Sealer::asyncNotifySealProposal(size_t _proposalStartIndex, size_t _proposalEndIndex,
    size_t _maxTxsPerBlock, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_sealingManager->resetSealingInfo(_proposalStartIndex, _proposalEndIndex, _maxTxsPerBlock);
    if (_onRecvResponse)
    {
        _onRecvResponse(nullptr);
    }
    SEAL_LOG(INFO) << LOG_DESC("asyncNotifySealProposal")
                   << LOG_KV("startIndex", _proposalStartIndex)
                   << LOG_KV("endIndex", _proposalEndIndex)
                   << LOG_KV("maxTxsPerBlock", _maxTxsPerBlock);
}

void Sealer::asyncNoteLatestBlockNumber(int64_t _blockNumber)
{
    m_sealingManager->resetCurrentNumber(_blockNumber);
    SEAL_LOG(INFO) << LOG_DESC("asyncNoteLatestBlockNumber") << LOG_KV("number", _blockNumber);
}

void Sealer::asyncNoteUnSealedTxsSize(
    size_t _unsealedTxsSize, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_sealingManager->setUnsealedTxsSize(_unsealedTxsSize);
    if (_onRecvResponse)
    {
        _onRecvResponse(nullptr);
    }
}

void Sealer::executeWorker()
{
    if (!m_sealingManager->shouldGenerateProposal() && !m_sealingManager->shouldFetchTransaction())
    {
        ///< 1 milliseconds to next loop
        boost::unique_lock<boost::mutex> l(x_signalled);
        m_signalled.wait_for(l, boost::chrono::milliseconds(1));
    }
    // try to generateProposal
    if (m_sealingManager->shouldGenerateProposal())
    {
        auto ret = m_sealingManager->generateProposal();
        auto proposal = ret.second;
        submitProposal(ret.first, proposal);
    }
    // try to fetch transactions
    if (m_sealingManager->shouldFetchTransaction())
    {
        m_sealingManager->fetchTransactions();
    }
}

void Sealer::submitProposal(bool _containSysTxs, bcos::protocol::Block::Ptr _block)
{
    if (_block->blockHeader()->number() <= m_sealingManager->currentNumber())
    {
        m_sealingManager->notifyResetProposal(_block);
        return;
    }
    auto recordT = utcTime();
    // supplement the header info: set sealerList and weightList
    std::vector<bytes> sealerList;
    std::vector<uint64_t> weightList;
    auto consensusNodeInfo = m_sealerConfig->consensus()->consensusNodeList();
    for (auto const& consensusNode : consensusNodeInfo)
    {
        sealerList.push_back(consensusNode->nodeID()->data());
        weightList.push_back(consensusNode->weight());
    }
    _block->blockHeader()->setSealerList(std::move(sealerList));
    _block->blockHeader()->setConsensusWeights(std::move(weightList));
    _block->blockHeader()->setSealer(m_sealerConfig->consensus()->nodeIndex());
    auto startT = utcTime();
    auto encodedData = std::make_shared<bytes>();
    _block->encode(*encodedData);
    auto encodeT = (utcTime() - startT);
    SEAL_LOG(INFO) << LOG_DESC("++++++++++++++++ Generate proposal")
                   << LOG_KV("index", _block->blockHeader()->number())
                   << LOG_KV("curNum", m_sealingManager->currentNumber())
                   << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                   << LOG_KV("sysTxs", _containSysTxs)
                   << LOG_KV("txsSize", _block->transactionsHashSize())
                   << LOG_KV("encodeT", encodeT) << LOG_KV("cost", (utcTime() - recordT));
    m_sealerConfig->consensus()->asyncSubmitProposal(_containSysTxs, ref(*encodedData),
        _block->blockHeader()->number(), _block->blockHeader()->hash(),
        [_block](Error::Ptr _error) {
            if (_error == nullptr)
            {
                return;
            }
            SEAL_LOG(WARNING) << LOG_DESC("asyncSubmitProposal failed: put back the transactions")
                              << LOG_KV("txsSize", _block->transactionsHashSize());
        });
}

void Sealer::asyncResetSealing(std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_sealingManager->resetSealing();
    if (_onRecvResponse)
    {
        _onRecvResponse(nullptr);
    }
}
