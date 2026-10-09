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
 * defensive paths (missing key, zero chainId, value overflow, a scheduled change on a
 * genesis-frozen key, an entry whose enableNumber is later than the evaluated block).
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
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), /*enableNumber=*/0);
    // The one runtime-writable key carries a past enableNumber (10) while the caller
    // block is 42: an entry the contract wrote earlier is simply active.
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), /*enableNumber=*/10);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
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
    // gas_limit is genesis-frozen: the tuple's block is the slot's enableNumber (0), not the
    // caller's block (42).
    BOOST_CHECK_EQUAL(gasLimitBlock, 0);
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 1000U);
    BOOST_CHECK_EQUAL(out.compatibilityVersion(), 0x03'10'00'00U);
}

// The slot holds one entry per key, so an entry whose enableNumber is still later than the
// block being evaluated has displaced the active value with nothing to fall back to. The
// contract cannot produce one (setValueByKey enforces enableNumber <= block.number + 1 and
// the loader is evaluated at committed + 1), so it is a raw storage write or an alloc edit:
// the loader must throw, not skip -- skipping would seal with the caller's SYS_CONFIG
// fallback until the height arrives, and leave @p out silently unchanged.
BOOST_AUTO_TEST_CASE(FutureScheduledWritableKeyThrows)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    // block_tx_count_limit is scheduled for block 200, but we load at block 50.
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), /*enableNumber=*/200);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
    LedgerConfig out;
    out.setBlockTxCountLimit(7);

    BOOST_CHECK_EXCEPTION(task::syncWait(loader.loadIntoLedgerConfig(/*blockNumber=*/50, out)),
        std::runtime_error, [](std::runtime_error const& error) {  // 50 < 200
            return std::string(error.what())
                       .find("'block_tx_count_limit' is scheduled for block 200") !=
                   std::string::npos;
        });
    // The scheduled value was not applied early either.
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 7U);
}

// Boundary: the writable key activates on the exact block equal to its enableNumber -- the
// shape setValueByKey(…, block.number + 1) in block N produces when the loader evaluates
// N + 1. The frozen keys carry 0.
BOOST_AUTO_TEST_CASE(ScheduledKeyAppliesAtExactEnableBlock)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 100);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
    LedgerConfig out;
    out.setBlockTxCountLimit(7);

    task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(/*blockNumber=*/100, out);  // 100 >= 100
        co_return;
    }());

    auto [gasLimit, gasLimitBlock] = out.gasLimit();
    BOOST_CHECK_EQUAL(gasLimit, 30'000'000U);
    BOOST_CHECK_EQUAL(gasLimitBlock, 0);
    BOOST_REQUIRE(out.chainId().has_value());
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 1000U);
    BOOST_CHECK_EQUAL(out.compatibilityVersion(), 0x03'10'00'00U);
}

// gas_limit and compatibility_version are genesis-frozen like chain_id: the contract
// rejects runtime writes, so a non-zero enableNumber is a slot written past the contract.
// The loader throws whether the schedule is already past (block 100 >= 7) or still ahead
// (block 50 < 200) -- the second case is what would otherwise let a crafted alloc pass the
// boot comparison (checkL2GenesisFrozenKeys) on the caller's fallback value and overlay the
// frozen key once the height arrived.
BOOST_AUTO_TEST_CASE(FrozenKeyScheduledChangeThrows)
{
    for (std::string_view frozenKey : {"gas_limit", "compatibility_version"})
    {
        for (auto [enableNumber, blockNumber] :
            {std::pair<uint64_t, protocol::BlockNumber>{7, 100}, {200, 50}})
        {
            FakeSlotStorage storage;
            putSlot(storage, "chain_id", chainIdLow192(901), 0);
            putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000),
                frozenKey == "gas_limit" ? enableNumber : 0);
            putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000), 0);
            putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00),
                frozenKey == "compatibility_version" ? enableNumber : 0);

            L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
            LedgerConfig out;
            // Pin the frozen-key gate, not the schedule gate: the {200, 50} case would throw
            // there too, so the message is what proves the frozen check fired first.
            BOOST_CHECK_EXCEPTION(task::syncWait(loader.loadIntoLedgerConfig(blockNumber, out)),
                std::runtime_error, [frozenKey](std::runtime_error const& error) {
                    return std::string(error.what())
                               .find(fmt::format("'{}' is genesis-frozen", frozenKey)) !=
                           std::string::npos;
                });
        }
    }
}

// An enableNumber at or above 2^63 must not slip past the schedule gate through a signed
// conversion (BlockNumber is int64): the comparison is std::cmp_less, so it is rejected
// like any other later height.
BOOST_AUTO_TEST_CASE(HugeEnableNumberIsStillRejected)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(1000),
        /*enableNumber=*/(uint64_t{1} << 63) + 5);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
    LedgerConfig out;
    out.setBlockTxCountLimit(7);
    BOOST_CHECK_EXCEPTION(task::syncWait(loader.loadIntoLedgerConfig(/*blockNumber=*/100, out)),
        std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("'block_tx_count_limit' is scheduled for") !=
                   std::string::npos;
        });
    BOOST_CHECK_EQUAL(out.blockTxCountLimit(), 7U);
}

BOOST_AUTO_TEST_CASE(MissingKeyThrows)
{
    FakeSlotStorage storage;
    // Only 3 of the 4 required keys written: omit block_tx_count_limit.
    putSlot(storage, "chain_id", chainIdLow192(901), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03'10'00'00), 0);

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
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

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
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

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
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

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
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

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
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

    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
    LedgerConfig out;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        co_await loader.loadIntoLedgerConfig(0, out);
        co_return;
    }()),
        std::runtime_error);
}

/// block_tx_count_limit feeds the sealer as an int64 count: 0 would seal empty blocks and a
/// value above INT64_MAX would wrap. Both are refused by the loader, so the seal-side fallback
/// to the constructor default never has to paper over an invalid on-chain value.
BOOST_AUTO_TEST_CASE(BlockTxCountLimitZeroThrows)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(20200), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit", packUint64IntoLow192(0), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03120000), 0);
    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
    LedgerConfig config;
    BOOST_CHECK_EXCEPTION(task::syncWait(loader.loadIntoLedgerConfig(1, config)),
        std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("block_tx_count_limit must be in") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(BlockTxCountLimitAboveInt64MaxThrows)
{
    FakeSlotStorage storage;
    putSlot(storage, "chain_id", chainIdLow192(20200), 0);
    putSlot(storage, "gas_limit", packUint64IntoLow192(30'000'000), 0);
    putSlot(storage, "block_tx_count_limit",
        packUint64IntoLow192(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1), 0);
    putSlot(storage, "compatibility_version", packUint64IntoLow192(0x03120000), 0);
    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, systemConfigTable());
    LedgerConfig config;
    BOOST_CHECK_EXCEPTION(task::syncWait(loader.loadIntoLedgerConfig(1, config)),
        std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("block_tx_count_limit must be in") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
