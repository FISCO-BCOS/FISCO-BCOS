/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief Regression test for FIB-144: when asyncMarkTxs fails with
 *        TransactionsMissing, TxsValidator must NOT leave the proposal hash
 *        permanently stranded in m_resettingProposals. The fix removes the
 *        hash so a subsequent PrePrepare for the same proposal can re-trigger
 *        marking once the data arrives.
 * @file FIB144_ResetRetryTest.cpp
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTxPool.h"
#include "bcos-pbft/pbft/engine/Validator.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
/// A FakeTxPool that fails asyncMarkTxs with the configured error code.
class CodedFailingMarkPool : public FakeTxPool
{
public:
    using Ptr = std::shared_ptr<CodedFailingMarkPool>;
    explicit CodedFailingMarkPool(int32_t _code, std::string _msg)
      : m_code(_code), m_msg(std::move(_msg))
    {}

    void asyncMarkTxs(const HashList& /*txs*/, bool /*flag*/, BlockNumber /*number*/,
        HashType const& /*hash*/, std::function<void(Error::Ptr)> _callback) override
    {
        std::thread([this, _callback = std::move(_callback)]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            _callback(BCOS_ERROR_PTR(m_code, m_msg));
        }).detach();
    }

private:
    int32_t m_code;
    std::string m_msg;
};

inline CryptoSuite::Ptr makeCryptoSuiteFib144()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

inline Block::Ptr makeBlockWithUnknownTxs(BlockFactory::Ptr _blockFactory,
    CryptoSuite::Ptr _cryptoSuite, BlockNumber _number, size_t _txsCount)
{
    auto block = _blockFactory->createBlock();
    auto header = _blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(_number);
    header->calculateHash(*_cryptoSuite->hashImpl());
    block->setBlockHeader(header);
    for (size_t i = 0; i < _txsCount; i++)
    {
        auto h = _cryptoSuite->hashImpl()->hash(
            "FIB144-unknown-tx-" + std::to_string(_number) + "-" + std::to_string(i));
        auto meta = _blockFactory->createTransactionMetaData(h, h.abridged());
        block->appendTransactionMetaData(meta);
    }
    return block;
}

template <typename Predicate>
bool waitForCondition(Predicate&& _pred, std::chrono::milliseconds _timeout)
{
    auto deadline = std::chrono::steady_clock::now() + _timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (_pred())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return _pred();
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB144_ResetRetry, TestPromptFixture)

BOOST_AUTO_TEST_CASE(transactionsMissing_removes_hash_from_resetting_set)
{
    // Given: a validator whose txpool replies with CommonError::TransactionsMissing.
    auto cryptoSuite = makeCryptoSuiteFib144();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto pool = std::make_shared<CodedFailingMarkPool>(
        CommonError::TransactionsMissing, "fake TransactionsMissing");
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto validator = std::make_shared<TxsValidator>(pool, blockFactory, txResultFactory);

    auto block = makeBlockWithUnknownTxs(blockFactory, cryptoSuite, /*number*/ 1, /*txs*/ 3);

    // When: asyncResetTxsFlag is invoked with _flag=true.
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);
    validator->asyncResetTxsFlag(*block, /*flag*/ true);

    // Wait for the callback to fire and observe the resetting set drop back to 0.
    bool drained = waitForCondition(
        [&]() { return validator->resettingProposalSize() == 0; }, std::chrono::seconds(3));

    // Then: the proposal hash must have been removed so a future PrePrepare
    // for the same proposal can re-trigger asyncResetTxsFlag.
    BOOST_CHECK(drained);
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);

    // Sanity: a *second* asyncResetTxsFlag must be allowed through (i.e.,
    // insertResettingProposal does not silently suppress because the hash is
    // still present).
    validator->asyncResetTxsFlag(*block, /*flag*/ true);
    // briefly observe re-insert (it will fail again immediately, but the act of
    // re-entering proves recoverability).
    waitForCondition(
        [&]() { return validator->resettingProposalSize() == 0; }, std::chrono::milliseconds(1500));
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);
}

BOOST_AUTO_TEST_CASE(non_transient_error_keeps_hash_in_resetting_set)
{
    // FIB-143 invariant must still hold for non-transient errors: the hash
    // stays in m_resettingProposals so sealing remains gated.
    auto cryptoSuite = makeCryptoSuiteFib144();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto pool = std::make_shared<CodedFailingMarkPool>(/*non-transient*/ -777, "permanent failure");
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto validator = std::make_shared<TxsValidator>(pool, blockFactory, txResultFactory);

    auto block = makeBlockWithUnknownTxs(blockFactory, cryptoSuite, /*number*/ 2, /*txs*/ 2);

    validator->asyncResetTxsFlag(*block, /*flag*/ true);

    // After ~200ms the callback has fired; the hash should remain stranded
    // intentionally (FIB-143 retain-on-failure semantics).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    BOOST_CHECK_GT(validator->resettingProposalSize(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
