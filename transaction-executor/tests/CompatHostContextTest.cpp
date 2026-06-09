/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE-FC-*: transaction-executor forward-compatibility (revision, 2929).
 *  @file CompatHostContextTest.cpp
 */

// TE EIP-2929 test scope (CompatHostContextTest):
// - Revision floor: CANCUN (toRevision never returns < EVMC_CANCUN for TE paths).
// - Pre-Berlin / London cold-access behavior: bcos-executor CompatEip2929Test.cpp.
// - EthTxGasSettlementHost suite disabled below (#if 0) — needs TE gas-settlement / EIP-7702 APIs.

#include "../bcos-transaction-executor/vm/HostContext.h"
#include "Eip2929TestHelpers.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/vm/Eip2929AccessState.h"
#include "bcos-executor/src/vm/VMInstance.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::hostcontext;

namespace bcos::test
{

class CompatTEHostFixture
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
    std::optional<PrecompiledManager> precompiledManager;
    bcos::ledger::LedgerConfig ledgerConfig;
    bcostars::protocol::BlockHeaderImpl blockHeader;
    int64_t seq = 0;

    CompatTEHostFixture()
      : rollbackableStorage(storage), rollbackableTransientStorage(transientStorage)
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = hashImpl;
        precompiledManager.emplace(hashImpl);
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);
    }

    HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)> makeHost(
        bcos::ledger::Features const& features,
        uint32_t blockHeaderVersion = static_cast<uint32_t>(
            bcos::protocol::BlockVersion::MAX_VERSION),
        evmc_address originIn = {}, evmc_address recipientIn = {},
        evmc_call_kind kindIn = EVMC_CALL,
        std::shared_ptr<const bcos::executor::Eip2930AccessList> eip2930AccessList = {},
        uint8_t web3TypedTxKindForAccessList = 0, int64_t gas = 1'000'000,
        std::shared_ptr<bcos::executor::Eip2929AccessState> eip2929Access = nullptr)
    {
        if (!eip2929Access)
        {
            eip2929Access = std::make_shared<bcos::executor::Eip2929AccessState>();
        }
        ledgerConfig.setFeatures(features);
        blockHeader.setVersion(blockHeaderVersion);
        blockHeader.calculateHash(*hashImpl);
        evmc_message message = {.kind = kindIn,
            .flags = 0,
            .depth = 0,
            .gas = gas,
            .recipient = recipientIn,
            .sender = {},
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = {},
            .code = nullptr,
            .code_size = 0,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender_ptr = nullptr,
            .sender_len = 0};
        return HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>(
            rollbackableStorage, rollbackableTransientStorage, blockHeader, message, originIn, "",
            0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0, bcos::task::syncWait,
            std::move(eip2930AccessList), web3TypedTxKindForAccessList, std::move(eip2929Access));
    }

    /// Feature profiles for EIP-2929 matrix tests (see eip2929::makeFeatures*).
    bcos::ledger::Features pragueEip2929Features() const
    {
        return eip2929::makeFeaturesPragueEip2929();
    }
    bcos::ledger::Features cancunEip2929Features() const
    {
        return eip2929::makeFeaturesCancunEip2929();
    }
    bcos::ledger::Features shanghaiEip2929Features() const
    {
        return eip2929::makeFeaturesShanghaiEip2929();
    }
    bcos::ledger::Features osakaEip2929Features() const
    {
        return eip2929::makeFeaturesOsakaEip2929();
    }

    int64_t measureDoubleExtCodeSizeGas(bcos::ledger::Features const& features,
        evmc_address const& target, int64_t startGas = 2'000'000, uint8_t runnerTag = 0x71)
    {
        return eip2929::measureDoubleExtCodeSizeGas(*this, features, target, startGas, runnerTag);
    }

    int64_t measureTwoAccountsExtCodeSizeGas(bcos::ledger::Features const& features,
        evmc_address const& target1, evmc_address const& target2, int64_t startGas = 2'000'000,
        uint8_t runnerTag = 0x72)
    {
        return eip2929::measureTwoAccountsExtCodeSizeGas(
            *this, features, target1, target2, startGas, runnerTag);
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatHostContext, CompatTEHostFixture)

BOOST_AUTO_TEST_CASE(TE_FC_revision_cancun_only)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK_EQUAL(bcos::executor::toRevision(features,
                          static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION)),
        EVMC_CANCUN);
}

BOOST_AUTO_TEST_CASE(TE_FC_revision_osaka_prague_chain)
{
    bcos::ledger::Features osaka;
    osaka.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    osaka.set(bcos::ledger::Features::Flag::feature_evm_osaka);
    BOOST_CHECK_EQUAL(bcos::executor::toRevision(
                          osaka, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION)),
        EVMC_OSAKA);

    bcos::ledger::Features prague;
    prague.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    prague.set(bcos::ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK_EQUAL(bcos::executor::toRevision(
                          prague, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION)),
        EVMC_PRAGUE);

    bcos::ledger::Features cancun;
    cancun.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    cancun.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK_EQUAL(bcos::executor::toRevision(
                          cancun, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION)),
        EVMC_CANCUN);
}

BOOST_AUTO_TEST_CASE(TE_FC_revision_fallback_london)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::V3_0_VERSION);
    ledgerConfig.setFeatures(features);
    blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_0_VERSION));
    blockHeader.calculateHash(*hashImpl);

    auto revision = bcos::executor::toRevision(ledgerConfig.features(), blockHeader.version());
    BOOST_CHECK_EQUAL(revision, EVMC_LONDON);
}

BOOST_AUTO_TEST_CASE(TE_FC_revision_shanghai_without_cancun)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    ledgerConfig.setFeatures(features);
    blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION));
    blockHeader.calculateHash(*hashImpl);

    auto revision = bcos::executor::toRevision(ledgerConfig.features(), blockHeader.version());
    BOOST_CHECK_EQUAL(revision, EVMC_PARIS);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_helpers_smoke)
{
    (void)pragueEip2929Features();
    (void)cancunEip2929Features();
    (void)shanghaiEip2929Features();
    (void)osakaEip2929Features();

    evmc_address sender{};
    sender.bytes[19] = 0x01;
    evmc_bytes32 salt{};
    auto const initcode = eip2929::revertInitcode();
    auto const msg = eip2929::makeCreate2Message(
        sender, salt, bcos::bytesConstRef(initcode.data(), initcode.size()), 1'000'000);
    BOOST_CHECK_EQUAL(msg.kind, EVMC_CREATE2);
    BOOST_CHECK_EQUAL(msg.input_size, initcode.size());
    BOOST_CHECK(msg.input_data == initcode.data());

    auto const accessList = eip2929::makeAccessListSingleAccountMultiSlot(
        "00000000000000000000000000000000deadbeef", {h256(1), h256(2)});
    BOOST_CHECK_EQUAL(accessList.size(), 1U);
    BOOST_CHECK_EQUAL(accessList[0].first, "00000000000000000000000000000000deadbeef");
    BOOST_CHECK_EQUAL(accessList[0].second.size(), 2U);

    auto const multiList = eip2929::makeAccessListMultiAccount(
        {{"00000000000000000000000000000000000000aa", {h256(3)}},
            {"00000000000000000000000000000000000000bb", {h256(4), h256(5)}}});
    BOOST_CHECK_EQUAL(multiList.size(), 2U);

    BOOST_CHECK(!shanghaiEip2929Features().get(bcos::ledger::Features::Flag::feature_evm_cancun));
}

BOOST_AUTO_TEST_CASE(TE_FC_7_calldata_floor)
{
    using namespace bcos::executor;
    bytes mixed(100);
    for (int i = 0; i < 50; ++i)
        mixed[i] = 0x00;
    for (int i = 50; i < 100; ++i)
        mixed[i] = 0x42;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(mixed)), 2500);
}

BOOST_AUTO_TEST_CASE(TE_FC_7_calldata_floor_overflow_guard_saturates)
{
    using namespace bcos::executor;
    constexpr auto maxSafeBytes =
        static_cast<size_t>(std::numeric_limits<int64_t>::max() /
                            (TOKENS_PER_NONZERO_BYTE * TOTAL_COST_FLOOR_PER_TOKEN));
    bcos::bytesConstRef hugeRef(nullptr, maxSafeBytes + 1);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(hugeRef), std::numeric_limits<int64_t>::max());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_warm_storage)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    auto host = makeHost(features);
    evmc_address addr{};
    std::memset(addr.bytes, 0x77, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0x88, sizeof(key.bytes));

    BOOST_CHECK_EQUAL(host.accessStorage(addr, key), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_warmup_api_idempotent)
{
    bcos::executor::Eip2929AccessState accessState;
    evmc_address addr{};
    std::memset(addr.bytes, 0x51, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0x61, sizeof(key.bytes));

    BOOST_CHECK(accessState.warmUpAddress(addr));
    BOOST_CHECK(!accessState.warmUpAddress(addr));
    BOOST_CHECK(accessState.containsAddress(addr));

    BOOST_CHECK(accessState.warmUpStorage(addr, key));
    BOOST_CHECK(!accessState.warmUpStorage(addr, key));
    BOOST_CHECK(accessState.containsStorage(addr, key));
}

BOOST_AUTO_TEST_CASE(TE_FC_eip2929_access_account)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    auto host = makeHost(features);
    evmc_address addr{};
    std::memset(addr.bytes, 0x11, sizeof(addr.bytes));

    BOOST_CHECK_EQUAL(host.accessAccount(addr), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr), EVMC_ACCESS_WARM);

    bcos::ledger::Features no2929;
    no2929.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    no2929.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    auto host2 = makeHost(no2929);
    BOOST_CHECK_EQUAL(host2.accessAccount(addr), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host2.accessAccount(addr), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_cold_warm_gas_extcodesize)
{
    // EIP-2929 EXTCODESIZE: cold ~2600, warm ~100. Full tx gas is ~21k+ (not comparable
    // to raw opcode costs); contrast double-probe on same addr (cold+warm) vs two addrs
    // (cold+cold): expect ~2500 gas delta from the second account access alone.
    auto const features = pragueEip2929Features();
    evmc_address target{};
    target.bytes[19] = 0x42;
    evmc_address target2{};
    target2.bytes[19] = 0x43;

    auto const doubleSameGas = measureDoubleExtCodeSizeGas(features, target, 2'000'000, 0x71);
    auto const twoColdGas =
        measureTwoAccountsExtCodeSizeGas(features, target, target2, 2'000'000, 0x72);
    BOOST_REQUIRE(doubleSameGas >= 0);
    BOOST_REQUIRE(twoColdGas >= 0);
    BOOST_CHECK(twoColdGas > doubleSameGas + 2000);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_flag_on_cold_warm_cycle)
{
    // TE m_revision is floored at CANCUN; pre-Berlin rev-gate is covered by executor
    // CompatEip2929Test (FC_A_revision_gate_eip2929_on_prefork_evmc_rev_always_cold).
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);
    auto host = makeHost(features);
    BOOST_CHECK_EQUAL(
        bcos::executor::toRevision(host.ledgerConfig().features(), host.blockVersion()),
        EVMC_PRAGUE);

    evmc_address addr{};
    std::memset(addr.bytes, 0xde, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0xed, sizeof(key.bytes));

    BOOST_CHECK_EQUAL(host.accessAccount(addr), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_flag_off_never_mutates_warm_set)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    auto access = std::make_shared<bcos::executor::Eip2929AccessState>();
    evmc_address origin{};
    origin.bytes[19] = 0x01;
    evmc_address recipient{};
    recipient.bytes[19] = 0x02;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, {}, 0, 1'000'000, access);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address addr{};
    addr.bytes[19] = 0x42;
    BOOST_CHECK(!access->containsAddress(addr));

    BOOST_CHECK_EQUAL(host.accessAccount(addr), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr), EVMC_ACCESS_COLD);
    BOOST_CHECK(!access->containsAddress(addr));
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2930_legacy_kind_ignores_access_list)
{
    auto const features = pragueEip2929Features();
    evmc_address origin{};
    origin.bytes[19] = 0x11;
    evmc_address recipient{};
    recipient.bytes[19] = 0x22;
    h256 const storageKey(0x42424242);
    auto accessList = std::make_shared<const bcos::executor::Eip2930AccessList>(
        eip2929::makeAccessListSingleAccountMultiSlot(
            "00000000000000000000000000000000deadbeef", {storageKey}));
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, accessList, 0);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address const listAddr = unhexAddress("00000000000000000000000000000000deadbeef");
    BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_warm_shared_across_external_call_depth)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    using HostTy =
        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>;
    HostTy parent = makeHost(features);
    evmc_address warmed{};
    std::memset(warmed.bytes, 0xaa, sizeof(warmed.bytes));
    evmc_address emptyCallee{};
    std::memset(emptyCallee.bytes, 0xcc, sizeof(emptyCallee.bytes));

    BOOST_CHECK_EQUAL(parent.accessAccount(warmed), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(parent.accessAccount(warmed), EVMC_ACCESS_WARM);

    // New top-level HostContext gets a fresh Eip2929AccessState (isolation); same instance must
    // keep warm sets across externalCall (see HostContext::externalCall inner ctor).
    HostTy unrelatedTopLevel = makeHost(features);
    BOOST_CHECK_EQUAL(unrelatedTopLevel.accessAccount(warmed), EVMC_ACCESS_COLD);

    evmc_message nested = {.kind = EVMC_CALL,
        .flags = 0,
        .depth = parent.message().depth + 1,
        .gas = 500000,
        .recipient = emptyCallee,
        .sender = parent.message().recipient,
        .input_data = nullptr,
        .input_size = 0,
        .value = {},
        .create2_salt = {},
        .code_address = emptyCallee,
        .code = nullptr,
        .code_size = 0,
        .destination_ptr = nullptr,
        .destination_len = 0,
        .sender_ptr = nullptr,
        .sender_len = 0};

    auto evmOut = syncWait([&parent, nested]() -> Task<EVMCResult> {
        co_return co_await parent.externalCall(nested);
    }());
    BOOST_CHECK_EQUAL(evmOut.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(parent.accessAccount(warmed), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_initial_warm_origin_consistency)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x11;
    evmc_address recipient{};
    recipient.bytes[19] = 0x22;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_double_prepare_idempotent)
{
    auto const features = pragueEip2929Features();

    evmc_address origin{};
    origin.bytes[19] = 0x12;
    evmc_address recipient{};
    recipient.bytes[19] = 0x34;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_await host.prepare();
    }());

    BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_initial_warm_to_consistency)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x33;
    evmc_address recipient{};
    recipient.bytes[19] = 0x44;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    BOOST_CHECK_EQUAL(host.accessAccount(recipient), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_initial_warm_precompile_consistency)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x55;
    evmc_address recipient{};
    recipient.bytes[19] = 0x66;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    for (int i = 1; i <= 9; ++i)
    {
        evmc_address pre{};
        pre.bytes[19] = static_cast<uint8_t>(i);
        BOOST_CHECK_EQUAL(host.accessAccount(pre), EVMC_ACCESS_WARM);
    }
}

BOOST_AUTO_TEST_CASE(TE_FC_A_initial_prewarm_prague_includes_0x0a_and_bls)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x55;
    evmc_address recipient{};
    recipient.bytes[19] = 0x66;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address pre0a{};
    pre0a.bytes[19] = 0x0a;
    evmc_address bls0b{};
    bls0b.bytes[19] = 0x0b;
    BOOST_CHECK_EQUAL(host.accessAccount(pre0a), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(bls0b), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_initial_prewarm_cancun_includes_0x0a_excludes_bls)
{
    auto host = makeHost(cancunEip2929Features());
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address pre0a{};
    pre0a.bytes[19] = 0x0a;
    evmc_address bls0b{};
    bls0b.bytes[19] = 0x0b;
    BOOST_CHECK_EQUAL(host.accessAccount(pre0a), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(bls0b), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_initial_prewarm_shanghai_te_revision_floor_warms_0x0a)
{
    // TE HostContext floors m_revision at EVMC_CANCUN (see HostContext.h). Even with
    // shanghaiEip2929Features() (no feature_evm_cancun), warmUpActivePrecompiles uses
    // m_revision >= CANCUN and pre-warms 0x0a. Executor path can still exclude 0x0a at
    // SHANGHAI — see CompatEip2929Test FC_A_initial_prewarm_shanghai_excludes_0x0a.
    auto host = makeHost(shanghaiEip2929Features());
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address pre0a{};
    pre0a.bytes[19] = 0x0a;
    BOOST_CHECK_EQUAL(host.accessAccount(pre0a), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_initial_prewarm_osaka_includes_p256verify)
{
    auto host = makeHost(osakaEip2929Features());
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address p256{};
    p256.bytes[18] = 0x01;
    p256.bytes[19] = 0x00;
    BOOST_CHECK_EQUAL(host.accessAccount(p256), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_initial_warm_create_skips_to)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x77;
    evmc_address recipient{};
    recipient.bytes[19] = 0x88;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CREATE);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address pre1{};
    pre1.bytes[19] = 0x01;
    BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(pre1), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(recipient), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(recipient), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_initial_warm_feature_off_prepare_noop)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    evmc_address origin{};
    origin.bytes[19] = 0x99;
    evmc_address recipient{};
    recipient.bytes[19] = 0xaa;
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address pre1{};
    pre1.bytes[19] = 0x01;
    BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(pre1), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2930_prepare_warms_account_and_storage)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x11;
    evmc_address recipient{};
    recipient.bytes[19] = 0x22;
    h256 const storageKey(0x42424242);
    auto accessList =
        std::make_shared<const bcos::executor::Eip2930AccessList>(bcos::executor::Eip2930AccessList{
            {"00000000000000000000000000000000c0ffee01", {storageKey}}});
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, accessList, 1);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address const listAddr = unhexAddress("00000000000000000000000000000000c0ffee01");
    evmc_bytes32 key{};
    static_assert(sizeof(key.bytes) == h256::SIZE);
    std::memcpy(key.bytes, storageKey.data(), h256::SIZE);
    BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(listAddr, key), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2930_eip1559_access_list_warms)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x33;
    evmc_address recipient{};
    recipient.bytes[19] = 0x44;
    h256 const storageKey(0x55);
    auto accessList =
        std::make_shared<const bcos::executor::Eip2930AccessList>(bcos::executor::Eip2930AccessList{
            {"00000000000000000000000000000000c0ffee02", {storageKey}}});
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, accessList, 2);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address const listAddr = unhexAddress("00000000000000000000000000000000c0ffee02");
    evmc_bytes32 key{};
    std::memcpy(key.bytes, storageKey.data(), h256::SIZE);
    BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(listAddr, key), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2930_empty_access_list_no_extra_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x55;
    evmc_address recipient{};
    recipient.bytes[19] = 0x66;
    auto emptyList = std::make_shared<const bcos::executor::Eip2930AccessList>();
    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, emptyList, 1);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address const extra{};
    BOOST_CHECK_EQUAL(host.accessAccount(extra), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2930_access_list_multi_slot)
{
    auto const features = pragueEip2929Features();
    evmc_address origin{};
    origin.bytes[19] = 0x11;
    evmc_address recipient{};
    recipient.bytes[19] = 0x22;
    evmc_address childContract{};
    childContract.bytes[19] = 0x73;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x74;

    h256 const key1(1);
    h256 const key2(2);
    h256 const key3(3);
    auto accessList = std::make_shared<const bcos::executor::Eip2930AccessList>(
        eip2929::makeAccessListSingleAccountMultiSlot(
            "00000000000000000000000000000000c0ffee04", {key1, key2, key3}));

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const code = eip2929::warmAccountThenRevertBytecode(coldTarget);
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, recipient, EVMC_CALL, accessList, 1);
        co_await host.prepare();

        evmc_address const listAddr = unhexAddress("00000000000000000000000000000000c0ffee04");
        auto toEvmcKey = [](h256 const& h) {
            evmc_bytes32 key{};
            std::memcpy(key.bytes, h.data(), h256::SIZE);
            return key;
        };

        BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, toEvmcKey(key1)), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, toEvmcKey(key2)), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, toEvmcKey(key3)), EVMC_ACCESS_WARM);

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_REVERT);

        // Access-list warmth is not journaled (W2): survives child REVERT.
        BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, toEvmcKey(key1)), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, toEvmcKey(key2)), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, toEvmcKey(key3)), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2930_access_list_multi_account)
{
    auto const features = pragueEip2929Features();
    evmc_address origin{};
    origin.bytes[19] = 0x33;
    evmc_address recipient{};
    recipient.bytes[19] = 0x44;

    h256 const key1(0x11);
    h256 const key2(0x22);
    auto accessList = std::make_shared<const bcos::executor::Eip2930AccessList>(
        eip2929::makeAccessListMultiAccount({
            {"00000000000000000000000000000000c0ffee05", {key1}},
            {"00000000000000000000000000000000c0ffee06", {key2}},
        }));

    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, accessList, 1);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address const listAddr1 = unhexAddress("00000000000000000000000000000000c0ffee05");
    evmc_address const listAddr2 = unhexAddress("00000000000000000000000000000000c0ffee06");
    evmc_bytes32 evmKey1{};
    std::memcpy(evmKey1.bytes, key1.data(), h256::SIZE);
    evmc_bytes32 evmKey2{};
    std::memcpy(evmKey2.bytes, key2.data(), h256::SIZE);

    BOOST_CHECK_EQUAL(host.accessAccount(listAddr1), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(listAddr1, evmKey1), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(listAddr2), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(listAddr2, evmKey2), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_revert_rolls_back_child_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x71;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x72;
    evmc_address childContract{};
    childContract.bytes[19] = 0x73;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x74;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const code = eip2929::warmAccountThenRevertBytecode(coldTarget);
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_address parentWarm{};
        parentWarm.bytes[19] = 0x75;
        BOOST_CHECK_EQUAL(host.accessAccount(parentWarm), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(parentWarm), EVMC_ACCESS_WARM);

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(parentWarm), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_success_commits_child_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x71;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x72;
    evmc_address childContract{};
    childContract.bytes[19] = 0x83;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x84;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const code = eip2929::warmAccountThenStopBytecode(coldTarget);
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_nested_inner_fail_outer_ok)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x70;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x71;
    evmc_address inner{};
    inner.bytes[19] = 0x80;
    evmc_address bAddr{};
    bAddr.bytes[19] = 0x81;
    evmc_address outer{};
    outer.bytes[19] = 0x82;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> innerAcc(
            rollbackableStorage, inner, false);
        co_await innerAcc.create();
        auto const innerCode = eip2929::warmAccountThenRevertBytecode(bAddr);
        auto const innerHash =
            hashImpl->hash(bcos::bytesConstRef(innerCode.data(), innerCode.size()));
        co_await innerAcc.setCode(innerCode, "", innerHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> outerAcc(
            rollbackableStorage, outer, false);
        co_await outerAcc.create();
        auto const outerCode = eip2929::callThenRevertBytecode(inner);
        auto const outerHash =
            hashImpl->hash(bcos::bytesConstRef(outerCode.data(), outerCode.size()));
        co_await outerAcc.setCode(outerCode, "", outerHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_address parentWarm{};
        parentWarm.bytes[19] = 0x83;
        BOOST_CHECK_EQUAL(host.accessAccount(parentWarm), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(parentWarm), EVMC_ACCESS_WARM);

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = outer,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = outer,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(bAddr), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(parentWarm), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_parent_call_nested_revert_rolls_back_child_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x68;
    evmc_address parentContract{};
    parentContract.bytes[19] = 0x69;
    evmc_address childContract{};
    childContract.bytes[19] = 0x6a;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x6b;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const childCode = eip2929::warmAccountThenRevertBytecode(coldTarget);
        auto const childHash =
            hashImpl->hash(bcos::bytesConstRef(childCode.data(), childCode.size()));
        co_await childAcc.setCode(childCode, "", childHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> parentAcc(
            rollbackableStorage, parentContract, false);
        co_await parentAcc.create();
        auto const parentCode = eip2929::callThenRevertBytecode(childContract);
        auto const parentHash =
            hashImpl->hash(bcos::bytesConstRef(parentCode.data(), parentCode.size()));
        co_await parentAcc.setCode(parentCode, "", parentHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentContract, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = parentContract;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_sequential_child_revert_then_success_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x50;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x51;
    evmc_address child1{};
    child1.bytes[19] = 0x52;
    evmc_address child2{};
    child2.bytes[19] = 0x53;
    evmc_address warmFromChild1{};
    warmFromChild1.bytes[19] = 0x54;
    evmc_address warmFromChild2{};
    warmFromChild2.bytes[19] = 0x55;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> child1Acc(
            rollbackableStorage, child1, false);
        co_await child1Acc.create();
        auto const child1Code = eip2929::warmAccountThenRevertBytecode(warmFromChild1);
        auto const child1Hash =
            hashImpl->hash(bcos::bytesConstRef(child1Code.data(), child1Code.size()));
        co_await child1Acc.setCode(child1Code, "", child1Hash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> child2Acc(
            rollbackableStorage, child2, false);
        co_await child2Acc.create();
        auto const child2Code = eip2929::warmAccountThenStopBytecode(warmFromChild2);
        auto const child2Hash =
            hashImpl->hash(bcos::bytesConstRef(child2Code.data(), child2Code.size()));
        co_await child2Acc.setCode(child2Code, "", child2Hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_message const callTemplate{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code = nullptr,
            .code_size = 0};

        auto msg1 = callTemplate;
        msg1.recipient = child1;
        msg1.code_address = child1;
        auto out1 = co_await host.externalCall(msg1);
        BOOST_REQUIRE_EQUAL(out1.status_code, EVMC_REVERT);
        BOOST_CHECK_EQUAL(host.accessAccount(warmFromChild1), EVMC_ACCESS_COLD);

        auto msg2 = callTemplate;
        msg2.recipient = child2;
        msg2.code_address = child2;
        auto out2 = co_await host.externalCall(msg2);
        BOOST_REQUIRE_EQUAL(out2.status_code, EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(host.accessAccount(warmFromChild2), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_child_revert_preserves_parent_warm_same_address)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x58;
    evmc_address parentContract{};
    parentContract.bytes[19] = 0x59;
    evmc_address childContract{};
    childContract.bytes[19] = 0x5a;
    evmc_address sharedAddr{};
    sharedAddr.bytes[19] = 0x5b;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const childCode = eip2929::warmAccountThenRevertBytecode(sharedAddr);
        auto const childHash =
            hashImpl->hash(bcos::bytesConstRef(childCode.data(), childCode.size()));
        co_await childAcc.setCode(childCode, "", childHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> parentAcc(
            rollbackableStorage, parentContract, false);
        co_await parentAcc.create();
        auto const parentCode = eip2929::warmAddressThenCallBytecode(sharedAddr, childContract);
        auto const parentHash =
            hashImpl->hash(bcos::bytesConstRef(parentCode.data(), parentCode.size()));
        co_await parentAcc.setCode(parentCode, "", parentHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentContract, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = parentContract;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(sharedAddr), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_staticcall_child_revert_rollback)
{
    auto const features = pragueEip2929Features();

    evmc_address origin{};
    origin.bytes[19] = 0x7c;
    evmc_address parentContract{};
    parentContract.bytes[19] = 0x7d;
    evmc_address innerContract{};
    innerContract.bytes[19] = 0x7e;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x7f;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> innerAcc(
            rollbackableStorage, innerContract, false);
        co_await innerAcc.create();
        auto const innerCode = eip2929::warmAccountThenRevertBytecode(coldTarget);
        auto const innerHash =
            hashImpl->hash(bcos::bytesConstRef(innerCode.data(), innerCode.size()));
        co_await innerAcc.setCode(innerCode, "", innerHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> parentAcc(
            rollbackableStorage, parentContract, false);
        co_await parentAcc.create();
        auto const parentCode = eip2929::staticCallThenRevertBytecode(innerContract);
        auto const parentHash =
            hashImpl->hash(bcos::bytesConstRef(parentCode.data(), parentCode.size()));
        co_await parentAcc.setCode(parentCode, "", parentHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentContract, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = parentContract;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_delegatecall_shares_warm_set)
{
    auto const features = pragueEip2929Features();

    evmc_address origin{};
    origin.bytes[19] = 0x8c;
    evmc_address parentContract{};
    parentContract.bytes[19] = 0x8d;
    evmc_address delegateCallee{};
    delegateCallee.bytes[19] = 0x8e;
    evmc_address warmAddr{};
    warmAddr.bytes[19] = 0x8f;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> calleeAcc(
            rollbackableStorage, delegateCallee, false);
        co_await calleeAcc.create();
        auto const calleeCode = eip2929::warmAccountThenStopBytecode(warmAddr);
        auto const calleeHash =
            hashImpl->hash(bcos::bytesConstRef(calleeCode.data(), calleeCode.size()));
        co_await calleeAcc.setCode(calleeCode, "", calleeHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> parentAcc(
            rollbackableStorage, parentContract, false);
        co_await parentAcc.create();
        auto const parentCode = eip2929::delegateCallThenStopBytecode(delegateCallee);
        auto const parentHash =
            hashImpl->hash(bcos::bytesConstRef(parentCode.data(), parentCode.size()));
        co_await parentAcc.setCode(parentCode, "", parentHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentContract, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = parentContract;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(warmAddr), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_nested_inner_ok_outer_fail)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x60;
    evmc_address inner{};
    inner.bytes[19] = 0x90;
    evmc_address xAddr{};
    xAddr.bytes[19] = 0x91;
    evmc_address runner{};
    runner.bytes[19] = 0x92;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> innerAcc(
            rollbackableStorage, inner, false);
        co_await innerAcc.create();
        auto const innerCode = eip2929::warmAccountThenStopBytecode(xAddr);
        auto const innerHash =
            hashImpl->hash(bcos::bytesConstRef(innerCode.data(), innerCode.size()));
        co_await innerAcc.setCode(innerCode, "", innerHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> runnerAcc(
            rollbackableStorage, runner, false);
        co_await runnerAcc.create();
        auto const runnerCode = eip2929::callThenRevertBytecode(inner);
        auto const runnerHash =
            hashImpl->hash(bcos::bytesConstRef(runnerCode.data(), runnerCode.size()));
        co_await runnerAcc.setCode(runnerCode, "", runnerHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, runner, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = runner;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(xAddr), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_oog_rolls_back_child_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x71;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x72;
    evmc_address childContract{};
    childContract.bytes[19] = 0x88;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x89;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        evmc_address coldTarget2{};
        coldTarget2.bytes[19] = 0x8a;
        auto const code = eip2929::warmTwoAccountsExtCodeSizeBytecode(coldTarget, coldTarget2);
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 500,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_OUT_OF_GAS);

        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget2), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_revert_preserves_tx_baseline)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x11;
    evmc_address recipient{};
    recipient.bytes[19] = 0x22;
    evmc_address childContract{};
    childContract.bytes[19] = 0x73;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x74;
    h256 const listStorageKey(0x29292929);
    auto accessList =
        std::make_shared<const bcos::executor::Eip2930AccessList>(bcos::executor::Eip2930AccessList{
            {"00000000000000000000000000000000c0ffee03", {listStorageKey}}});

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const code = eip2929::warmAccountThenRevertBytecode(coldTarget);
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, recipient, EVMC_CALL, accessList, 1);
        co_await host.prepare();

        evmc_address const listAddr = unhexAddress("00000000000000000000000000000000c0ffee03");
        evmc_bytes32 listSlot{};
        std::memcpy(listSlot.bytes, listStorageKey.data(), h256::SIZE);
        BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(recipient), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, listSlot), EVMC_ACCESS_WARM);

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(recipient), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(listAddr), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessStorage(listAddr, listSlot), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_revert_rolls_back_storage_slot)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x71;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x72;
    evmc_address childContract{};
    childContract.bytes[19] = 0x85;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto code = eip2929::storageWriterBytecode();
        code.pop_back();  // remove STOP
        code.push_back(0x60);
        code.push_back(0x00);
        code.push_back(0x60);
        code.push_back(0x00);
        code.push_back(0xfd);  // REVERT
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_bytes32 key{};
        std::memset(key.bytes, 0, sizeof(key.bytes));

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessStorage(childContract, key), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_create_fail_keeps_contract_warm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x61;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x62;

    syncWait([&]() -> task::Task<void> {
        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> senderAcc(
            rollbackableStorage, host.message().recipient, false);
        auto const nonceStr = co_await senderAcc.nonce();
        u256 const nonce(nonceStr.value_or(std::string("0")));

        auto const initCode = eip2929::revertInitcode();
        evmc_message nested{.kind = EVMC_CREATE,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = {},
            .sender = host.message().recipient,
            .input_data = initCode.data(),
            .input_size = initCode.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {},
            .code = nullptr,
            .code_size = 0};
        int64_t const childSeq = seq + 1;
        auto const resolved =
            getMessage(false, nested, blockHeader.number(), 0, childSeq, nonce, *hashImpl);
        evmc_address const createAddr = resolved.code_address;
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_NE(out.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(createAddr), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_create_fail_evmone_inner_warm_rolled_back)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address origin{};
    origin.bytes[19] = 0x63;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x64;
    evmc_address innerOnlyWarm{};
    innerOnlyWarm.bytes[19] = 0x65;

    syncWait([&]() -> task::Task<void> {
        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> senderAcc(
            rollbackableStorage, host.message().recipient, false);
        auto const nonceStr = co_await senderAcc.nonce();
        u256 const nonce(nonceStr.value_or(std::string("0")));

        auto const initCode = eip2929::revertInitcodeAfterWarmOtherBytecode(innerOnlyWarm);
        evmc_message nested{.kind = EVMC_CREATE,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = {},
            .sender = host.message().recipient,
            .input_data = initCode.data(),
            .input_size = initCode.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {},
            .code = nullptr,
            .code_size = 0};
        int64_t const childSeq = seq + 1;
        auto const resolved =
            getMessage(false, nested, blockHeader.number(), 0, childSeq, nonce, *hashImpl);
        evmc_address const createAddr = resolved.code_address;
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_NE(out.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(createAddr), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(innerOnlyWarm), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_create2_fail_keeps_contract_warm)
{
    auto const features = pragueEip2929Features();

    evmc_address origin{};
    origin.bytes[19] = 0x66;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x67;
    evmc_address other{};
    other.bytes[19] = 0x68;

    syncWait([&]() -> task::Task<void> {
        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> senderAcc(
            rollbackableStorage, host.message().recipient, false);
        auto const nonceStr = co_await senderAcc.nonce();
        u256 const nonce(nonceStr.value_or(std::string("0")));

        evmc_bytes32 salt{};
        salt.bytes[31] = 0x42;
        auto const initCode = eip2929::revertInitcodeAfterWarmOtherBytecode(other);
        auto nested = eip2929::makeCreate2Message(host.message().recipient, salt,
            bcos::bytesConstRef(initCode.data(), initCode.size()), 1'000'000);
        nested.depth = host.message().depth + 1;
        int64_t const childSeq = seq + 1;
        auto const resolved =
            getMessage(false, nested, blockHeader.number(), 0, childSeq, nonce, *hashImpl);
        evmc_address const create2Addr = resolved.code_address;
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_NE(out.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(create2Addr), EVMC_ACCESS_WARM);
        BOOST_CHECK_EQUAL(host.accessAccount(other), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_top_level_revert_rolls_back_runtime_warm)
{
    auto const features = pragueEip2929Features();

    evmc_address origin{};
    origin.bytes[19] = 0x69;
    evmc_address runner{};
    runner.bytes[19] = 0x6a;
    evmc_address runtimeTarget{};
    runtimeTarget.bytes[19] = 0x6b;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> runnerAcc(
            rollbackableStorage, runner, false);
        co_await runnerAcc.create();
        auto const runnerCode = eip2929::warmAccountThenRevertBytecode(runtimeTarget);
        auto const runnerHash =
            hashImpl->hash(bcos::bytesConstRef(runnerCode.data(), runnerCode.size()));
        co_await runnerAcc.setCode(runnerCode, "", runnerHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, runner, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = runner;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(runtimeTarget), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(origin), EVMC_ACCESS_WARM);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_nested_commit_then_parent_revert)
{
    auto const features = pragueEip2929Features();

    evmc_address origin{};
    origin.bytes[19] = 0x6c;
    evmc_address inner{};
    inner.bytes[19] = 0x6d;
    evmc_address xAddr{};
    xAddr.bytes[19] = 0x6e;
    evmc_address runner{};
    runner.bytes[19] = 0x6f;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAcc(
            rollbackableStorage, origin, false);
        if (!co_await originAcc.exists())
        {
            co_await originAcc.create();
        }
        co_await originAcc.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> innerAcc(
            rollbackableStorage, inner, false);
        co_await innerAcc.create();
        auto const innerCode = eip2929::warmAccountThenStopBytecode(xAddr);
        auto const innerHash =
            hashImpl->hash(bcos::bytesConstRef(innerCode.data(), innerCode.size()));
        co_await innerAcc.setCode(innerCode, "", innerHash);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> runnerAcc(
            rollbackableStorage, runner, false);
        co_await runnerAcc.create();
        auto const runnerCode = eip2929::callThenRevertBytecode(inner);
        auto const runnerHash =
            hashImpl->hash(bcos::bytesConstRef(runnerCode.data(), runnerCode.size()));
        co_await runnerAcc.setCode(runnerCode, "", runnerHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, runner, EVMC_CALL, {}, 0, 2'000'000);
        host.mutableMessage().code_address = runner;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_REVERT);

        BOOST_CHECK_EQUAL(host.accessAccount(xAddr), EVMC_ACCESS_COLD);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_A_eip2929_checkpoint_off_nested_call)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    evmc_address origin{};
    origin.bytes[19] = 0x71;
    evmc_address parentRecipient{};
    parentRecipient.bytes[19] = 0x72;
    evmc_address childContract{};
    childContract.bytes[19] = 0x86;
    evmc_address coldTarget{};
    coldTarget.bytes[19] = 0x87;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> childAcc(
            rollbackableStorage, childContract, false);
        co_await childAcc.create();
        auto const code = eip2929::warmAccountThenStopBytecode(coldTarget);
        auto const hash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
        co_await childAcc.setCode(code, "", hash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, parentRecipient, EVMC_CALL, {}, 0, 2'000'000);
        co_await host.prepare();

        evmc_message nested{.kind = EVMC_CALL,
            .flags = 0,
            .depth = host.message().depth + 1,
            .gas = 1'000'000,
            .recipient = childContract,
            .sender = host.message().recipient,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = childContract,
            .code = nullptr,
            .code_size = 0};
        auto out = co_await host.externalCall(nested);
        BOOST_REQUIRE_EQUAL(out.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
        BOOST_CHECK_EQUAL(host.accessAccount(coldTarget), EVMC_ACCESS_COLD);
    }());
}

#if 0  // Requires TE HostContext gas-settlement / EIP-7702 APIs not on release-3.18.0 base.
BOOST_AUTO_TEST_SUITE(EthTxGasSettlementHost)

BOOST_AUTO_TEST_CASE(Web3_stop_debitsIntrinsicOnce_notTransfer21000)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    evmc_address origin{};
    origin.bytes[19] = 0x81;
    evmc_address recipient{};
    recipient.bytes[19] = 0x82;

    constexpr int64_t startGas = 500'000;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        bcos::bytes const stopCode{0x00};
        auto const codeHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", codeHash);

        gas::TxGasSettlementContext settlement{};
        settlement.gasLimit = startGas;

        auto host = makeHost(features,
            static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION), origin, recipient,
            EVMC_CALL, {}, 0, startGas, {}, {}, {}, true, std::addressof(settlement));
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        BOOST_CHECK_EQUAL(settlement.gasBeforeEvm, startGas - gas::TX_BASE_GAS);
        BOOST_CHECK_EQUAL(result.gas_left, settlement.gasBeforeEvm);

        settlement.evmGasLeft = result.gas_left;
        settlement.evmGasRefund = result.gas_refund;
        settlement.fixedIntrinsic = gas::TX_BASE_GAS;
        BOOST_CHECK_EQUAL(gas::finalizeEthereumGasUsed(settlement), gas::TX_BASE_GAS);
    }());
}

BOOST_AUTO_TEST_CASE(Web3_mixedCalldata_finalizeUsesFloor)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    bytes mixed(100);
    for (int i = 0; i < 50; ++i)
    {
        mixed[i] = 0x00;
    }
    for (int i = 50; i < 100; ++i)
    {
        mixed[i] = 0x42;
    }
    auto const components = bcos::executor::calcEip7623Components(ref(mixed));

    evmc_address origin{};
    origin.bytes[19] = 0x91;
    evmc_address recipient{};
    recipient.bytes[19] = 0x92;

    constexpr int64_t startGas = 500'000;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        bcos::bytes const stopCode{0x00};
        auto const codeHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", codeHash);

        gas::TxGasSettlementContext settlement{};
        settlement.gasLimit = startGas;

        auto host = makeHost(features,
            static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION), origin, recipient,
            EVMC_CALL, {}, 0, startGas, {}, {}, {}, true, std::addressof(settlement));
        auto& msg = host.mutableMessage();
        msg.input_data = mixed.data();
        msg.input_size = mixed.size();
        msg.code_address = recipient;

        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        settlement.calldata = components;
        settlement.fixedIntrinsic = gas::TX_BASE_GAS;
        settlement.evmGasLeft = result.gas_left;
        settlement.evmGasRefund = result.gas_refund;

        BOOST_CHECK_EQUAL(
            gas::finalizeEthereumGasUsed(settlement), gas::TX_BASE_GAS + components.floorCost);
    }());
}

BOOST_AUTO_TEST_CASE(Web3_intrinsicOOG_beforeEvm_stillReportsIntrinsicGas)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::bugfix_v1_exec_error_gas_used);

    evmc_address origin{};
    origin.bytes[19] = 0x81;
    evmc_address recipient{};
    recipient.bytes[19] = 0x82;

    constexpr int64_t startGas = 20'000;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        bcos::bytes const stopCode{0x00};
        auto const codeHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", codeHash);

        gas::TxGasSettlementContext settlement{};
        settlement.gasLimit = startGas;

        auto host = makeHost(features,
            static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION), origin, recipient,
            EVMC_CALL, {}, 0, startGas, {}, {}, {}, true, std::addressof(settlement));
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_CHECK_EQUAL(result.status_code, EVMC_OUT_OF_GAS);
        BOOST_CHECK_EQUAL(result.gas_left, 0);
        BOOST_CHECK_EQUAL(settlement.gasBeforeEvm, 0);

        auto const& msg = host.message();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, nullptr, 0, nullptr);
        BOOST_CHECK_EQUAL(gas::finalizeEthereumGasUsedWithoutEvmStart(
                              settlement, intrinsic.preExecutionDebit(), result.gas_left, true),
            startGas);
    }());
}

BOOST_AUTO_TEST_CASE(Web3_revert_executionBurnIncludedInFinalize)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    evmc_address origin{};
    origin.bytes[19] = 0xc1;
    evmc_address recipient{};
    recipient.bytes[19] = 0xc2;

    constexpr int64_t startGas = 500'000;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        auto const& revertCode = bcos::test::eip7702::revertBytecode();
        auto const codeHash =
            hashImpl->hash(bcos::bytesConstRef(revertCode.data(), revertCode.size()));
        co_await recipientAccount.setCode(revertCode, "", codeHash);

        gas::TxGasSettlementContext settlement{};
        settlement.gasLimit = startGas;

        auto host = makeHost(features,
            static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION), origin, recipient,
            EVMC_CALL, {}, 0, startGas, {}, {}, {}, true, std::addressof(settlement));
        auto& msg = host.mutableMessage();
        msg.code_address = recipient;

        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_REVERT);
        BOOST_CHECK_GT(settlement.gasBeforeEvm, 0);
        BOOST_CHECK_LT(result.gas_left, settlement.gasBeforeEvm);

        settlement.calldata =
            executor::calcEip7623Components(bcos::bytesConstRef(msg.input_data, msg.input_size));
        settlement.fixedIntrinsic = gas::TX_BASE_GAS;
        settlement.evmGasLeft = result.gas_left;
        settlement.evmGasRefund = result.gas_refund;
        BOOST_CHECK_GT(gas::finalizeEthereumGasUsed(settlement), gas::TX_BASE_GAS);
    }());
}

BOOST_AUTO_TEST_CASE(Web3_valueTransfer_noDouble21000OnFinalize)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_balance);
    features.set(bcos::ledger::Features::Flag::feature_balance_policy1);

    evmc_address origin{};
    origin.bytes[19] = 0xd1;
    evmc_address recipient{};
    recipient.bytes[19] = 0xd2;

    constexpr int64_t startGas = 500'000;

    syncWait([&]() -> task::Task<void> {
        ledgerConfig.setFeatures(features);
        ledgerConfig.setBalanceTransfer(true);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        bcos::bytes const stopCode{0x00};
        auto const codeHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", codeHash);

        gas::TxGasSettlementContext settlement{};
        settlement.gasLimit = startGas;

        auto host = makeHost(features,
            static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION), origin, recipient,
            EVMC_CALL, {}, 0, startGas, {}, {}, {}, true, std::addressof(settlement));
        auto& msg = host.mutableMessage();
        msg.sender = origin;
        msg.code_address = recipient;
        msg.value = bcos::toEvmC(bcos::u256(1));

        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        settlement.fixedIntrinsic = gas::TX_BASE_GAS;
        settlement.evmGasLeft = result.gas_left;
        settlement.evmGasRefund = result.gas_refund;
        auto const gasUsed = gas::finalizeEthereumGasUsed(settlement);
        BOOST_CHECK_EQUAL(gasUsed, gas::TX_BASE_GAS);
        BOOST_CHECK_LT(gasUsed, 2 * gas::TX_BASE_GAS);
    }());
}

BOOST_AUTO_TEST_CASE(Legacy_nonWeb3_stillDebits7702IntrinsicBeforeEvm)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);

    auto authList = std::make_shared<bcos::executor::Eip7702AuthorizationList>();
    authList->resize(1);

    evmc_address origin{};
    origin.bytes[19] = 0xa1;
    evmc_address recipient{};
    recipient.bytes[19] = 0xa2;

    constexpr int64_t startGas = 500'000;
    int64_t const authCost = executor_v1::EIP_7702_PER_EMPTY_ACCOUNT_COST;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        bcos::bytes const stopCode{0x00};
        auto const codeHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", codeHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, recipient, EVMC_CALL, {}, 4, startGas, authList, {}, {}, false);
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
        BOOST_CHECK_GE(startGas - result.gas_left, authCost);
    }());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Eip7702)

BOOST_AUTO_TEST_CASE(IntrinsicGasAddedPerTuple)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    auto authList = std::make_shared<bcos::executor::Eip7702AuthorizationList>();
    authList->resize(2);

    evmc_address origin{};
    origin.bytes[19] = 0x71;
    evmc_address recipient{};
    recipient.bytes[19] = 0x72;

    constexpr int64_t startGas = 500'000;
    int64_t const intrinsic =
        static_cast<int64_t>(authList->size()) * executor_v1::EIP_7702_PER_EMPTY_ACCOUNT_COST;

    syncWait([&, authList]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        if (!co_await originAccount.exists())
        {
            co_await originAccount.create();
        }
        co_await originAccount.setBalance(bcos::u256(1) << 96);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, recipient, false);
        if (!co_await recipientAccount.exists())
        {
            co_await recipientAccount.create();
        }
        bcos::bytes const stopCode{0x00};
        auto const codeHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", codeHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, recipient, EVMC_CALL, {}, 4, startGas, authList);
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
        // Intrinsic 25000×N is debited before EVM run; additional gas may be spent on STOP + 2929.
        BOOST_CHECK_GE(startGas - result.gas_left, intrinsic);
    }());
}

BOOST_AUTO_TEST_CASE(RefundCapDeferred)
{
    BOOST_WARN_MESSAGE(
        true, "EIP-3529 refund cap is not enforced on transaction-executor in M1 (Q-Refund=B)");
}

BOOST_AUTO_TEST_CASE(WarmsAddresses)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address authority{};
    authority.bytes[19] = 0x81;
    evmc_address target{};
    target.bytes[19] = 0x82;
    evmc_address origin{};
    origin.bytes[19] = 0x83;
    evmc_address recipient{};
    recipient.bytes[19] = 0x84;

    auto warmAuthorities = std::make_shared<std::vector<evmc_address>>();
    warmAuthorities->push_back(authority);
    auto warmTargets = std::make_shared<std::vector<evmc_address>>();
    warmTargets->push_back(target);

    auto host = makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
        origin, recipient, EVMC_CALL, {}, 4, 1'000'000, {}, warmAuthorities, warmTargets);
    host.warmEip7702Addresses();
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    BOOST_CHECK_EQUAL(host.accessAccount(authority), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(target), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(CallFollowsIndicator)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address target{};
    target.bytes[19] = 0x91;
    evmc_address authority{};
    authority.bytes[19] = 0x92;
    evmc_address origin{};
    origin.bytes[19] = 0x93;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> targetAccount(rollbackableStorage, target, false);
        co_await targetAccount.create();
        auto const writer = eip7702::storageWriterBytecode();
        auto const writerHash = hashImpl->hash(bcos::bytesConstRef(writer.data(), writer.size()));
        co_await targetAccount.setCode(writer, "", writerHash);

        bcos::Address targetAddr;
        std::memcpy(targetAddr.data(), target.bytes, sizeof(target.bytes));
        co_await eip7702::setDelegationIndicator(
            rollbackableStorage, hashImpl, authority, targetAddr, false);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, authority, EVMC_CALL, {}, 4, 2'000'000);
        host.mutableMessage().code_address = authority;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> authorityAccount(
            rollbackableStorage, authority, false);
        evmc_bytes32 key{};
        auto const authoritySlot = co_await authorityAccount.storage(key);
        BOOST_CHECK_EQUAL(authoritySlot.bytes[31], 0x2a);

        auto const targetSlot = co_await targetAccount.storage(key);
        BOOST_CHECK_EQUAL(targetSlot.bytes[31], 0);
    }());
}

BOOST_AUTO_TEST_CASE(DelegatecallFollowsIndicator)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address target{};
    target.bytes[19] = 0xa1;
    evmc_address authority{};
    authority.bytes[19] = 0xa2;
    evmc_address origin{};
    origin.bytes[19] = 0xa3;

    syncWait([&]() -> task::Task<void> {
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> targetAccount(rollbackableStorage, target, false);
        co_await targetAccount.create();
        auto const writer = eip7702::storageWriterBytecode();
        auto const writerHash = hashImpl->hash(bcos::bytesConstRef(writer.data(), writer.size()));
        co_await targetAccount.setCode(writer, "", writerHash);

        bcos::Address targetAddr;
        std::memcpy(targetAddr.data(), target.bytes, sizeof(target.bytes));
        co_await eip7702::setDelegationIndicator(
            rollbackableStorage, hashImpl, authority, targetAddr, false);

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> originAccount(rollbackableStorage, origin, false);
        co_await originAccount.create();
        bcos::bytes const stopCode{0x00};
        auto const stopHash = hashImpl->hash(bcos::bytesConstRef(stopCode.data(), stopCode.size()));
        co_await originAccount.setCode(stopCode, "", stopHash);

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, origin, EVMC_DELEGATECALL, {}, 4, 2'000'000);
        host.mutableMessage().code_address = authority;
        co_await host.prepare();
        auto const result = co_await host.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        evmc_bytes32 key{};
        auto const originSlot = co_await originAccount.storage(key);
        BOOST_CHECK_EQUAL(originSlot.bytes[31], 0x2a);
    }());
}

BOOST_AUTO_TEST_CASE(ExtcodeOpsReturnIndicator)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);

    evmc_address delegated{};
    delegated.bytes[19] = 0xb1;
    evmc_address origin{};
    origin.bytes[19] = 0xb2;
    evmc_address recipient{};
    recipient.bytes[19] = 0xb3;

    syncWait([&]() -> task::Task<void> {
        bcos::Address targetAddr = bcos::Address("0xcccccccccccccccccccccccccccccccccccccccc");
        co_await eip7702::setDelegationIndicator(
            rollbackableStorage, hashImpl, delegated, targetAddr, false);

        auto const indicator = eip7702::makeDelegationIndicatorCode(targetAddr);
        auto const indicatorHash =
            hashImpl->hash(bcos::bytesConstRef(indicator.data(), indicator.size()));

        auto host =
            makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
                origin, recipient, EVMC_CALL, {}, 4, 1'000'000);
        co_await host.prepare();

        auto const size = co_await host.codeSizeAt(delegated);
        BOOST_CHECK_EQUAL(size, executor_v1::EIP_7702_DELEGATION_CODE_SIZE);

        auto const hash = co_await host.codeHashAt(delegated);
        BOOST_CHECK_EQUAL(hash, indicatorHash);

        auto const codeEntry = co_await host.code(delegated);
        BOOST_REQUIRE(codeEntry);
        BOOST_CHECK_EQUAL(codeEntry->get().size(), indicator.size());
    }());
}

BOOST_AUTO_TEST_SUITE_END()
#endif

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
