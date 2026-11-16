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
 * @file Web3NonceLedgerReadTest.cpp
 * @brief Web3NonceChecker with an empty committed-nonce cache, reading a real Ledger.
 */
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/testutils/ScopedNodeAddressTableMode.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/txpool/Constant.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-table/src/StateStorage.h>
#include <bcos-task/Wait.h>
#include <bcos-tx-validator/Web3NonceChecker.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

namespace bcos::test
{
namespace
{
// FakeLedger cannot stand in here: its getStorageState returns whatever the test stored, so it
// never shows how the real Ledger reads an account's rows. The account below has the rows of a
// sender that never moved value on a free-gas v0/v1 chain -- a NONCE row and no BALANCE row.
//
// Every verdict is asked of a NEW Web3NonceChecker, whose committed-nonce cache is empty, so the
// committed nonce comes from the ledger alone.
struct Web3NonceLedgerReadFixture
{
    ScopedNodeAddressTableMode m_modeGuard{ledger::account::AddressTableMode::Hex};
    std::shared_ptr<storage::StateStorage> m_storage =
        std::make_shared<storage::StateStorage>(nullptr, false);
    std::shared_ptr<ledger::Ledger> m_ledger = std::make_shared<ledger::Ledger>(
        createBlockFactory(createNormalCryptoSuite()), m_storage, 0);
    std::string m_sender = Address::generateRandomFixedBytes().toRawString();

    void writeSenderRow(std::string_view field, std::string_view value)
    {
        task::syncWait(storage2::writeOne(*m_storage,
            executor_v1::StateKey("/apps/" + toHex(m_sender), field), storage::Entry{value}));
    }

    protocol::TransactionStatus freshCheckerVerdict(std::string const& nonce)
    {
        txvalidator::Web3NonceChecker checker(m_ledger);
        return task::syncWait(checker.checkWeb3Nonce(m_sender, nonce, true));
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(Web3NonceLedgerReadTest, Web3NonceLedgerReadFixture)

BOOST_AUTO_TEST_CASE(nonceOnlyAccountIsJudgedByItsCommittedNonce)
{
    writeSenderRow(ledger::ACCOUNT_TABLE_FIELDS::NONCE, "9");

    using protocol::TransactionStatus;
    auto const limit = protocol::DEFAULT_WEB3_NONCE_CHECK_LIMIT;
    // Nonces below the committed one have already been used.
    BOOST_CHECK_EQUAL(freshCheckerVerdict("5"), TransactionStatus::NonceCheckFail);
    BOOST_CHECK_EQUAL(freshCheckerVerdict("8"), TransactionStatus::NonceCheckFail);
    BOOST_CHECK_EQUAL(freshCheckerVerdict("9"), TransactionStatus::None);
    BOOST_CHECK_EQUAL(freshCheckerVerdict("10"), TransactionStatus::None);
    BOOST_CHECK_EQUAL(freshCheckerVerdict(std::to_string(9 + limit)), TransactionStatus::None);
    BOOST_CHECK_EQUAL(
        freshCheckerVerdict(std::to_string(9 + limit + 1)), TransactionStatus::NonceCheckFail);

    // The admission layer's Web3NonceWindow reads the same value (TxValidator::readAccountState).
    txvalidator::Web3NonceChecker checker(m_ledger);
    auto const committed = task::syncWait(checker.committedNonce(m_sender));
    BOOST_REQUIRE(committed.has_value());
    BOOST_CHECK_EQUAL(*committed, 9);
}

// An account with a balance row agrees with the nonce-only one: the balance row plays no part in
// the nonce verdict.
BOOST_AUTO_TEST_CASE(fundedAccountIsJudgedTheSameWay)
{
    writeSenderRow(ledger::ACCOUNT_TABLE_FIELDS::NONCE, "9");
    writeSenderRow(ledger::ACCOUNT_TABLE_FIELDS::BALANCE, "1000");

    using protocol::TransactionStatus;
    BOOST_CHECK_EQUAL(freshCheckerVerdict("5"), TransactionStatus::NonceCheckFail);
    BOOST_CHECK_EQUAL(freshCheckerVerdict("9"), TransactionStatus::None);
}

// No NONCE row: there is no committed nonce to judge against, and the checker still declines to
// judge the account (Web3NonceChecker::committedNonce).
BOOST_AUTO_TEST_CASE(accountWithoutNonceRowIsNotJudged)
{
    txvalidator::Web3NonceChecker checker(m_ledger);
    BOOST_CHECK(!task::syncWait(checker.committedNonce(m_sender)).has_value());
    BOOST_CHECK_EQUAL(freshCheckerVerdict("0"), protocol::TransactionStatus::None);
    BOOST_CHECK_EQUAL(freshCheckerVerdict("5"), protocol::TransactionStatus::None);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
