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
 * @brief Validator for the consensus module
 * @file Validator.cpp
 * @author: yujiechen
 * @date 2021-04-21
 */
#include "Validator.h"
using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

void bcos::consensus::TxsValidator::stop()
{
    m_worker->stop();
}

void bcos::consensus::TxsValidator::init()
{
    PBFT_LOG(INFO) << LOG_DESC("asyncResetTxPool when startup");
    asyncResetTxPool();
}

void bcos::consensus::TxsValidator::asyncResetTxPool()
{
    m_txPool->asyncResetTxPool([](Error::Ptr _error) {
        if (_error)
        {
            PBFT_LOG(WARNING) << LOG_DESC("asyncResetTxPool failed")
                              << LOG_KV("code", _error->errorCode())
                              << LOG_KV("msg", _error->errorMessage());
        }
    });
}

ssize_t bcos::consensus::TxsValidator::resettingProposalSize() const
{
    ReadGuard l(x_resettingProposals);
    return m_resettingProposals.size();
}

void TxsValidator::verifyProposal(bcos::crypto::PublicPtr _fromNode,
    PBFTProposalInterface::Ptr _proposal,
    std::function<void(Error::Ptr, bool)> _verifyFinishedHandler)
{
    auto block = m_blockFactory->createBlock(_proposal->data());
    auto blockHeader = block->blockHeader();
    if (blockHeader->number() != _proposal->index())
    {
        if (_verifyFinishedHandler)
        {
            auto error = BCOS_ERROR_PTR(-1, "Invalid proposal");
            _verifyFinishedHandler(error, false);
        }
        return;
    }
    // TODO: passing block directly, no need to createBlock twice
    m_txPool->asyncVerifyBlock(_fromNode, _proposal->data(), _verifyFinishedHandler);
}

void TxsValidator::asyncResetTxsFlag(bytesConstRef _data, bool _flag, bool _emptyTxBatchHash)
{
    auto block = m_blockFactory->createBlock(_data);
    auto blockHeader = block->blockHeader();
    if (_flag)
    {
        // already has the reset request
        if (!insertResettingProposal(blockHeader->hash()))
        {
            return;
        }
    }
    auto self = std::weak_ptr<TxsValidator>(shared_from_this());
    m_worker->enqueue([self, blockHeader, block, _flag, _emptyTxBatchHash]() {
        try
        {
            auto validator = self.lock();
            if (!validator)
            {
                return;
            }

            auto txsHash = std::make_shared<HashList>();
            for (size_t i = 0; i < block->transactionsHashSize(); i++)
            {
                txsHash->emplace_back(block->transactionHash(i));
            }
            if (txsHash->empty())
            {
                return;
            }
            PBFT_LOG(INFO) << LOG_DESC("asyncResetTxsFlag")
                           << LOG_KV("index", blockHeader->number())
                           << LOG_KV("hash", blockHeader->hash().abridged())
                           << LOG_KV("flag", _flag);
            validator->asyncResetTxsFlag(block, txsHash, _flag, _emptyTxBatchHash);
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("asyncResetTxsFlag exception")
                              << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

void TxsValidator::asyncResetTxsFlag(
    bcos::protocol::Block::Ptr _block, HashListPtr _txsHashList, bool _flag, bool _emptyTxBatchHash)
{
    auto blockHeader = _block->blockHeader();
    auto proposalNumber = blockHeader->number();
    auto proposalHash = blockHeader->hash();
    if (_emptyTxBatchHash)
    {
        proposalNumber = -1;
        proposalHash = bcos::crypto::HashType();
    }
    auto self = std::weak_ptr<TxsValidator>(shared_from_this());
    auto startT = utcSteadyTime();
    m_txPool->asyncMarkTxs(_txsHashList, _flag, proposalNumber, proposalHash,
        [self, _block, blockHeader, _txsHashList, _flag, _emptyTxBatchHash, startT](
            Error::Ptr _error) {
            auto validator = self.lock();
            if (!validator)
            {
                return;
            }
            // must ensure asyncResetTxsFlag success before seal new next blocks
            if (_flag)
            {
                validator->eraseResettingProposal(blockHeader->hash());
            }
            if (_error == nullptr)
            {
                PBFT_LOG(INFO) << LOG_DESC("asyncMarkTxs success")
                               << LOG_KV("index", blockHeader->number())
                               << LOG_KV("hash", blockHeader->hash().abridged())
                               << LOG_KV("flag", _flag) << LOG_KV("markT", utcSteadyTime() - startT)
                               << LOG_KV("emptyTxBatchHash", _emptyTxBatchHash);
                return;
            }
            PBFT_LOG(WARNING) << LOG_DESC("asyncMarkTxs failed")
                              << LOG_KV("code", _error->errorCode())
                              << LOG_KV("msg", _error->errorMessage());
            if (_flag)
            {
                validator->insertResettingProposal(blockHeader->hash());
            }
        });
}

// void bcos::consensus::TxsValidator::notifyTransactionsResult(
//     bcos::protocol::Block::Ptr _block, bcos::protocol::BlockHeader::Ptr _header)
// {
//     auto results = std::make_shared<bcos::protocol::TransactionSubmitResults>();
//     results->reserve(_block->transactionsHashSize());
//     for (size_t i = 0; i < _block->transactionsHashSize(); i++)
//     {
//         auto txHash = _block->transactionHash(i);
//         auto txResult = m_txResultFactory->createTxSubmitResult();
//         txResult->setBlockHash(_header->hash());
//         txResult->setTxHash(txHash);
//         results->emplace_back(std::move(txResult));
//     }
//     m_txPool->asyncNotifyBlockResult(
//         _header->number(), results, [_block, _header](Error::Ptr _error) {
//             if (_error == nullptr)
//             {
//                 PBFT_LOG(INFO) << LOG_DESC("notify block result success")
//                                << LOG_KV("number", _header->number())
//                                << LOG_KV("hash", _header->hash().abridged())
//                                << LOG_KV("txsSize", _block->transactionsHashSize());
//                 return;
//             }
//             PBFT_LOG(INFO) << LOG_DESC("notify block result failed")
//                            << LOG_KV("code", _error->errorCode())
//                            << LOG_KV("msg", _error->errorMessage());
//         });
// }

bcos::consensus::PBFTProposalInterface::Ptr bcos::consensus::TxsValidator::generateEmptyProposal(
    uint32_t _proposalVersion, PBFTMessageFactory::Ptr _factory, int64_t _index, int64_t _sealerId)
{
    auto proposal = _factory->createPBFTProposal();
    proposal->setIndex(_index);
    auto block = m_blockFactory->createBlock();
    auto blockHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->populateEmptyBlock(_index, _sealerId);
    blockHeader->setVersion(_proposalVersion);
    blockHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
    block->setBlockHeader(blockHeader);
    auto encodedData = std::make_shared<bytes>();
    block->encode(*encodedData);
    proposal->setHash(blockHeader->hash());
    proposal->setData(std::move(*encodedData));
    return proposal;
}

void bcos::consensus::TxsValidator::updateValidatorConfig(
    bcos::consensus::ConsensusNodeList const& _consensusNodeList,
    bcos::consensus::ConsensusNodeList const& _observerNodeList)
{
    m_txPool->notifyConsensusNodeList(_consensusNodeList, [](Error::Ptr _error) {
        if (_error == nullptr)
        {
            PBFT_LOG(DEBUG) << LOG_DESC("notify to update consensusNodeList config success");
            return;
        }
        PBFT_LOG(WARNING) << LOG_DESC("notify to update consensusNodeList config failed")
                          << LOG_KV("code", _error->errorCode())
                          << LOG_KV("msg", _error->errorMessage());
    });

    m_txPool->notifyObserverNodeList(_observerNodeList, [](Error::Ptr _error) {
        if (_error == nullptr)
        {
            PBFT_LOG(DEBUG) << LOG_DESC("notify to update observerNodeList config success");
            return;
        }
        PBFT_LOG(WARNING) << LOG_DESC("notify to update observerNodeList config failed")
                          << LOG_KV("code", _error->errorCode())
                          << LOG_KV("msg", _error->errorMessage());
    });
}

void bcos::consensus::TxsValidator::setVerifyCompletedHook(std::function<void()> _hook)
{
    RecursiveGuard l(x_verifyCompletedHook);
    m_verifyCompletedHook = _hook;
}

void bcos::consensus::TxsValidator::eraseResettingProposal(bcos::crypto::HashType const& _hash)
{
    {
        WriteGuard l(x_resettingProposals);
        m_resettingProposals.erase(_hash);
        if (!m_resettingProposals.empty())
        {
            return;
        }
    }
    // When all consensusing proposals are notified, call verifyCompletedHook
    triggerVerifyCompletedHook();
}

void bcos::consensus::TxsValidator::triggerVerifyCompletedHook()
{
    RecursiveGuard l(x_verifyCompletedHook);
    if (!m_verifyCompletedHook)
    {
        return;
    }
    auto callback = m_verifyCompletedHook;
    m_verifyCompletedHook = nullptr;
    auto self = weak_from_this();
    m_worker->enqueue([self, callback]() {
        auto validator = self.lock();
        if (!validator)
        {
            return;
        }
        if (!callback)
        {
            return;
        }
        callback();
    });
}

bool bcos::consensus::TxsValidator::insertResettingProposal(bcos::crypto::HashType const& _hash)
{
    UpgradableGuard l(x_resettingProposals);
    if (m_resettingProposals.count(_hash))
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_resettingProposals.insert(_hash);
    return true;
}
