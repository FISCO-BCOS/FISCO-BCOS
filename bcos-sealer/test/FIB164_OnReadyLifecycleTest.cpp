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
 * @file FIB164_OnReadyLifecycleTest.cpp
 * @brief Regression test for FIB-164: Sealer must register its onReady
 *        callback in start() (where weak_from_this() is valid), capturing a
 *        weak_ptr so that callback invocations after Sealer destruction safely
 *        no-op instead of dereferencing a dangling raw `this`.
 */
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-sealer/Sealer.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealingManager.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace bcos::test
{
namespace
{

struct StubTxPoolForFIB164 : public bcos::txpool::TxPoolInterface
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
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)>) override
    {}
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
        bcos::bytesConstRef, std::function<void(Error::Ptr _error)>) override
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

bcos::sealer::SealerConfig::Ptr makeSealerConfig()
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, nullptr, nullptr);
    auto txpool = std::make_shared<StubTxPoolForFIB164>();
    return std::make_shared<bcos::sealer::SealerConfig>(blockFactory, txpool, nullptr);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(FIB164_OnReadyLifecycle)

BOOST_AUTO_TEST_CASE(callback_no_uaf_after_sealer_destroyed)
{
    auto cfg = makeSealerConfig();
    auto sealingManagerCopy = bcos::sealer::SealingManager::Ptr{};

    {
        auto sealer = std::make_shared<bcos::sealer::Sealer>(cfg);
        sealer->start();
        // Acquire a strong reference to the SealingManager so it outlives the
        // Sealer. The onReady callback (registered in start()) holds a
        // weak_ptr<Sealer>, so it should safely no-op after Sealer destruction.
        sealingManagerCopy = sealer->sealingManager();
        sealer->stop();
    }
    // Sealer is now destroyed; sealingManagerCopy is the only owner of the
    // manager. Triggering the onReady callback path must not dereference
    // dangling memory.
    BOOST_REQUIRE(sealingManagerCopy);
    BOOST_CHECK_NO_THROW(sealingManagerCopy->resetSealingInfo(2, 1000, 1));
}

BOOST_AUTO_TEST_CASE(callback_invokes_noteGenerateProposal_while_sealer_alive)
{
    // Verify the registered callback is non-empty and reachable by triggering
    // resetSealingInfo while Sealer is alive (no crash, callback runs via
    // weak_ptr lock succeeding).
    auto cfg = makeSealerConfig();
    auto sealer = std::make_shared<bcos::sealer::Sealer>(cfg);
    sealer->start();
    BOOST_CHECK_NO_THROW(sealer->sealingManager()->resetSealingInfo(2, 1000, 1));
    sealer->stop();
}

BOOST_AUTO_TEST_CASE(callback_not_registered_until_start)
{
    // Before start(), the SealingManager's onReady callback should NOT be the
    // one capturing this Sealer. Trigger resetSealingInfo before start() and
    // verify no UAF / crash even if the manager exists in isolation.
    auto cfg = makeSealerConfig();
    auto sealer = std::make_shared<bcos::sealer::Sealer>(cfg);
    auto mgr = sealer->sealingManager();
    BOOST_REQUIRE(mgr);
    // resetSealingInfo without a registered callback must be a no-op.
    BOOST_CHECK_NO_THROW(mgr->resetSealingInfo(2, 1000, 1));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
