/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE-FC-*: transaction-executor forward-compatibility (revision mapping).
 *  @file CompatHostContextTest.cpp
 */

#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestMemoryStorage.h"
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
#include <limits>
#include <memory>
#include <optional>

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
        bcos::executor_v1::clearGlobalExecutableCache();
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
        evmc_call_kind kindIn = EVMC_CALL, int64_t gas = 1'000'000, bytes const* input = nullptr)
    {
        ledgerConfig.setFeatures(features);
        blockHeader.setVersion(blockHeaderVersion);
        blockHeader.calculateHash(*hashImpl);
        evmc_message message = {.kind = kindIn,
            .flags = 0,
            .depth = 0,
            .gas = gas,
            .recipient = recipientIn,
            .sender = {},
            .input_data = input == nullptr ? nullptr : input->data(),
            .input_size = input == nullptr ? 0 : input->size(),
            .value = {},
            .state_gas = {},  // evmc ABI 19 (EIP-8037); unused below Amsterdam
            .code_address = recipientIn,
            .code = nullptr,
            .code_size = 0};
        return HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>(
            rollbackableStorage, rollbackableTransientStorage, blockHeader, message, originIn, "",
            0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0, bcos::task::syncWait);
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
}

BOOST_AUTO_TEST_CASE(TE_FC_access_always_cold)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    auto host = makeHost(features);
    auto const* hostInterface = host.hostInterface();
    evmc_address address{};
    evmc_bytes32 key{};

    BOOST_REQUIRE(hostInterface != nullptr);
    BOOST_CHECK_EQUAL(hostInterface->access_account(host.hostCtx(), &address), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(hostInterface->access_account(host.hostCtx(), &address), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(
        hostInterface->access_storage(host.hostCtx(), &address, &key), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(
        hostInterface->access_storage(host.hostCtx(), &address, &key), EVMC_ACCESS_COLD);
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

// EIP-7623 calldata floor (this upgrade's PR#5740-era tests; pure function checks).
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

BOOST_AUTO_TEST_SUITE_END()  // CompatHostContext

}  // namespace bcos::test
