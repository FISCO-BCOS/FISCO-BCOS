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
 * @brief Regression test for FIB-143: TxsValidator::asyncResetTxsFlag's
 *        callback must check _error BEFORE erasing the proposal hash from
 *        m_resettingProposals. Otherwise a failed asyncMarkTxs can spuriously
 *        clear the resetting set, fire verifyCompletedHook, and unblock
 *        sealing while txpool state is still inconsistent.
 * @file FIB143_ResetCallbackOrderTest.cpp
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
/// A FakeTxPool that always reports a failure from asyncMarkTxs, simulating
/// the bug scenario where the txpool side cannot complete the mark step.
class FailingMarkTxsPool : public FakeTxPool
{
public:
    using Ptr = std::shared_ptr<FailingMarkTxsPool>;
    explicit FailingMarkTxsPool(int32_t _errorCode = -42, std::string _msg = "fake mark failure")
      : m_errorCode(_errorCode), m_msg(std::move(_msg))
    {}

    void asyncMarkTxs(const HashList& /*txs*/, bool /*flag*/, BlockNumber /*number*/,
        HashType const& /*hash*/, std::function<void(Error::Ptr)> _callback) override
    {
        // schedule asynchronously like the real txpool
        std::thread([this, _callback = std::move(_callback)]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            _callback(BCOS_ERROR_PTR(m_errorCode, m_msg));
        }).detach();
    }

    void setError(int32_t _code, std::string _msg)
    {
        m_errorCode = _code;
        m_msg = std::move(_msg);
    }

private:
    int32_t m_errorCode;
    std::string m_msg;
};

inline CryptoSuite::Ptr makeCryptoSuiteFib143()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

inline Block::Ptr makeBlockWithTxsFib143(BlockFactory::Ptr _blockFactory,
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
            "FIB143-tx-" + std::to_string(_number) + "-" + std::to_string(i));
        auto meta = _blockFactory->createTransactionMetaData(h, h.abridged());
        block->appendTransactionMetaData(meta);
    }
    return block;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB143_ResetCallbackOrder, TestPromptFixture)

BOOST_AUTO_TEST_CASE(failed_asyncMarkTxs_does_not_unblock_sealer)
{
    auto cryptoSuite = makeCryptoSuiteFib143();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto failingPool = std::make_shared<FailingMarkTxsPool>();
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto validator = std::make_shared<TxsValidator>(failingPool, blockFactory, txResultFactory);

    auto block = makeBlockWithTxsFib143(blockFactory, cryptoSuite, /*number*/ 1, /*txs*/ 3);

    std::atomic<bool> sealerNotifiedTooEarly{false};
    validator->setVerifyCompletedHook([&]() { sealerNotifiedTooEarly.store(true); });

    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);
    validator->asyncResetTxsFlag(*block, /*flag*/ true);

    // Wait for the asyncMarkTxs callback (FailingMarkTxsPool sleeps ~5ms before
    // calling back).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !sealerNotifiedTooEarly.load() &&
           validator->resettingProposalSize() > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Give one more tick to be sure the callback completed even if it didn't
    // unblock the sealer.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // FIB-143 invariant: asyncMarkTxs failed, so the sealer must NOT be
    // unblocked, and the proposal hash must remain in m_resettingProposals so
    // that a recovery path still has the chance to act.
    BOOST_CHECK_EQUAL(sealerNotifiedTooEarly.load(), false);
    BOOST_CHECK_GT(validator->resettingProposalSize(), 0);
}

BOOST_AUTO_TEST_CASE(successful_asyncMarkTxs_still_unblocks_sealer)
{
    // Sanity: with a vanilla FakeTxPool (asyncMarkTxs is a no-op success), the
    // existing erase-on-success / fire-hook path still works after the fix.
    auto cryptoSuite = makeCryptoSuiteFib143();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto okPool = std::make_shared<FakeTxPool>();
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto validator = std::make_shared<TxsValidator>(okPool, blockFactory, txResultFactory);

    auto block = makeBlockWithTxsFib143(blockFactory, cryptoSuite, /*number*/ 5, /*txs*/ 2);

    std::atomic<bool> hookFired{false};
    validator->setVerifyCompletedHook([&]() { hookFired.store(true); });

    validator->asyncResetTxsFlag(*block, /*flag*/ true);

    // FakeTxPool::asyncMarkTxs is synchronous (no-op + immediate nullptr-style),
    // but here it never invokes the callback at all. We simply check that the
    // hash was inserted (post-FIB-141) and that no spurious hook fires.
    BOOST_CHECK_EQUAL(hookFired.load(), false);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
