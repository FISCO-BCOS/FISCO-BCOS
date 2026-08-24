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
 * @file test_L2ConfigLoader.cpp
 * @brief L2ConfigLoaderImpl direct slot read + decode (A6.7).
 *
 * The loader reads SystemConfig._config slots directly (no EVM staticcall),
 * decodes the packed (value:uint192, enableNumber:uint64) word, and projects
 * each known key onto a LedgerConfig setter. These tests drive the loader
 * against a small in-memory storage that satisfies the readSome concept and
 * verify both the happy path (4 keys land in the right setters) and the
 * defensive paths (missing key, zero chainId, value overflow).
 */
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/L2ConfigLoader.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::ledger;
using bcos::executor_v1::StateKey;

namespace
{
// Minimal in-memory storage satisfying the loader's readSome concept.
// We index by StateKey (the loader's key type) so the keys produced by
// L2ConfigLoaderImpl match these exactly, including the table-name format.
struct FakeSlotStorage
{
    std::map<StateKey, bcos::storage::Entry> data;

    task::Task<std::vector<std::optional<bcos::storage::Entry>>> readSome(
        std::vector<StateKey> keys)
    {
        std::vector<std::optional<bcos::storage::Entry>> result;
        result.reserve(keys.size());
        for (auto const& key : keys)
        {
            auto it = data.find(key);
            if (it != data.end())
            {
                result.emplace_back(it->second);
            }
            else
            {
                result.emplace_back(std::nullopt);
            }
        }
        co_return result;
    }
};

// table name used by the loader for the SystemConfig predeploy.
std::string systemConfigTable()
{
    return fmt::format(
        "{}{}", bcos::ledger::SYS_DIRECTORY::USER_APPS, L2_SYSTEM_CONFIG_ADDRESS_HEX);
}

// Write one Entry slot for `key` whose value half (uint192) is `low192` and
// whose enableNumber is `enableNumber`. `low192` is supplied as 24 raw
// big-endian bytes so callers can construct exactly the on-chain layout
// (including the leading-zero invariants the loader's decoder validates).
void putSlot(FakeSlotStorage& storage, std::string_view configKey,
    std::array<uint8_t, 24> const& low192, uint64_t enableNumber)
{
    auto slot = l2_loader_detail::mappingStringSlot(configKey, L2_SYSTEM_CONFIG_BASE_SLOT);

    // 32-byte packed slot value: [enableNumber:uint64 BE | value:uint192 BE].
    std::array<uint8_t, 32> packed{};
    for (int shift = 56, i = 0; shift >= 0; shift -= 8, ++i)
    {
        packed[i] = static_cast<uint8_t>((enableNumber >> shift) & 0xFFU);
    }
    std::copy(low192.begin(), low192.end(), packed.begin() + 8);

    bcos::storage::Entry entry;
    entry.set(std::string(reinterpret_cast<char const*>(packed.data()), packed.size()));

    StateKey stateKey(systemConfigTable(),
        std::string_view(reinterpret_cast<char const*>(slot.data()), slot.size()));
    storage.data.emplace(std::move(stateKey), std::move(entry));
}

// Convenience: pack a small unsigned integer (uint64 / uint32) into the low
// 192 bits, leaving the upper bytes zero — what a well-formed on-chain config
// would look like for any sub-192-bit value.
std::array<uint8_t, 24> packUint64IntoLow192(uint64_t value)
{
    std::array<uint8_t, 24> result{};
    for (int shift = 56, i = 0; shift >= 0; shift -= 8, ++i)
    {
        result[16 + i] = static_cast<uint8_t>((value >> shift) & 0xFFU);
    }
    return result;
}

// Convenience: encode chainId as low 192 bits (any chainId we use in tests
// fits in uint64, so the high 16 bytes are zero like a real contract value).
std::array<uint8_t, 24> chainIdLow192(uint64_t chainId)
{
    return packUint64IntoLow192(chainId);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(L2ConfigLoaderTest)

BOOST_AUTO_TEST_CASE(SlotAddressingMatchesKeccakFormula)
{
    // Cross-check the in-loader slot formula against a direct keccak256 of
    // (utf8(key) || be32(101)). If this drifts the contract and the loader
    // address different slots — a silent consensus bug.
    constexpr std::string_view key = "chain_id";
    auto loaderSlot = l2_loader_detail::mappingStringSlot(key, L2_SYSTEM_CONFIG_BASE_SLOT);

    bcos::bytes expected;
    expected.insert(expected.end(), key.begin(), key.end());
    for (size_t i = 0; i < 24; ++i)
    {
        expected.push_back(0);  // high 24 bytes of be32(baseSlot) are zero
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        expected.push_back(static_cast<uint8_t>((L2_SYSTEM_CONFIG_BASE_SLOT >> shift) & 0xFFU));
    }
    auto referenceSlot =
        crypto::keccak256Hash(bcos::bytesConstRef(expected.data(), expected.size()));
    BOOST_CHECK_EQUAL(loaderSlot.hex(), referenceSlot.hex());
}

BOOST_AUTO_TEST_CASE(HappyPathPopulatesLedgerConfig)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), /*enableNumber=*/0);
    // gas_limit carries a non-zero enableNumber (10) while the caller block is
    // 42: the loader must record 10 (the slot's enable block), not 42 (caller).
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), /*enableNumber=*/10);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;

    task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(/*blockNumber=*/42, out);
        co_return;
    }());

    // chainId: only low 2 bytes (901 = 0x0385) non-zero, rest zero.
    BOOST_REQUIRE(out.chainId().has_value());
    auto const& chainIdBytes = out.chainId()->bytes;
    for (size_t i = 0; i < 30; ++i)
    {
        BOOST_CHECK_EQUAL(chainIdBytes[i], 0);
    }
    BOOST_CHECK_EQUAL(chainIdBytes[30], 0x03);
    BOOST_CHECK_EQUAL(chainIdBytes[31], 0x85);

    auto [gasLimit, gasLimitBlock] = out.gasLimit();
    BOOST_CHECK_EQUAL(gasLimit, 30'000'000U);
    // enableNumber from the slot, NOT the caller's block (42).
    BOOST_CHECK_EQUAL(gasLimitBlock, 10);
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 1000U);
    BOOST_CHECK_EQUAL(out.compatibilityVersion(), 0x03'10'00'00U);
}

// A key whose enableNumber is still in the future must be skipped: the loader
// leaves LedgerConfig's prior value untouched rather than applying the
// scheduled change early. Mirrors LedgerTypeDef::readFromStorage semantics.
BOOST_AUTO_TEST_CASE(ScheduledKeySkippedWhenFutureEnable)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    // gas_limit is scheduled to enable at block 200, but we load at block 50.
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), /*enableNumber=*/200);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    // Pre-seed a cached gas limit the loader must NOT overwrite this block.
    out.setGasLimit({99'999'999, 0});

    task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(/*blockNumber=*/50, out);  // 50 < 200
        co_return;
    }());

    // gas_limit was future-enabled -> untouched, keeps the pre-seeded value.
    auto [gasLimit, gasLimitBlock] = out.gasLimit();
    BOOST_CHECK_EQUAL(gasLimit, 99'999'999U);
    BOOST_CHECK_EQUAL(gasLimitBlock, 0);

    // The other three keys (enableNumber 0) are active and applied normally.
    BOOST_REQUIRE(out.chainId().has_value());
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 1000U);
    BOOST_CHECK_EQUAL(out.compatibilityVersion(), 0x03'10'00'00U);
}

// Boundary: a key activates on the exact block equal to its enableNumber. The
// gate is `blockNumber >= enableNumber`, so block == enableNumber applies.
BOOST_AUTO_TEST_CASE(ScheduledKeyAppliesAtExactEnableBlock)
{
    FakeSlotStorage storage;
    // chain_id is genesis-frozen: its enableNumber must stay 0 (see
    // ChainIdScheduledChangeThrows); the schedulable keys carry 100.
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 100);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 100);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 100);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;

    task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(/*blockNumber=*/100, out);  // 100 >= 100
        co_return;
    }());

    auto [gasLimit, gasLimitBlock] = out.gasLimit();
    BOOST_CHECK_EQUAL(gasLimit, 30'000'000U);
    BOOST_CHECK_EQUAL(gasLimitBlock, 100);
    BOOST_REQUIRE(out.chainId().has_value());
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 1000U);
    BOOST_CHECK_EQUAL(out.compatibilityVersion(), 0x03'10'00'00U);
}

BOOST_AUTO_TEST_CASE(MissingKeyThrows)
{
    FakeSlotStorage storage;
    // Only 3 of the 4 required keys written: omit block_tx_count_limit.
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(0, out);
        co_return;
    }()),
        std::runtime_error);
}

// D4 authority boundary: chain_id is genesis-frozen. Genesis writes it with
// enableNumber 0 and SystemConfig.setValueByKey rejects the key, so a slot
// carrying a non-zero enableNumber means the chain-identity invariant was
// bypassed — the loader must abort the block instead of re-keying the chain.
BOOST_AUTO_TEST_CASE(ChainIdScheduledChangeThrows)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), /*enableNumber=*/7);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    // Throws even when the caller's block (100) is past the enableNumber —
    // a scheduled chain_id change is invalid regardless of schedule state.
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(100, out);
        co_return;
    }()),
        std::runtime_error);
}

// Same invariant, other side of the schedule gate: a chain_id entry whose
// enableNumber is still in the FUTURE must also throw. The old loader would
// have silently skipped it (schedule gate) and re-keyed the chain when the
// block height caught up — "regardless of schedule state" means exactly that
// this case fails loudly too.
BOOST_AUTO_TEST_CASE(ChainIdFutureScheduledChangeAlsoThrows)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), /*enableNumber=*/200);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(/*blockNumber=*/50, out);  // 50 < 200
        co_return;
    }()),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(ChainIdZeroThrowsEip155)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(0), 0);  // forbidden: chainId == 0
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(0, out);
        co_return;
    }()),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(GasLimitExceedsUint64Throws)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    // Force a nonzero byte in the high 16 bytes of the 24-byte value -> the
    // loader's uint64 projection must reject this rather than silently
    // truncating a consensus parameter.
    std::array<uint8_t, 24> oversizedGasLimit = packUint64IntoLow192(30'000'000);
    oversizedGasLimit[8] = 0x01;  // bit set above the uint64 window
    putSlot(storage, "gas_limit", oversizedGasLimit, 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(0, out);
        co_return;
    }()),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(CompatibilityVersionExceedsUint32Throws)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
    // Pack a value that does not fit in uint32 -> loader rejects rather than
    // truncating to a wrong on-chain compatibility version.
    auto oversizedVersion = packUint64IntoLow192(uint64_t{1} << 33);
    putSlot(storage, "compatibility_version", oversizedVersion, 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage);
    LedgerConfig out;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(0, out);
        co_return;
    }()),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
