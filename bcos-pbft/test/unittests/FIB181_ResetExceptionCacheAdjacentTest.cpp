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
 * @file FIB181_ResetExceptionCacheAdjacentTest.cpp
 * @brief FIB-181: PBFTCache::resetExceptionCache() must process every stale
 *        exception pre-prepare entry in a single pass. Pre-fix, erasing a stale
 *        entry returned an iterator to the next entry but the loop's
 *        unconditional post-branch ++ then skipped that next entry. With two
 *        adjacent stale entries the second one missed its
 *        asyncResetTxsFlag(*block, false) call. See audit/findings/FIB-181.md.
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeFrontService.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTxPool.h"
#include "bcos-pbft/pbft/cache/PBFTCache.h"
#include "bcos-pbft/pbft/engine/Validator.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessageFactoryImpl.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::front;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
/// Validator that only counts asyncResetTxsFlag(Block, bool, bool) calls. It does
/// NOT call the base implementation, so the test does not depend on txpool state.
class CountingValidator : public TxsValidator
{
public:
    using TxsValidator::TxsValidator;
    void asyncResetTxsFlag(const protocol::Block& /*proposal*/, bool /*flag*/,
        bool /*emptyTxBatchHash*/ = false) override
    {
        ++m_count;
    }
    std::atomic<int> m_count{0};
};

inline CryptoSuite::Ptr makeCryptoSuiteFib181()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

/// Build an exception pre-prepare message carrying a distinct 32-byte hash and a
/// consensusProposal whose data() is a real encoded empty block, so the
/// resetExceptionCache else-branch's createBlock(data, false, false) parses.
inline PBFTMessageInterface::Ptr makeExceptionPrePrepare(
    CryptoSuite::Ptr const& _cryptoSuite, BlockFactory::Ptr const& _blockFactory, uint8_t _seed)
{
    auto block = _blockFactory->createBlock();
    bytes encoded;
    block->encode(encoded);

    auto proposal = std::make_shared<PBFTProposal>();
    proposal->setIndex(10);
    proposal->setHash(_cryptoSuite->hashImpl()->hash(std::to_string(_seed)));
    proposal->setData(encoded);

    auto msg = std::make_shared<PBFTMessage>();
    msg->setIndex(10);
    msg->setHash(_cryptoSuite->hashImpl()->hash("exception-" + std::to_string(_seed)));
    msg->setConsensusProposal(proposal);
    return msg;
}

/// Minimal PBFTConfig: only blockFactory() and validator() are exercised by
/// resetExceptionCache(). stateMachine/storage/codec are null and guarded by
/// PBFTConfig::stop().
class MinimalPBFTConfig : public PBFTConfig
{
public:
    MinimalPBFTConfig(CryptoSuite::Ptr _cryptoSuite, KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<ValidatorInterface> _validator,
        std::shared_ptr<FrontServiceInterface> _frontService, BlockFactory::Ptr _blockFactory)
      : PBFTConfig(m_ioService, std::move(_cryptoSuite), std::move(_keyPair),
            std::make_shared<PBFTMessageFactoryImpl>(), nullptr, std::move(_validator),
            std::move(_frontService), nullptr, nullptr, std::move(_blockFactory))
    {}

private:
    boost::asio::io_context m_ioService;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB181_ResetExceptionCacheAdjacent, TestPromptFixture)

BOOST_AUTO_TEST_CASE(adjacent_stale_entries_both_reset)
{
    auto cryptoSuite = makeCryptoSuiteFib181();
    // generateKeyPair() returns a unique_ptr; bind to the shared KeyPairInterface::Ptr
    // expected by PBFTConfig (the converting ctor moves the unique_ptr in).
    KeyPairInterface::Ptr keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto txPool = std::make_shared<FakeTxPool>();
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto validator = std::make_shared<CountingValidator>(txPool, blockFactory, txResultFactory);
    auto frontService = std::make_shared<FakeFrontService>(keyPair->publicKey());

    auto config = std::make_shared<MinimalPBFTConfig>(
        cryptoSuite, keyPair, validator, frontService, blockFactory);

    auto cache = std::make_shared<PBFTCache>(config, /*index*/ 10);

    // Two adjacent stale exception pre-prepares with DISTINCT hashes. With
    // m_prePrepare and m_precommit both null, neither matches a valid/precommit
    // hash, so both fall into the else-branch that erases and calls
    // asyncResetTxsFlag(*block, false).
    cache->addExceptionPrePrepareCache(makeExceptionPrePrepare(cryptoSuite, blockFactory, 1));
    cache->addExceptionPrePrepareCache(makeExceptionPrePrepare(cryptoSuite, blockFactory, 2));

    cache->resetExceptionCache(/*curView*/ 1);

    // Post-fix: both stale entries are processed in the same pass.
    // Pre-fix the count was 1 (the second adjacent entry was skipped).
    BOOST_CHECK_EQUAL(validator->m_count.load(), 2);

    config->stop();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
