/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief the two txpool event edges that do not go through a successful admission: an insert()
 *        that only lends its callback to the already-pooled copy is not an admission, and a
 *        removal that brings a full pool under its limit flips it back (TxPoolRecovered)
 */
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-utilities/BlockStat.h"
#include "bcos-utilities/IOServicePool.h"
#include <bcos-framework/ledger/LedgerConfigState.h>
#include <bcos-framework/testutils/LogCapture.h>
#include <bcos-tx-validator/TxPoolNonceChecker.h>
#include <bcos-tx-validator/TxValidator.h>
#include <bcos-tx-validator/Web3NonceChecker.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
class ProbedMemoryStorage : public bcos::txpool::MemoryStorage
{
public:
    using MemoryStorage::MemoryStorage;
    using MemoryStorage::removeInvalidTxs;
    bool poolFull() const { return m_poolFull.load(); }
};
}  // namespace

struct TxPoolAdmissionEventsFixture
{
    TxPoolAdmissionEventsFixture()
      : web3NonceChecker{std::make_shared<bcos::txvalidator::Web3NonceChecker>(nullptr)},
        txValidator{std::make_shared<bcos::txvalidator::TxValidator>(
            std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr),
            nullptr, std::make_shared<bcos::ledger::LedgerConfigState>(), nullptr, web3NonceChecker,
            [](bcos::protocol::Transaction const&) { return false; }, "group0", "chain0")},
        txpoolConfig{std::make_shared<bcos::txpool::TxPoolConfig>(txValidator, nullptr, nullptr,
            nullptr, std::make_shared<bcos::txvalidator::TxPoolNonceChecker>(), web3NonceChecker, 0,
            0, false)},
        txpoolStorage(txpoolConfig, *ioServicePool->getIOService())
    {
        BlockStat::enable();
    }
    ~TxPoolAdmissionEventsFixture() { BlockStat::disable(); }

    bcostars::protocol::TransactionImpl::Ptr makeTx(std::string nonce)
    {
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
        tx->setNonce(std::move(nonce));
        tx->mutableInner().data.groupID = "group0";
        tx->mutableInner().data.chainID = "chain0";
        tx->calculateHash(hashImpl);
        return tx;
    }

    bcos::txvalidator::Web3NonceChecker::Ptr web3NonceChecker;
    std::shared_ptr<bcos::txvalidator::TxValidator> txValidator;
    std::shared_ptr<bcos::txpool::TxPoolConfig> txpoolConfig;
    bcos::IOServicePool::Ptr ioServicePool =
        std::make_shared<bcos::IOServicePool>(1, "admissionEventsTest");
    ProbedMemoryStorage txpoolStorage;
    bcos::crypto::Keccak256 hashImpl;
};

BOOST_FIXTURE_TEST_SUITE(TxPoolAdmissionEventsTest, TxPoolAdmissionEventsFixture)

// the race in doVerifyAndSubmitTransaction: two submissions of one tx both pass
// txpoolStorageCheck and the second reaches insert() after the first is pooled
BOOST_AUTO_TEST_CASE(insertThatOnlyLendsItsCallbackIsNotAnAdmission)
{
    auto pooled = makeTx("race");
    BOOST_REQUIRE(txpoolStorage.insert(pooled) == protocol::TransactionStatus::None);
    BOOST_REQUIRE(!pooled->submitCallback());

    auto racer = makeTx("race");  // same hash, separate object, carries the RPC callback
    racer->setSubmitCallback([](Error::Ptr, protocol::TransactionSubmitResult::Ptr) {});
    BOOST_CHECK(
        txpoolStorage.insert(racer) == protocol::TransactionStatus::AlreadyInTxPoolAndAccept);
    BOOST_CHECK(pooled->submitCallback());  // the pooled copy now answers that client
    BOOST_CHECK_EQUAL(txpoolStorage.size(), 1U);
}

BOOST_AUTO_TEST_CASE(removalBelowTheLimitRecoversTheFullPool)
{
    txpoolConfig->setPoolLimit(2);
    auto tx1 = makeTx("f1");
    auto tx2 = makeTx("f2");
    BOOST_REQUIRE(txpoolStorage.verifyAndSubmitTransaction(tx1, nullptr, true, false) ==
                  protocol::TransactionStatus::None);
    BOOST_REQUIRE(txpoolStorage.verifyAndSubmitTransaction(tx2, nullptr, true, false) ==
                  protocol::TransactionStatus::None);
    BOOST_REQUIRE(txpoolStorage.verifyAndSubmitTransaction(makeTx("f3"), nullptr, true, false) ==
                  protocol::TransactionStatus::TxPoolIsFull);
    BOOST_REQUIRE(txpoolStorage.poolFull());

    // expiry / invalidation sweep: no admission follows, the flip must come from the removal
    LogCapture capture;
    std::vector<protocol::Transaction::Ptr> swept{tx1};
    txpoolStorage.removeInvalidTxs(swept);
    BOOST_CHECK_EQUAL(txpoolStorage.size(), 1U);
    BOOST_CHECK(!txpoolStorage.poolFull());
    auto text = capture.text();
    BOOST_CHECK_MESSAGE(text.find("TxPoolRecovered,pending=1,limit=2") != std::string::npos, text);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
