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
 * @file L2ConfigLoader.h
 * @brief Per-block loader that refreshes LedgerConfig from the L2 SystemConfig
 *        predeploy by reading its storage slots DIRECTLY (no EVM staticcall).
 *
 *        See `docs/superpowers/plans/op-stack-l2/A6-precompile-predeploys/
 *        2026-06-17-systemconfig-slot-kv-redesign.md` for the slot-KV contract
 *        the Solidity side and this loader share.
 *
 *        SystemConfig.sol stores configs in a single
 *        `mapping(string => Entry{uint192 value, uint64 enableNumber}) _config;`
 *        declared at storage slot 101 (OZ v4.7.3 layout pinned by PR-7
 *        storage-layout fixture gate). For each key the entry occupies one
 *        32-byte slot whose address is:
 *
 *            entrySlot(key) = keccak256( utf8(key) || be32(baseSlot=101) )
 *
 *        and the slot value is the packed `(value:uint192 || enableNumber:uint64)`
 *        big-endian word — enableNumber occupies the high 8 bytes, value the
 *        low 24 bytes.
 *
 *        Solidity is the single authoritative source for the L2 chain config.
 *        A missing key (entry never written) or a malformed slot value aborts
 *        the current block via throw; the loader never falls back to a cached
 *        config. A key whose packed enableNumber is still in the future is
 *        skipped (the block keeps its prior value), matching
 *        LedgerTypeDef::readFromStorage's `blockNumber >= enableNumber`
 *        schedule semantics so every node activates a change on the same block.
 */
#pragma once
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/IL2ConfigLoader.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <fmt/format.h>
#include <boost/throw_exception.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ledger
{
// Predeploy address of SystemConfig.sol: 0x42000000000000000000000000000000000000C0.
// The table name follows USER_APPS convention so it lives next to user contract
// tables in the state storage.
inline constexpr std::string_view L2_SYSTEM_CONFIG_ADDRESS_HEX =
    "42000000000000000000000000000000000000c0";

// Storage slot where SystemConfig._config is declared. With OZ v4.7.3
// Initializable + ContextUpgradeable + Ownable bases the mapping lands at
// slot 101 (Initializable[0] + Context.__gap[1..50] + _owner[51] +
// Ownable.__gap[52..100]). The PR-7 storage-layout fixture gate locks this
// value: any OZ bump or any new state variable declared before _config will
// fail that gate before this constant drifts silently.
inline constexpr uint64_t L2_SYSTEM_CONFIG_BASE_SLOT = 101;

// Width of one EVM storage slot.
inline constexpr size_t L2_SLOT_BYTES = 32;

// Config keys read from the SystemConfig predeploy. The set mirrors the
// LedgerConfig fields that downstream consumers already understand (Scheduler /
// Executor / RPC); growing it is zero-ABI on the contract side (just write a
// new key) but it does require a matching LedgerConfig setter, which is why
// l2_block_time is intentionally NOT in this set yet (LedgerConfig has no
// l2BlockTime field; spec follow-up).
inline constexpr std::array<std::string_view, 4> L2_SYSTEM_CONFIG_KEYS = {
    "chain_id", "gas_limit", "block_tx_count_limit", "compatibility_version"};

namespace l2_loader_detail
{
// Compute entrySlot(key) = keccak256( utf8(key) || be32(baseSlot) ).
//
// This is the standard Solidity formula for `mapping(string => V)` value
// addressing: the string key's `h(k)` is the raw bytes (no padding) and `p` is
// the mapping's declaration slot as a 32-byte big-endian word.
//
// keccak256 is fixed regardless of the node's hashImpl. EVM opcodes always
// produce keccak256 storage layouts, so SM-crypto nodes must still derive slot
// addresses with keccak256 to stay byte-compatible with the contract.
inline bcos::h256 mappingStringSlot(std::string_view key, uint64_t baseSlot)
{
    bcos::bytes buffer;
    buffer.reserve(key.size() + L2_SLOT_BYTES);
    buffer.insert(buffer.end(), key.begin(), key.end());
    // 32-byte big-endian baseSlot: 24 zero pad bytes, then 8 BE bytes.
    for (size_t i = 0; i < L2_SLOT_BYTES - sizeof(uint64_t); ++i)
    {
        buffer.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        buffer.push_back(static_cast<uint8_t>((baseSlot >> shift) & 0xFFU));
    }
    return crypto::keccak256Hash(bcos::bytesConstRef(buffer.data(), buffer.size()));
}

// One decoded SystemConfig entry: the uint192 value half plus the uint64
// enableNumber the contract packed alongside it. The contract declares
// `Entry{uint192 value, uint64 enableNumber}`, so both halves carry meaning —
// enableNumber is the block at which a scheduled config change takes effect.
struct DecodedEntry
{
    std::array<uint8_t, 24> value;  // uint192 config value, big-endian
    uint64_t enableNumber;          // block this entry becomes active
};

// Decode the packed Entry slot into (value, enableNumber).
// Layout (big-endian 32-byte word):
//   [enableNumber:uint64 high 8 bytes | value:uint192 low 24 bytes]
//
// The value is returned as a 24-byte buffer; callers project it down to the
// actual config width (uint64 / uint32 / uint256) by reading the trailing N
// bytes and asserting the leading bytes are zero — matching the contract's
// typed accessors (value is declared uint192 on-chain so the upper bytes of
// any uint64 / uint32 config are required to be zero). enableNumber gates
// whether the value is applied this block (see loadIntoLedgerConfig).
inline DecodedEntry decodeEntryValue(std::string_view slotBytes)
{
    if (slotBytes.size() != L2_SLOT_BYTES)
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error(fmt::format("L2ConfigLoader: slot value must be {} bytes, got {}",
                L2_SLOT_BYTES, slotBytes.size())));
    }
    DecodedEntry decoded{};
    // enableNumber occupies bytes [0..8) of the big-endian word.
    for (size_t i = 0; i < sizeof(uint64_t); ++i)
    {
        decoded.enableNumber = (decoded.enableNumber << 8) | static_cast<uint8_t>(slotBytes[i]);
    }
    // value occupies bytes [8..32) of the big-endian word.
    std::copy_n(reinterpret_cast<uint8_t const*>(slotBytes.data() + 8), 24, decoded.value.begin());
    return decoded;
}

// Project a 24-byte value down to uint64 (low 8 bytes BE). Throws if the
// high 16 bytes are non-zero — that means the on-chain `uint192` value does
// not fit in a uint64 and a consensus parameter is being misencoded.
inline uint64_t valueToUint64(std::array<uint8_t, 24> const& value, std::string_view keyName)
{
    for (size_t i = 0; i < 16; ++i)
    {
        if (value[i] != 0)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(fmt::format(
                "L2ConfigLoader: '{}' value exceeds uint64 (nonzero high byte at offset {})",
                keyName, i)));
        }
    }
    uint64_t result = 0;
    for (size_t i = 16; i < 24; ++i)
    {
        result = (result << 8) | value[i];
    }
    return result;
}

// Project a 24-byte value down to uint32 (low 4 bytes BE). Same upper-byte
// check as valueToUint64.
inline uint32_t valueToUint32(std::array<uint8_t, 24> const& value, std::string_view keyName)
{
    for (size_t i = 0; i < 20; ++i)
    {
        if (value[i] != 0)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(fmt::format(
                "L2ConfigLoader: '{}' value exceeds uint32 (nonzero high byte at offset {})",
                keyName, i)));
        }
    }
    uint32_t result = 0;
    for (size_t i = 20; i < 24; ++i)
    {
        result = (result << 8) | value[i];
    }
    return result;
}

// Project a 24-byte value into a 32-byte big-endian evmc_uint256be (left-pad
// with 8 zero bytes). Used for chain_id which downstream consumers store as
// uint256.
inline evmc_uint256be valueToUint256BE(std::array<uint8_t, 24> const& value)
{
    evmc_uint256be result{};
    // Left-pad: result bytes [0..8) stay zero, [8..32) get the 24 value bytes.
    std::copy(value.begin(), value.end(), result.bytes + 8);
    return result;
}
}  // namespace l2_loader_detail

/// Concept: a storage type compatible with `storage2::readSome` keyed by
/// `executor_v1::StateKey`, returning `std::optional<bcos::storage::Entry>`
/// (the default FISCO-BCOS state-storage shape). The caller owns the storage;
/// L2ConfigLoaderImpl holds a non-owning pointer.
template <typename Storage>
class L2ConfigLoaderImpl : public ledger::IL2ConfigLoader
{
public:
    explicit L2ConfigLoaderImpl(Storage& storage) : m_storage(&storage) { assert(m_storage); }

    /// Refresh @p out by reading 4 slots from the SystemConfig predeploy.
    /// Precondition: @p out must already carry any non-L2 fields (consensus
    /// nodes, etc.); this loader only mutates the slot-backed ones.
    task::Task<void> loadIntoLedgerConfig(
        protocol::BlockNumber blockNumber, ledger::LedgerConfig& out) override
    {
        using executor_v1::StateKey;
        namespace detail = l2_loader_detail;

        auto const tableName = fmt::format(
            "{}{}", bcos::ledger::SYS_DIRECTORY::USER_APPS, L2_SYSTEM_CONFIG_ADDRESS_HEX);

        // Compute the 4 slot addresses once. Slot hashes are content-addressed
        // and reusable across blocks, but precomputing them per call keeps the
        // loader stateless (and the cost — 4 keccak256 over <50 bytes each —
        // is negligible compared to one storage read).
        std::array<bcos::h256, L2_SYSTEM_CONFIG_KEYS.size()> slots;
        std::vector<StateKey> keys;
        keys.reserve(slots.size());
        for (size_t i = 0; i < slots.size(); ++i)
        {
            slots[i] =
                detail::mappingStringSlot(L2_SYSTEM_CONFIG_KEYS[i], L2_SYSTEM_CONFIG_BASE_SLOT);
            keys.emplace_back(tableName,
                std::string_view(reinterpret_cast<char const*>(slots[i].data()), slots[i].size()));
        }

        // One batched storage read. Missing key or wrong-sized value throws.
        auto entries = co_await bcos::storage2::readSome(*m_storage, keys);
        if (entries.size() != keys.size())
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error(fmt::format("L2ConfigLoader: readSome returned {} entries for "
                                               "{} keys (storage backend bug)",
                    entries.size(), keys.size())));
        }

        // Decode and project each key.
        for (size_t i = 0; i < L2_SYSTEM_CONFIG_KEYS.size(); ++i)
        {
            auto const& key = L2_SYSTEM_CONFIG_KEYS[i];
            if (!entries[i])
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(fmt::format(
                    "L2ConfigLoader: SystemConfig key '{}' is not set (slot empty); the "
                    "predeploy genesis allocs must write every required key",
                    key)));
            }
            auto const slotBytes = entries[i]->get();
            auto const decoded = detail::decodeEntryValue(slotBytes);

            // Schedule gate: a config whose enableNumber is still in the future
            // must not be applied yet — the block keeps its prior value. This
            // mirrors LedgerTypeDef::readFromStorage's `blockNumber >=
            // enableNumber` check so every node activates a scheduled change on
            // the same block (a divergence here would fork the chain).
            if (blockNumber < static_cast<protocol::BlockNumber>(decoded.enableNumber))
            {
                continue;
            }
            auto const& value = decoded.value;

            if (key == "chain_id")
            {
                auto chainId = detail::valueToUint256BE(value);
                bool nonZero = false;
                for (uint8_t byte : chainId.bytes)
                {
                    if (byte != 0)
                    {
                        nonZero = true;
                        break;
                    }
                }
                if (!nonZero)
                {
                    BOOST_THROW_EXCEPTION(std::runtime_error(
                        "L2ConfigLoader: chain_id == 0 breaks EIP-155 replay protection"));
                }
                out.setChainId(chainId);
            }
            else if (key == "gas_limit")
            {
                // The gas-limit tuple's second element is the block the value
                // takes effect on; use the slot's enableNumber, not the caller's
                // current block, so downstream sees the contract's schedule.
                out.setGasLimit({detail::valueToUint64(value, key),
                    static_cast<protocol::BlockNumber>(decoded.enableNumber)});
            }
            else if (key == "block_tx_count_limit")
            {
                out.setBlockTxCountLimit(detail::valueToUint64(value, key));
            }
            else if (key == "compatibility_version")
            {
                out.setCompatibilityVersion(detail::valueToUint32(value, key));
            }
            else
            {
                // L2_SYSTEM_CONFIG_KEYS only contains the 4 keys above. If you
                // add a key, also add a branch here.
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    fmt::format("L2ConfigLoader: unhandled config key '{}'", key)));
            }
        }
        co_return;
    }

private:
    Storage* m_storage;
};
}  // namespace bcos::ledger
