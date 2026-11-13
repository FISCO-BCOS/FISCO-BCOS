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
 * @file test_LedgerStorageStateNonceOnly.cpp
 * @brief Ledger::getStorageState on an account that has a NONCE row and no BALANCE row.
 */
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/testutils/ScopedNodeAddressTableMode.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-table/src/StateStorage.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

namespace bcos::test
{
namespace
{
// A Web3 sender's committed nonce lives in its NONCE row; a BALANCE row exists only once something
// writes the balance -- a transfer, gas charged on a chain whose tx_gas_price is above zero, a
// genesis alloc. A sender that never moved value on a free-gas chain has a nonce and no balance.
struct NonceOnlyAccountFixture
{
    ScopedNodeAddressTableMode m_modeGuard{ledger::account::AddressTableMode::Hex};
    std::shared_ptr<storage::StateStorage> m_storage =
        std::make_shared<storage::StateStorage>(nullptr, false);
    std::shared_ptr<ledger::Ledger> m_ledger = std::make_shared<ledger::Ledger>(
        createBlockFactory(createNormalCryptoSuite()), m_storage, 0);
    std::string m_hexAddress = "4200000000000000000000000000000000005a5a";

    void writeAccountRow(std::string_view field, std::string_view value)
    {
        task::syncWait(storage2::writeOne(*m_storage,
            executor_v1::StateKey("/apps/" + m_hexAddress, field), storage::Entry{value}));
    }

    std::optional<ledger::StorageState> readStorageState()
    {
        // Ledger's override is private; the callers reach it through the interface.
        ledger::LedgerInterface& ledgerInterface = *m_ledger;
        return task::syncWait(ledgerInterface.getStorageState(m_hexAddress, 0));
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(LedgerStorageStateNonceOnlyTest, NonceOnlyAccountFixture)

BOOST_AUTO_TEST_CASE(nonceRowWithoutBalanceRowIsAnAccount)
{
    writeAccountRow(ledger::ACCOUNT_TABLE_FIELDS::NONCE, "9");

    auto const state = readStorageState();
    BOOST_REQUIRE(state.has_value());
    BOOST_CHECK_EQUAL(state->nonce, "9");
    BOOST_CHECK(state->balance.empty());
}

BOOST_AUTO_TEST_CASE(nonceAndBalanceRowsAreBothReturned)
{
    writeAccountRow(ledger::ACCOUNT_TABLE_FIELDS::NONCE, "9");
    writeAccountRow(ledger::ACCOUNT_TABLE_FIELDS::BALANCE, "12345");

    auto const state = readStorageState();
    BOOST_REQUIRE(state.has_value());
    BOOST_CHECK_EQUAL(state->nonce, "9");
    BOOST_CHECK_EQUAL(state->balance, "12345");
}

// Without a NONCE row there is no committed nonce to judge against, and callers keep reading the
// account as absent (Web3NonceChecker::committedNonce declines to judge it).
BOOST_AUTO_TEST_CASE(accountWithoutNonceRowIsAbsent)
{
    BOOST_CHECK(!readStorageState().has_value());

    writeAccountRow(ledger::ACCOUNT_TABLE_FIELDS::BALANCE, "12345");
    BOOST_CHECK(!readStorageState().has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
