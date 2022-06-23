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
 * @file Validator.h
 * @author: yujiechen
 * @date 2021-04-21
 */
#pragma once
#include "../interfaces/PBFTMessageFactory.h"
#include "../interfaces/PBFTProposalInterface.h"
#include "bcos-framework//txpool/TxPoolInterface.h"
#include <bcos-framework//protocol/BlockFactory.h>
#include <bcos-framework//protocol/TransactionSubmitResultFactory.h>
#include <bcos-utilities/ThreadPool.h>

namespace bcos
{
namespace consensus
{
class ValidatorInterface
{
public:
    using Ptr = std::shared_ptr<ValidatorInterface>;
    ValidatorInterface() = default;
    virtual ~ValidatorInterface() {}
    virtual void verifyProposal(bcos::crypto::PublicPtr _fromNode,
        PBFTProposalInterface::Ptr _proposal,
        std::function<void(Error::Ptr, bool)> _verifyFinishedHandler) = 0;

    virtual void asyncResetTxsFlag(bytesConstRef _data, bool _flag) = 0;
    virtual PBFTProposalInterface::Ptr generateEmptyProposal(uint32_t _proposalVersion,
        PBFTMessageFactory::Ptr _factory, int64_t _index, int64_t _sealerId) = 0;

    virtual void notifyTransactionsResult(
        bcos::protocol::Block::Ptr _block, bcos::protocol::BlockHeader::Ptr _header) = 0;
    virtual void updateValidatorConfig(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        bcos::consensus::ConsensusNodeList const& _observerNodeList) = 0;

    virtual void stop() = 0;
    virtual void init() = 0;
    virtual void asyncResetTxPool() = 0;
    virtual ssize_t resettingProposalSize() const = 0;
    virtual void setVerifyCompletedHook(std::function<void()>) = 0;
};

class TxsValidator : public ValidatorInterface, public std::enable_shared_from_this<TxsValidator>
{
public:
    using Ptr = std::shared_ptr<TxsValidator>;
    explicit TxsValidator(bcos::txpool::TxPoolInterface::Ptr _txPool,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory)
      : m_txPool(_txPool),
        m_blockFactory(_blockFactory),
        m_txResultFactory(_txResultFactory),
        m_worker(std::make_shared<ThreadPool>("validator", 2))
    {}

    ~TxsValidator() override {}

    void stop() override { m_worker->stop(); }

    void init() override
    {
        PBFT_LOG(INFO) << LOG_DESC("asyncResetTxPool when startup");
        asyncResetTxPool();
    }

    void asyncResetTxPool() override
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
    void verifyProposal(bcos::crypto::PublicPtr _fromNode, PBFTProposalInterface::Ptr _proposal,
        std::function<void(Error::Ptr, bool)> _verifyFinishedHandler) override;

    void asyncResetTxsFlag(bytesConstRef _data, bool _flag) override;
    ssize_t resettingProposalSize() const override
    {
        ReadGuard l(x_resettingProposals);
        return m_resettingProposals.size();
    }

    PBFTProposalInterface::Ptr generateEmptyProposal(uint32_t _proposalVersion,
        PBFTMessageFactory::Ptr _factory, int64_t _index, int64_t _sealerId) override
    {
        auto proposal = _factory->createPBFTProposal();
        proposal->setIndex(_index);
        auto block = m_blockFactory->createBlock();
        auto blockHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        blockHeader->populateEmptyBlock(_index, _sealerId);
        blockHeader->setVersion(_proposalVersion);
        block->setBlockHeader(blockHeader);
        auto encodedData = std::make_shared<bytes>();
        block->encode(*encodedData);
        proposal->setHash(blockHeader->hash());
        proposal->setData(std::move(*encodedData));
        return proposal;
    }

    void notifyTransactionsResult(
        bcos::protocol::Block::Ptr _block, bcos::protocol::BlockHeader::Ptr _header) override
    {
        auto results = std::make_shared<bcos::protocol::TransactionSubmitResults>();
        for (size_t i = 0; i < _block->transactionsHashSize(); i++)
        {
            auto txHash = _block->transactionHash(i);
            auto txResult = m_txResultFactory->createTxSubmitResult();
            txResult->setBlockHash(_header->hash());
            txResult->setTxHash(txHash);
            results->emplace_back(std::move(txResult));
        }
        m_txPool->asyncNotifyBlockResult(
            _header->number(), results, [_block, _header](Error::Ptr _error) {
                if (_error == nullptr)
                {
                    PBFT_LOG(INFO) << LOG_DESC("notify block result success")
                                   << LOG_KV("number", _header->number())
                                   << LOG_KV("hash", _header->hash().abridged())
                                   << LOG_KV("txsSize", _block->transactionsHashSize());
                    return;
                }
                PBFT_LOG(INFO) << LOG_DESC("notify block result failed")
                               << LOG_KV("code", _error->errorCode())
                               << LOG_KV("msg", _error->errorMessage());
            });
    }

    void updateValidatorConfig(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        bcos::consensus::ConsensusNodeList const& _observerNodeList) override
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

    void setVerifyCompletedHook(std::function<void()> _hook) override
    {
        RecursiveGuard l(x_verifyCompletedHook);
        m_verifyCompletedHook = _hook;
    }

protected:
    virtual void eraseResettingProposal(bcos::crypto::HashType const& _hash)
    {
        {
            WriteGuard l(x_resettingProposals);
            m_resettingProposals.erase(_hash);
            if (m_resettingProposals.size() > 0)
            {
                return;
            }
        }
        // When all consensusing proposals are notified, call verifyCompletedHook
        triggerVerifyCompletedHook();
    }

    void triggerVerifyCompletedHook()
    {
        RecursiveGuard l(x_verifyCompletedHook);
        if (!m_verifyCompletedHook)
        {
            return;
        }
        auto callback = m_verifyCompletedHook;
        m_verifyCompletedHook = nullptr;
        auto self = std::weak_ptr<TxsValidator>(shared_from_this());
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
    virtual bool insertResettingProposal(bcos::crypto::HashType const& _hash)
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

    virtual void asyncResetTxsFlag(bcos::protocol::Block::Ptr _block,
        bcos::crypto::HashListPtr _txsHashList, bool _flag, size_t _retryTime = 0);

    bcos::txpool::TxPoolInterface::Ptr m_txPool;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    ThreadPool::Ptr m_worker;
    std::set<bcos::crypto::HashType> m_resettingProposals;
    mutable SharedMutex x_resettingProposals;

    std::function<void()> m_verifyCompletedHook = nullptr;
    mutable RecursiveMutex x_verifyCompletedHook;
};
}  // namespace consensus
}  // namespace bcos