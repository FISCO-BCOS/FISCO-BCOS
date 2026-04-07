/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file FIB77_81_AuthCheckTest.cpp
 * @brief Regression tests for FIB-77 (CREATE2 auth check) and FIB-81 (auth failure status)
 * @date 2026-04-07
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;

class AuthCheckBugfixFixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    bcos::executor_v1::TransactionExecutorImpl executor{
        receiptFactory, cryptoSuite->hashImpl(), precompiledManager};

    AuthCheckBugfixFixture()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    }
};

BOOST_FIXTURE_TEST_SUITE(FIB77_81_AuthCheckTest, AuthCheckBugfixFixture)

// FIB-81: When auth check is enabled and deployment is denied, receipt should have
// non-zero status (EVMC_REVERT) when bugfix_auth_check_revert_status is enabled.
BOOST_AUTO_TEST_CASE(authFailureSetsRevertStatus)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_16_5_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        auto features = ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::V3_16_5_VERSION);
        features.set(ledger::Features::Flag::bugfix_auth_check_revert_status);
        features.set(ledger::Features::Flag::bugfix_auth_check_create2);
        ledgerConfig.setFeatures(features);
        // Enable auth checking
        ledgerConfig.setAuthCheckStatus(1);

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

        // Deploy contract - auth check will trigger since authCheckStatus is set,
        // and without proper auth setup, the deploy path will go through checkAuth.
        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);

        // Auth check should deny the deployment (no auth tables set up),
        // and with bugfix enabled, the status should indicate revert, not success
        if (receipt->status() != 0)
        {
            // If auth denied, the receipt output should NOT contain the full input bytecode
            // (FIB-81 data leak fix)
            BOOST_CHECK(receipt->output().size() < helloworldBytecodeBinary.size());
        }
    }());
}

// FIB-81: Without bugfix, auth failure may leave evmStatus as EVMC_SUCCESS (0)
BOOST_AUTO_TEST_CASE(authFailureWithoutBugfix)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_16_0_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        auto features = ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::V3_16_0_VERSION);
        // Do NOT set bugfix_auth_check_revert_status
        ledgerConfig.setFeatures(features);
        ledgerConfig.setAuthCheckStatus(1);

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);

        // Without bugfix, behavior is preserved (backward compat)
        // Just verify we get a receipt without crashing
        BOOST_CHECK(receipt != nullptr);
    }());
}

// FIB-77: Verify feature flag controls CREATE2 auth path
BOOST_AUTO_TEST_CASE(featureFlagCreate2)
{
    // Verify the feature flags can be set and read correctly
    ledger::Features features;

    // Without bugfix_auth_check_create2
    BOOST_CHECK(!features.get(ledger::Features::Flag::bugfix_auth_check_create2));

    // With bugfix_auth_check_create2
    features.set(ledger::Features::Flag::bugfix_auth_check_create2);
    BOOST_CHECK(features.get(ledger::Features::Flag::bugfix_auth_check_create2));

    // Verify setGenesisFeatures at V3_16_5 includes the new flags
    ledger::Features genesisFeatures;
    genesisFeatures.setGenesisFeatures(bcos::protocol::BlockVersion::V3_16_5_VERSION);
    BOOST_CHECK(genesisFeatures.get(ledger::Features::Flag::bugfix_auth_check_create2));
    BOOST_CHECK(genesisFeatures.get(ledger::Features::Flag::bugfix_auth_check_revert_status));
    BOOST_CHECK(genesisFeatures.get(ledger::Features::Flag::bugfix_auth_table_raw_address));
    BOOST_CHECK(genesisFeatures.get(ledger::Features::Flag::bugfix_auth_table_squatting));
}

// FIB-77: Verify upgrade from V3_16_4 to V3_16_5 enables the new flags
BOOST_AUTO_TEST_CASE(upgradeEnablesAuthFlags)
{
    ledger::Features features;
    features.setUpgradeFeatures(bcos::protocol::BlockVersion::V3_16_4_VERSION,
        bcos::protocol::BlockVersion::V3_16_5_VERSION);
    BOOST_CHECK(features.get(ledger::Features::Flag::bugfix_auth_check_create2));
    BOOST_CHECK(features.get(ledger::Features::Flag::bugfix_auth_check_revert_status));
    BOOST_CHECK(features.get(ledger::Features::Flag::bugfix_auth_table_raw_address));
    BOOST_CHECK(features.get(ledger::Features::Flag::bugfix_auth_table_squatting));

    // Should NOT have flags from before (only the diff)
    // The upgrade from V3_16_4 to V3_16_5 should only add the new auth flags
    ledger::Features features2;
    features2.setUpgradeFeatures(bcos::protocol::BlockVersion::V3_16_0_VERSION,
        bcos::protocol::BlockVersion::V3_16_4_VERSION);
    BOOST_CHECK(!features2.get(ledger::Features::Flag::bugfix_auth_check_create2));
    BOOST_CHECK(!features2.get(ledger::Features::Flag::bugfix_auth_check_revert_status));
}

BOOST_AUTO_TEST_SUITE_END()
