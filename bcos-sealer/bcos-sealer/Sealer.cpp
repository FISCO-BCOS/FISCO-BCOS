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
#include "VRFBasedSealer.h"
#include "bcos-framework/ledger/Features.h"
#include <bcos-framework/protocol/GlobalConfig.h>
#include <range/v3/view/transform.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::sealer;
using namespace bcos::protocol;
namespace bcos::sealer
{
class VRFBasedSealer;
}

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
    m_sealerConfig->setConsensusInterface(std::move(_consensus));
}

void Sealer::asyncNotifySealProposal(uint64_t _proposalStartIndex, uint64_t _proposalEndIndex,
    uint64_t _maxTxsPerBlock, std::function<void(Error::Ptr)> _onRecvResponse)
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
    m_sealingManager->resetLatestNumber(_blockNumber);
    SEAL_LOG(INFO) << LOG_DESC("asyncNoteLatestBlockNumber") << LOG_KV("number", _blockNumber);
}

void Sealer::asyncNoteLatestBlockHash(crypto::HashType _hash)
{
    SEAL_LOG(INFO) << LOG_DESC("asyncNoteLatestBlockHash") << LOG_KV("_hash", _hash.abridged());
    m_sealingManager->resetLatestHash(_hash);
}

void Sealer::executeWorker()
{
    // try to fetch transactions
    m_sealingManager->fetchTransactions();

    // try to generateProposal
    if (m_sealingManager->shouldGenerateProposal())
    {
        auto ret = m_sealingManager->generateProposal(
            [this](bcos::protocol::Block::Ptr _block) -> uint16_t {
                return hookWhenSealBlock(std::move(_block));
            });
        submitProposal(ret.first, ret.second);

        // TODO: pullTxsTimer's work
    }
    else
    {
        boost::unique_lock<boost::mutex> lock(x_signalled);
        m_signalled.wait_for(lock, boost::chrono::milliseconds(1));
    }
}

void Sealer::submitProposal(bool _containSysTxs, bcos::protocol::Block::Ptr _block)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().SEALER, ittapi::ITT_DOMAINS::instance().SUBMIT_PROPOSAL);
    // Note: the block maybe empty
    if (!_block)
    {
        return;
    }
    if (_block->blockHeader()->number() <= m_sealingManager->latestNumber())
    {
        SEAL_LOG(INFO) << LOG_DESC("submitProposal return for the block has already been committed")
                       << LOG_KV("proposalIndex", _block->blockHeader()->number())
                       << LOG_KV("currentNumber", m_sealingManager->latestNumber());
        m_sealingManager->notifyResetProposal(*_block);
        return;
    }
    // supplement the header info: set sealerList and weightList
    auto consensusNodeInfo = m_sealerConfig->consensus()->consensusNodeList();
    _block->blockHeader()->setSealerList(::ranges::views::transform(consensusNodeInfo,
                                             [](auto const& node) { return node.nodeID->data(); }) |
                                         ::ranges::to<std::vector>());
    _block->blockHeader()->setConsensusWeights(
        ::ranges::views::transform(
            consensusNodeInfo, [](auto const& node) { return node.voteWeight; }) |
        ::ranges::to<std::vector>());
    _block->blockHeader()->setSealer(m_sealerConfig->consensus()->nodeIndex());
    // set the version
    auto version = std::min(m_sealerConfig->consensus()->compatibilityVersion(),
        (uint32_t)g_BCOSConfig.maxSupportedVersion());
    _block->blockHeader()->setVersion(version);
    _block->blockHeader()->calculateHash(*m_hashImpl);

    SEAL_LOG(INFO) << LOG_DESC("++++++++++++++++ Generate proposal")
                   << LOG_KV("index", _block->blockHeader()->number())
                   << LOG_KV("curNum", m_sealingManager->latestNumber())
                   << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                   << LOG_KV("sysTxs", _containSysTxs)
                   << LOG_KV("txsSize", _block->transactionsHashSize())
                   << LOG_KV("version", version);
    m_sealerConfig->consensus()->asyncSubmitProposal(_containSysTxs, *_block,
        _block->blockHeader()->number(), _block->blockHeader()->hash(), [_block](auto&& _error) {
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

uint16_t Sealer::hookWhenSealBlock(bcos::protocol::Block::Ptr _block)
{
    if (!m_sealerConfig->consensus()->shouldRotateSealers(
            _block == nullptr ? -1 : _block->blockHeader()->number()))
    {
        return SealBlockResult::SUCCESS;
    }
    return VRFBasedSealer::generateTransactionForRotating(_block, m_sealerConfig, m_sealingManager,
        m_hashImpl,
        m_sealerConfig->consensus()->consensusConfig()->features().get(
            ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input));
}
