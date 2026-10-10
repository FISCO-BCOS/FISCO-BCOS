/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief TXPOOL BlockStat counters: admitted / rejected submissions are counted only while
 *        log.enable_block_stat is on, and takeAndReset drains the slots.
 */
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-utilities/BlockStat.h"
#include "bcos-utilities/IOServicePool.h"
#include <bcos-framework/ledger/LedgerConfigState.h>
#include <bcos-tx-validator/TxValidator.h>
#include <bcos-tx-validator/Web3NonceChecker.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
struct TxPoolBlockStatFixture
{
    TxPoolBlockStatFixture()
      : web3NonceChecker{std::make_shared<bcos::txvalidator::Web3NonceChecker>(nullptr)},
        txValidator{std::make_shared<bcos::txvalidator::TxValidator>(
            std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr),
            nullptr, std::make_shared<bcos::ledger::LedgerConfigState>(), nullptr, web3NonceChecker,
            [](bcos::protocol::Transaction const&) { return false; }, "group0", "chain0")},
        txpoolConfig{std::make_shared<bcos::txpool::TxPoolConfig>(
            txValidator, nullptr, nullptr, nullptr, nullptr, web3NonceChecker, 0, 0, false)},
        txpoolStorage(txpoolConfig, *ioServicePool->getIOService())
    {
        BlockStat::enable();
    }
    ~TxPoolBlockStatFixture() { BlockStat::disable(); }

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
        std::make_shared<bcos::IOServicePool>(1, "blockStatTest");
    bcos::txpool::MemoryStorage txpoolStorage;
    bcos::crypto::Keccak256 hashImpl;
};

BOOST_FIXTURE_TEST_SUITE(TxPoolBlockStatTest, TxPoolBlockStatFixture)

BOOST_AUTO_TEST_CASE(countsAdmittedAndDuplicateRejections)
{
    using Slot = bcos::txpool::MemoryStorage::BlockStatSlot;
    auto tx1 = makeTx("n1");
    auto tx2 = makeTx("n2");
    auto tx3 = makeTx("n3");
    for (auto const& tx : {tx1, tx2, tx3})
    {
        BOOST_CHECK(txpoolStorage.verifyAndSubmitTransaction(tx, nullptr, false, false) ==
                    protocol::TransactionStatus::None);
    }
    // the same two transactions again: already pooled, refused as AlreadyInTxPool
    for (auto const& tx : {tx1, tx2})
    {
        BOOST_CHECK(txpoolStorage.verifyAndSubmitTransaction(tx, nullptr, false, false) ==
                    protocol::TransactionStatus::AlreadyInTxPool);
    }

    auto& counters = txpoolStorage.blockStatCounters();
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::Added), 3U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::Rejected), 2U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::RejectDuplicate), 2U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::RejectNonce), 0U);
    // drained
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::Added), 0U);
}

BOOST_AUTO_TEST_CASE(disabledSwitchCountsNothing)
{
    using Slot = bcos::txpool::MemoryStorage::BlockStatSlot;
    BlockStat::disable();
    auto tx = makeTx("n9");
    BOOST_CHECK(txpoolStorage.verifyAndSubmitTransaction(tx, nullptr, false, false) ==
                protocol::TransactionStatus::None);
    BOOST_CHECK(txpoolStorage.verifyAndSubmitTransaction(tx, nullptr, false, false) ==
                protocol::TransactionStatus::AlreadyInTxPool);
    auto& counters = txpoolStorage.blockStatCounters();
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::Added), 0U);
    BOOST_CHECK_EQUAL(counters.takeAndReset(Slot::Rejected), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
