/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE-FC-*: transaction-executor forward-compatibility (revision, 2929).
 *  @file CompatHostContextTest.cpp
 */

#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/vm/Eip2929AccessState.h"
#include "bcos-executor/src/vm/VMInstance.h"
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
        uint8_t web3TypedTxKindForAccessList = 0)
    {
        ledgerConfig.setFeatures(features);
        blockHeader.setVersion(blockHeaderVersion);
        blockHeader.calculateHash(*hashImpl);
        evmc_message message = {.kind = kindIn,
            .flags = 0,
            .depth = 0,
            .gas = 1'000'000,
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
            std::move(eip2930AccessList), web3TypedTxKindForAccessList);
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
    BOOST_CHECK_EQUAL(revision, EVMC_SHANGHAI);
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

BOOST_AUTO_TEST_CASE(TE_FC_revision_gate_eip2929_on_below_berlin_always_cold)
{
    // toRevision(...) for supported chains is >= London (> Berlin); the rev gate is exercised via
    // explicit evmc_revision the same way bcos-executor HostContext(access, rev) does.
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

    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_ISTANBUL), EVMC_ACCESS_COLD);

    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_WARM);
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

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
