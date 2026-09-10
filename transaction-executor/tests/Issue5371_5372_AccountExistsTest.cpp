/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression tests for issues #5371 (HostContext::exists() always true) and #5372
 *        (EXTCODEHASH 0 for a live code-less account), behind bugfix_eip161_1052_account_semantics.
 */
#include "../bcos-transaction-executor/EVMCResult.h"
#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::task;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::hostcontext;

namespace bcos::test
{
struct AccountExistsFixture
{
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();
    MutableStorage storage;
    Rollbackable<decltype(storage)> rollbackableStorage{storage};
    using TransientStorageType = storage2::memory_storage::MemoryStorage<StateKey, StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
    TransientStorageType transientStorage;
    Rollbackable<TransientStorageType> rollbackableTransientStorage{transientStorage};
    int64_t seq = 0;
    PrecompiledManager precompiledManager{hashImpl};
    ledger::LedgerConfig ledgerConfig;
    bcostars::protocol::BlockHeaderImpl blockHeader;
    evmc_message message{};
    evmc_address origin{};

    AccountExistsFixture()
    {
        blockHeader.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);
        message.kind = EVMC_CALL;
        message.gas = 100000;
    }

    void enableFix(bool on)
    {
        ledger::Features features;
        if (on)
        {
            features.set(ledger::Features::Flag::bugfix_eip161_1052_account_semantics);
        }
        ledgerConfig.setFeatures(features);
    }

    static evmc_address addr(uint8_t last)
    {
        evmc_address a{};
        a.bytes[19] = last;
        return a;
    }

    /// Runner (code-bearing) addresses get a suite-specific prefix: the executable cache is
    /// process-wide and keyed by address alone, and the EIP-2929 helpers in this binary already
    /// put other code at 0x..71 / 0x..72.
    static evmc_address runnerAddr(uint8_t tag)
    {
        evmc_address a{};
        a.bytes[0] = 0x53;
        a.bytes[1] = 0x71;
        a.bytes[19] = tag;
        return a;
    }

    auto makeHost()
    {
        return HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>(
            rollbackableStorage, rollbackableTransientStorage, blockHeader, message, origin, "", 0,
            seq, precompiledManager, ledgerConfig, *hashImpl, false, 0, bcos::task::syncWait);
    }

    ledger::account::EVMAccount<decltype(rollbackableStorage)> account(const evmc_address& a)
    {
        return {rollbackableStorage, a, false};
    }

    h256 emptyCodeHash() const { return hashImpl->hash(bytesConstRef{}); }

    /// Run @p code as the body of a funded runner contract at @p runnerTag under @p features;
    /// returns the raw EVMC result (gas_left, output). Same shape as
    /// eip2929::detail::measureProbeGasTask, without that helper's fixture contract.
    task::Task<EVMCResult> runBytecode(
        ledger::Features features, bytes const& code, uint8_t runnerTag, int64_t startGas)
    {
        ledgerConfig.setFeatures(features);
        auto originAddr = addr(0x70);
        auto runner = runnerAddr(runnerTag);
        auto originAcc = account(originAddr);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(u256(1) << 96);
        auto runnerAcc = account(runner);
        if (!co_await runnerAcc.exists())
        {
            co_await runnerAcc.create();
        }
        co_await runnerAcc.setBalance(u256(1) << 96);
        co_await runnerAcc.setCode(code, "", hashImpl->hash(ref(code)));

        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.gas = startGas;
        msg.recipient = runner;
        msg.code_address = runner;
        msg.sender = originAddr;
        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)> host(
            rollbackableStorage, rollbackableTransientStorage, blockHeader, msg, originAddr, "", 0,
            seq, precompiledManager, ledgerConfig, *hashImpl, false, 0, bcos::task::syncWait);
        co_await host.prepare();
        co_return co_await host.execute();
    }

    static ledger::Features featuresWithFix(bool on)
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_evm_cancun);  // explicit; HostContext floors
                                                                   // m_revision at CANCUN anyway
        if (on)
        {
            features.set(ledger::Features::Flag::bugfix_eip161_1052_account_semantics);
        }
        return features;
    }

    /// PUSH1 0 x4 (retSize retOffset argsSize argsOffset) PUSH1 1 (value) PUSH20 <to>
    /// PUSH2 gas CALL STOP
    static bytes callWithOneWeiBytecode(const evmc_address& to)
    {
        bytes code;
        for (int i = 0; i < 4; ++i)
        {
            code.insert(code.end(), {0x60, 0x00});
        }
        code.insert(code.end(), {0x60, 0x01});
        code.push_back(0x73);
        code.insert(code.end(), to.bytes, to.bytes + 20);
        code.insert(code.end(), {0x61, 0xff, 0xff});
        code.push_back(0xf1);
        code.push_back(0x00);
        return code;
    }

    /// PUSH20 <a> EXTCODEHASH PUSH1 0 MSTORE PUSH1 32 PUSH1 0 RETURN
    static bytes extCodeHashBytecode(const evmc_address& a)
    {
        bytes code{0x73};
        code.insert(code.end(), a.bytes, a.bytes + 20);
        code.insert(code.end(), {0x3f, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3});
        return code;
    }
};

BOOST_FIXTURE_TEST_SUITE(Issue5371_5372_AccountExists, AccountExistsFixture)

BOOST_AUTO_TEST_CASE(absentAccountDoesNotExistAndHashesToZero)
{
    enableFix(true);
    auto host = makeHost();
    BOOST_CHECK(!syncWait(host.exists(addr(0x01))));
    BOOST_CHECK(syncWait(host.codeHashAt(addr(0x01))) == h256{});
}

BOOST_AUTO_TEST_CASE(fundedEoaExistsAndHashesToEmptyCode)
{
    enableFix(true);
    auto eoa = account(addr(0x02));
    syncWait(eoa.create());
    syncWait(eoa.setBalance(u256(1)));

    auto host = makeHost();
    BOOST_CHECK(syncWait(host.exists(addr(0x02))));
    BOOST_CHECK(syncWait(host.codeHashAt(addr(0x02))) == emptyCodeHash());
}

BOOST_AUTO_TEST_CASE(emptyAccountIsAbsentPerEip161)
{
    enableFix(true);
    auto empty = account(addr(0x03));
    syncWait(empty.create());  // table exists, nonce 0, balance 0, no code

    auto host = makeHost();
    BOOST_CHECK(!syncWait(host.exists(addr(0x03))));
    BOOST_CHECK(syncWait(host.codeHashAt(addr(0x03))) == h256{});
}

BOOST_AUTO_TEST_CASE(contractHashesToItsCode)
{
    enableFix(true);
    auto contract = account(addr(0x04));
    syncWait(contract.create());
    bytes code{0x60, 0x00, 0x60, 0x00, 0xf3};
    auto codeHash = hashImpl->hash(ref(code));
    syncWait(contract.setCode(code, "", codeHash));

    auto host = makeHost();
    BOOST_CHECK(syncWait(host.exists(addr(0x04))));
    BOOST_CHECK(syncWait(host.codeHashAt(addr(0x04))) == codeHash);
}

BOOST_AUTO_TEST_CASE(legacyBehaviourWithoutFlag)
{
    enableFix(false);
    auto eoa = account(addr(0x05));
    syncWait(eoa.create());
    syncWait(eoa.setBalance(u256(1)));

    auto host = makeHost();
    BOOST_CHECK(syncWait(host.exists(addr(0x05))));
    BOOST_CHECK(syncWait(host.exists(addr(0x06))));                // absent, still true pre-fix
    BOOST_CHECK(syncWait(host.codeHashAt(addr(0x05))) == h256{});  // pre-fix zero
}

// The consensus-visible effect of #5371: evmone charges EIP-161's 25000 new-account gas for a
// value-carrying CALL to an address that does not exist. It never did before, because exists()
// answered true. (The charge does not depend on feature_balance: evmone looks at msg.value only.)
BOOST_AUTO_TEST_CASE(valueCallToFreshAddressChargesNewAccountGasOnlyWithFix)
{
    constexpr int64_t startGas = 1'000'000;
    auto used = [&](bool fix, uint8_t target, uint8_t runnerTag) {
        auto r = syncWait(runBytecode(
            featuresWithFix(fix), callWithOneWeiBytecode(addr(target)), runnerTag, startGas));
        BOOST_REQUIRE_EQUAL(r.status_code, EVMC_SUCCESS);
        return startGas - r.gas_left;
    };
    // Warm-up: the first top-level frame in a fixture also pays BALANCE_TRANSFER_GAS
    // (consumeTransferGas at m_level == 0); the level counter is shared across HostContext
    // instances, so every later run in this fixture is a like-for-like comparison.
    (void)used(false, 0x90, 0x70);

    auto eoa = account(addr(0x97));
    syncWait(eoa.create());
    syncWait(eoa.setBalance(u256(1)));

    auto offAbsent = used(false, 0x98, 0x71);
    auto onAbsent = used(true, 0x99, 0x72);
    auto onExisting = used(true, 0x97, 0x73);
    BOOST_CHECK_EQUAL(onAbsent - offAbsent, 25000);
    BOOST_CHECK_EQUAL(onExisting, offAbsent);  // an existing callee is never charged for
}

// The consensus-visible effect of #5372: the EXTCODEHASH opcode's stack value.
BOOST_AUTO_TEST_CASE(extCodeHashOpcodeFollowsEip1052WithFix)
{
    auto eoa = account(addr(0x0a));
    syncWait(eoa.create());
    syncWait(eoa.setBalance(u256(1)));

    // Runner tags are unique across this suite: a runner address must never carry two codes.
    auto readHash = [](EVMCResult const& r) {
        BOOST_REQUIRE_EQUAL(r.status_code, EVMC_SUCCESS);
        BOOST_REQUIRE_EQUAL(r.output_size, 32U);
        return h256(bytesConstRef(r.output_data, r.output_size));
    };
    // flag off: funded EOA hashes to zero (the pre-fix answer)
    BOOST_CHECK(readHash(syncWait(runBytecode(featuresWithFix(false),
                    extCodeHashBytecode(addr(0x0a)), 0x80, 500'000))) == h256{});
    // flag on: funded EOA -> hash(""), absent -> 0
    BOOST_CHECK(readHash(syncWait(runBytecode(featuresWithFix(true),
                    extCodeHashBytecode(addr(0x0a)), 0x81, 500'000))) == emptyCodeHash());
    BOOST_CHECK(readHash(syncWait(runBytecode(featuresWithFix(true),
                    extCodeHashBytecode(addr(0x0b)), 0x82, 500'000))) == h256{});
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
