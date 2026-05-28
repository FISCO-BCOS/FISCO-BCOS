/*
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
 * @file FIB163_TimestampSaturateTest.cpp
 * @brief CertiK FIB-163 regression: saturate the m_latestTimestamp + 1
 *        derivation in SealingManager::generateProposal() at INT64_MAX. The
 *        bug was unchecked signed addition that wraps to INT64_MIN at the
 *        boundary; the fix saturates instead.
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tool/NodeTimeMaintenance.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <limits>

using namespace bcos;
using namespace bcos::sealer;

namespace bcos::test
{
namespace
{
struct FIB163MockTxPool : public bcos::txpool::TxPoolInterface
{
    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t /*_txsLimit*/) override
    {
        return {};
    }
    void tryToSyncTxsFromPeers() override {}
    void start() override {}
    void stop() override {}
    void asyncMarkTxs(const bcos::crypto::HashList&, bool, bcos::protocol::BlockNumber,
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)> _onRecvResponse) override
    {
        if (_onRecvResponse)
        {
            _onRecvResponse(nullptr);
        }
    }
    void asyncVerifyBlock(bcos::crypto::PublicPtr, bcos::protocol::Block::ConstPtr,
        std::function<void(Error::Ptr, bool)>) override
    {}
    void asyncFillBlock(bcos::crypto::HashListPtr,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)>) override
    {}
    void asyncNotifyBlockResult(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>) override
    {}
    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr, std::string const&, bcos::crypto::NodeIDPtr,
        bytesConstRef, std::function<void(Error::Ptr)>) override
    {}
    void notifyConsensusNodeList(
        bcos::consensus::ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    void notifyObserverNodeList(
        bcos::consensus::ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    void asyncGetPendingTransactionSize(std::function<void(Error::Ptr, uint64_t)>) override {}
    void asyncResetTxPool(std::function<void(Error::Ptr)>) override {}
    void notifyConnectedNodes(
        bcos::crypto::NodeIDSet const&, std::function<void(Error::Ptr)>) override
    {}
};

struct FIB163Fixture
{
    FIB163Fixture()
    {
        boost::log::core::get()->set_logging_enabled(false);
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        auto transactionFactory =
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
        auto receiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
        txpool = std::make_shared<FIB163MockTxPool>();
        nodeTimeMaintenance = std::make_shared<bcos::tool::NodeTimeMaintenance>();
        sealerConfig =
            std::make_shared<sealer::SealerConfig>(blockFactory, txpool, nodeTimeMaintenance);
    }
    ~FIB163Fixture() { boost::log::core::get()->set_logging_enabled(true); }

    crypto::CryptoSuite::Ptr cryptoSuite;
    protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<FIB163MockTxPool> txpool;
    bcos::tool::NodeTimeMaintenance::Ptr nodeTimeMaintenance;
    sealer::SealerConfig::Ptr sealerConfig;
};

inline std::vector<bcos::protocol::TransactionMetaData::Ptr> makeMetaDataBatch(size_t count)
{
    std::vector<bcos::protocol::TransactionMetaData::Ptr> batch;
    batch.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        bcos::crypto::HashType h;
        h[0] = static_cast<bcos::byte>((i + 1) & 0xff);
        auto md =
            std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(h, std::string{"to"});
        batch.emplace_back(std::move(md));
    }
    return batch;
}

inline std::shared_ptr<sealer::SealingManager> makeSealingManagerWithSeed(
    sealer::SealerConfig::Ptr config)
{
    auto mgr = std::make_shared<sealer::SealingManager>(std::move(config));
    mgr->resetSealingInfo(/*start*/ 2, /*end*/ 1'000'000, /*maxTxsPerBlock*/ 1);
    auto seed = makeMetaDataBatch(4);
    mgr->testOnlySeedPendingTxs(seed);
    return mgr;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB163_TimestampSaturate, FIB163Fixture)

BOOST_AUTO_TEST_CASE(latestTimestamp_at_int64_max_does_not_overflow)
{
    auto mgr = makeSealingManagerWithSeed(sealerConfig);
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
    mgr->resetLatestTimestamp(kMax);

    auto [containsSysTxs, block] = mgr->generateProposal({});
    BOOST_REQUIRE(block);
    // Pre-fix: timestamp wraps to INT64_MIN (or any negative / nonsense
    // value). Post-fix: saturates at INT64_MAX.
    BOOST_CHECK_EQUAL(block->blockHeader()->timestamp(), kMax);
}

BOOST_AUTO_TEST_CASE(latestTimestamp_one_below_int64_max_increments_normally)
{
    auto mgr = makeSealingManagerWithSeed(sealerConfig);
    constexpr int64_t kMaxMinus1 = std::numeric_limits<int64_t>::max() - 1;
    mgr->resetLatestTimestamp(kMaxMinus1);

    auto [containsSysTxs, block] = mgr->generateProposal({});
    BOOST_REQUIRE(block);
    // Just below the boundary: addition is safe, normal increment applies.
    BOOST_CHECK_EQUAL(block->blockHeader()->timestamp(), std::numeric_limits<int64_t>::max());
}

BOOST_AUTO_TEST_CASE(latestTimestamp_far_below_uses_aligned_time)
{
    auto mgr = makeSealingManagerWithSeed(sealerConfig);
    // m_latestTimestamp = 0 (default). Aligned time will be the wall clock
    // (large positive). Generated timestamp must therefore be the aligned
    // time, not m_latestTimestamp + 1.
    auto [containsSysTxs, block] = mgr->generateProposal({});
    BOOST_REQUIRE(block);
    BOOST_CHECK(block->blockHeader()->timestamp() > 1'000'000'000LL);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
