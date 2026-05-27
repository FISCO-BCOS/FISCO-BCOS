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
 * @brief Regression test for FIB-112: Missing _onExecuteFinished callback in
 *        block-number-mismatch error branch of StateMachine::apply()
 * @file FIB112_OnExecuteFinishedRestoreTest.cpp
 * @author: claude
 * @date 2026-05-07
 */
#include "bcos-pbft/core/Proposal.h"
#include "bcos-pbft/core/StateMachine.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{

// Scheduler that returns a block header with a number different from the requested block.
// This triggers the mismatch branch in StateMachine::apply().
class MismatchedNumberScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    explicit MismatchedNumberScheduler(bcos::protocol::BlockFactory::Ptr _blockFactory)
      : m_blockFactory(_blockFactory)
    {}

    void executeBlock(bcos::protocol::Block::Ptr _block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> _callback)
        override
    {
        // Build a header whose number is far off from the requested block's number
        auto originalNumber = _block->blockHeader()->number();
        auto mismatchedHeader =
            m_blockFactory->blockHeaderFactory()->createBlockHeader(originalNumber + 9000);
        mismatchedHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
        _callback(nullptr, std::move(mismatchedHeader), false);
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)>) override
    {}
    void status(std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)>) override {}
    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) override
    {}
    void reset(std::function<void(Error::Ptr)>) override {}
    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)>) override {}
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)> _callback) override
    {
        _callback(nullptr);
    }

private:
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};

BOOST_FIXTURE_TEST_SUITE(FIB112Test, TestPromptFixture)

// Before FIB-112 fix: when the scheduler returns a block header with a different block
// number than expected, StateMachine::apply() logged a warning and returned WITHOUT
// calling _onExecuteFinished. This caused the consensus engine to hang indefinitely.
//
// After fix: _onExecuteFinished is called with a non-zero error code in all exit paths.
BOOST_AUTO_TEST_CASE(callback_called_on_block_number_mismatch)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    auto headerFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, headerFactory, txFactory, receiptFactory);

    auto scheduler = std::make_shared<MismatchedNumberScheduler>(blockFactory);
    auto stateMachine = std::make_shared<StateMachine>(scheduler, blockFactory);

    // lastApplied at index 1
    auto lastApplied = std::make_shared<Proposal>();
    lastApplied->setIndex(1);
    lastApplied->setHash(HashType{});

    // proposal at index 2 (= lastApplied+1)
    auto proposalBlock = blockFactory->createBlock();
    auto proposalHeader = headerFactory->createBlockHeader(2);
    proposalHeader->setNumber(2);
    proposalHeader->calculateHash(*hashImpl);
    proposalBlock->setBlockHeader(proposalHeader);
    bytes blockData;
    proposalBlock->encode(blockData);

    auto proposal = std::make_shared<Proposal>();
    proposal->setIndex(2);
    proposal->setHash(proposalHeader->hash());
    proposal->setData(blockData);

    auto executedProposal = std::make_shared<Proposal>();

    std::atomic<bool> callbackCalled{false};
    std::atomic<int64_t> callbackCode{0};

    stateMachine->asyncApply(3000, lastApplied, proposal, executedProposal, [&](int64_t errorCode) {
        callbackCalled.store(true);
        callbackCode.store(errorCode);
    });

    // Wait up to 5 seconds for callback
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!callbackCalled.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // FIB-112: callback MUST be called even when block numbers don't match
    BOOST_CHECK_MESSAGE(callbackCalled.load(),
        "FIB-112: _onExecuteFinished must be called on block number mismatch");
    // Error code must be non-zero (failure indication)
    BOOST_CHECK_NE(callbackCode.load(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
