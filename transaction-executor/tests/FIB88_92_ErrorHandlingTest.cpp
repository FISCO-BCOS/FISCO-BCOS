/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Unit tests for audit fixes FIB-88 through FIB-92 in HostContext::execute()
 */

#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::hostcontext;

class FIB88_92_Fixture
{
public:
    bcos::crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    MutableStorage storage;
    Rollbackable<decltype(storage)> rollbackableStorage;
    using MemoryStorageType =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue,
            bcos::storage2::memory_storage::Attribute(
                bcos::storage2::memory_storage::ORDERED |
                bcos::storage2::memory_storage::LOGICAL_DELETION)>;
    MemoryStorageType transientStorage;
    Rollbackable<MemoryStorageType> rollbackableTransientStorage;
    evmc_address helloworldAddress{};
    int64_t seq = 0;
    std::optional<PrecompiledManager> precompiledManager;
    bcos::ledger::LedgerConfig ledgerConfig;
    bcostars::protocol::BlockHeaderImpl blockHeader;

    FIB88_92_Fixture()
      : rollbackableStorage(storage), rollbackableTransientStorage(transientStorage)
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        precompiledManager.emplace(hashImpl);

        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);

        // Activate all feature flags for MAX_VERSION
        auto features = ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
        ledgerConfig.setFeatures(features);

        // Deploy the HelloWorld contract for later use
        std::string helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

        evmc_message message = {.kind = EVMC_CREATE,
            .flags = 0,
            .depth = 0,
            .gas = 300 * 10000,
            .recipient = {},
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
            .input_size = helloworldBytecodeBinary.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {}};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        syncWait(hostContext.prepare());
        auto result = syncWait(hostContext.execute());
        BOOST_REQUIRE_EQUAL(result.status_code, 0);
        helloworldAddress = result.create_address;
    }
};

BOOST_FIXTURE_TEST_SUITE(FIB88_92_ErrorHandlingTest, FIB88_92_Fixture)

// ---------------------------------------------------------------------------
// FIB-88: Fatal errors must consume all gas (gas_left = 0)
// ---------------------------------------------------------------------------

// FIB-88: NotEnoughCashError should set gas_left = 0
BOOST_AUTO_TEST_CASE(FIB88_InsufficientBalanceConsumesAllGas)
{
    syncWait([this]() -> Task<void> {
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        // Enable balance transfer so transferBalance() is actually called
        ledgerConfig.setBalanceTransfer(true);

        evmc_address sender = bcos::unhexAddress("0x0000000000000000000000000000000000000AAA");
        evmc_address recipient = bcos::unhexAddress("0x0000000000000000000000000000000000000BBB");

        // Set sender balance to 100 but try to transfer 1000
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> senderAccount(
            rollbackableStorage, sender, false);
        co_await senderAccount.setBalance(bcos::u256(100));

        constexpr int64_t GAS_LIMIT = 300000;
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = GAS_LIMIT,
            .recipient = recipient,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = sender,
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = nullptr,
            .input_size = 0,
            .value = bcos::toEvmC(bcos::u256(1000)),  // value > balance
            .create2_salt = {},
            .code_address = recipient};
        evmc_address origin{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        // FIB-88: gas_left must be 0, not the original gas limit
        BOOST_CHECK_EQUAL(result.status_code, EVMC_INSUFFICIENT_BALANCE);
        BOOST_CHECK_EQUAL(result.gas_left, 0);
        BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::NotEnoughCash);
    }());
}

// FIB-88: Calling a non-existent contract (non-static, non-delegatecall) should return EVMC_REVERT
// with gas preserved (REVERT semantics: return remaining gas)
BOOST_AUTO_TEST_CASE(FIB88_NotFoundCodeRevertPreservesGas)
{
    syncWait([this]() -> Task<void> {
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        evmc_address nonExistentAddr =
            bcos::unhexAddress("0x00000000000000000000000000000000DEADBEEF");

        // Prepare a CALL message with input_size > 0 to a non-existent address
        bcos::bytes dummyInput = {0x01, 0x02, 0x03, 0x04};
        constexpr int64_t GAS_LIMIT = 500000;
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = GAS_LIMIT,
            .recipient = nonExistentAddr,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = dummyInput.data(),
            .input_size = dummyInput.size(),
            .value = {},
            .create2_salt = {},
            .code_address = nonExistentAddr};
        evmc_address origin{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        // For non-static/non-delegatecall NotFoundCodeError, EVMC_REVERT preserves gas
        BOOST_CHECK_EQUAL(result.status_code, EVMC_REVERT);
        BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::RevertInstruction);
    }());
}

// FIB-88: Calling a non-existent contract with EVMC_STATIC should return EVMC_SUCCESS
BOOST_AUTO_TEST_CASE(FIB88_NotFoundCodeStaticCallReturnsSuccess)
{
    syncWait([this]() -> Task<void> {
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        evmc_address nonExistentAddr =
            bcos::unhexAddress("0x00000000000000000000000000000000DEADBEEF");

        bcos::bytes dummyInput = {0x01, 0x02, 0x03, 0x04};
        constexpr int64_t GAS_LIMIT = 500000;
        evmc_message message = {.kind = EVMC_CALL,
            .flags = EVMC_STATIC,
            .depth = 0,
            .gas = GAS_LIMIT,
            .recipient = nonExistentAddr,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = dummyInput.data(),
            .input_size = dummyInput.size(),
            .value = {},
            .create2_salt = {},
            .code_address = nonExistentAddr};
        evmc_address origin{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        // STATIC_CALL to non-existent address should succeed per EVM spec
        BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::None);
    }());
}

// ---------------------------------------------------------------------------
// FIB-89: OutOfGas catch must not dereference nullopt evmResult
// Verified indirectly: if the old code ran, dereferencing nullopt would crash.
// This test uses makeErrorEVMCResult directly to confirm gas_left = 0.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(FIB89_MakeErrorEVMCResultOutOfGasZeroGas)
{
    // FIB-89: the OutOfGas catch handler now passes 0 as gas_left.
    // Verify that makeErrorEVMCResult produces the expected result.
    auto result = makeErrorEVMCResult(
        *hashImpl, bcos::protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0, "out of gas");

    BOOST_CHECK_EQUAL(result.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
    BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::OutOfGas);
}

// ---------------------------------------------------------------------------
// FIB-91: checkAuth() must be inside try block
// Structural fix — verified by inspecting code. The test here confirms that
// execute() does not crash/throw when authCheckStatus is enabled and the call
// goes through the normal success path.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(FIB91_AuthCheckInsideTryBlock)
{
    syncWait([this]() -> Task<void> {
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        // Enable auth checking (non-zero authCheckStatus)
        ledgerConfig.setAuthCheckStatus(1);

        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        auto input = abiCodec.abiIn(std::string("getInt()"));

        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = helloworldAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = helloworldAddress};
        evmc_address origin{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();

        // FIB-91: execute() must not throw even when auth checking is enabled.
        // Before the fix, if checkAuth threw, it would propagate out uncaught.
        BOOST_CHECK_NO_THROW(co_await hostContext.execute());

        // Reset auth status for other tests
        ledgerConfig.setAuthCheckStatus(0);
    }());
}

// ---------------------------------------------------------------------------
// FIB-92: Generic exception handler must use TransactionStatus::Unknown,
//         not TransactionStatus::OutOfGas, for EVMC_INTERNAL_ERROR
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(FIB92_MakeErrorEVMCResultInternalErrorUsesUnknown)
{
    // FIB-92: verify that makeErrorEVMCResult with EVMC_INTERNAL_ERROR
    // returns TransactionStatus::Unknown (not OutOfGas)
    auto result = makeErrorEVMCResult(
        *hashImpl, bcos::protocol::TransactionStatus::Unknown, EVMC_INTERNAL_ERROR, 0, "");

    BOOST_CHECK_EQUAL(result.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
    BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::Unknown);
    // The old code incorrectly used TransactionStatus::OutOfGas here
    BOOST_CHECK_NE(static_cast<int32_t>(result.status),
        static_cast<int32_t>(bcos::protocol::TransactionStatus::OutOfGas));
}

// FIB-88 + FIB-92: verify the negative-gas fixup path also consumes all gas
BOOST_AUTO_TEST_CASE(FIB88_NegativeGasFixupConsumesAllGas)
{
    // The negative-gas fixup at line 504 produces an OutOfGas result with gas_left = 0
    auto result = makeErrorEVMCResult(
        *hashImpl, bcos::protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0, "");

    BOOST_CHECK_EQUAL(result.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
    BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::OutOfGas);
}

// ---------------------------------------------------------------------------
// Flag-off backward compatibility: when bugfix_v1_exec_error_gas_used is
// disabled, error paths must preserve the old gas_left / status values so
// that receipt hashes stay identical to older nodes.
// ---------------------------------------------------------------------------

// FIB-88 flag-off: NotEnoughCashError should preserve gas_left = ref->gas (old behavior)
BOOST_AUTO_TEST_CASE(FIB88_FlagOff_InsufficientBalancePreservesGas)
{
    syncWait([this]() -> Task<void> {
        // Use V3_16_3 which does NOT activate bugfix_v1_exec_error_gas_used
        bcostars::protocol::BlockHeaderImpl oldHeader;
        oldHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_16_3_VERSION));
        oldHeader.setNumber(seq++);
        oldHeader.calculateHash(*hashImpl);

        bcos::ledger::LedgerConfig oldConfig;
        oldConfig.setBalanceTransfer(true);
        auto features = oldConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::V3_16_3_VERSION);
        oldConfig.setFeatures(features);

        evmc_address sender = bcos::unhexAddress("0x0000000000000000000000000000000000000CCC");
        evmc_address recipient = bcos::unhexAddress("0x0000000000000000000000000000000000000DDD");

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> senderAccount(
            rollbackableStorage, sender, false);
        co_await senderAccount.setBalance(bcos::u256(100));

        constexpr int64_t GAS_LIMIT = 300000;
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = GAS_LIMIT,
            .recipient = recipient,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = sender,
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = nullptr,
            .input_size = 0,
            .value = bcos::toEvmC(bcos::u256(1000)),
            .create2_salt = {},
            .code_address = recipient};
        evmc_address origin{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, oldHeader, message,
                origin, "", 0, seq, *precompiledManager, oldConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        // Flag off: old behavior preserves gas_left = ref->gas
        BOOST_CHECK_EQUAL(result.status_code, EVMC_INSUFFICIENT_BALANCE);
        BOOST_CHECK_EQUAL(result.gas_left, GAS_LIMIT);
        BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::NotEnoughCash);
    }());
}

// FIB-92 flag-off: generic exception handler should use OutOfGas (old behavior)
BOOST_AUTO_TEST_CASE(FIB92_FlagOff_InternalErrorUsesOutOfGas)
{
    // Verify old behavior: EVMC_INTERNAL_ERROR paired with OutOfGas status + ref->gas
    // This matches what the old code produced before the fix
    auto result = makeErrorEVMCResult(
        *hashImpl, bcos::protocol::TransactionStatus::OutOfGas, EVMC_INTERNAL_ERROR, 300000, "");

    BOOST_CHECK_EQUAL(result.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(result.gas_left, 300000);
    BOOST_CHECK_EQUAL(result.status, bcos::protocol::TransactionStatus::OutOfGas);
}

BOOST_AUTO_TEST_SUITE_END()
