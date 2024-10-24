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
#include "bcos-framework/txpool/TxPoolInterface.h"
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
#include <bcos-utilities/ThreadPool.h>
#include <utility>

namespace bcos::consensus
{
class ValidatorInterface
{
public:
    using Ptr = std::shared_ptr<ValidatorInterface>;
    ValidatorInterface() = default;
    virtual ~ValidatorInterface() = default;
    virtual void verifyProposal(bcos::crypto::PublicPtr _fromNode,
        PBFTProposalInterface::Ptr _proposal,
        std::function<void(Error::Ptr, bool)> _verifyFinishedHandler) = 0;

    virtual void asyncResetTxsFlag(
        bytesConstRef _data, bool _flag, bool _emptyTxBatchHash = false) = 0;
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
      : m_txPool(std::move(_txPool)),
        m_blockFactory(std::move(_blockFactory)),
        m_txResultFactory(std::move(_txResultFactory)),
        m_worker(std::make_shared<ThreadPool>("validator", 2))
    {}

    ~TxsValidator() override = default;

    void stop() override;
    void init() override;

    void asyncResetTxPool() override;
    void verifyProposal(bcos::crypto::PublicPtr _fromNode, PBFTProposalInterface::Ptr _proposal,
        std::function<void(Error::Ptr, bool)> _verifyFinishedHandler) override;

    void asyncResetTxsFlag(
        bytesConstRef _data, bool _flag, bool _emptyTxBatchHash = false) override;
    ssize_t resettingProposalSize() const override;

    PBFTProposalInterface::Ptr generateEmptyProposal(uint32_t _proposalVersion,
        PBFTMessageFactory::Ptr _factory, int64_t _index, int64_t _sealerId) override;

    void notifyTransactionsResult(
        bcos::protocol::Block::Ptr _block, bcos::protocol::BlockHeader::Ptr _header) override;

    void updateValidatorConfig(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        bcos::consensus::ConsensusNodeList const& _observerNodeList) override;

    void setVerifyCompletedHook(std::function<void()> _hook) override;

protected:
    virtual void eraseResettingProposal(bcos::crypto::HashType const& _hash);

    void triggerVerifyCompletedHook();
    virtual bool insertResettingProposal(bcos::crypto::HashType const& _hash);

    virtual void asyncResetTxsFlag(bcos::protocol::Block::Ptr _block,
        bcos::crypto::HashListPtr _txsHashList, bool _flag, bool _emptyTxBatchHash);

    bcos::txpool::TxPoolInterface::Ptr m_txPool;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    ThreadPool::Ptr m_worker;
    std::set<bcos::crypto::HashType> m_resettingProposals;
    mutable SharedMutex x_resettingProposals;

    std::function<void()> m_verifyCompletedHook = nullptr;
    mutable RecursiveMutex x_verifyCompletedHook;
};
}  // namespace bcos::consensus