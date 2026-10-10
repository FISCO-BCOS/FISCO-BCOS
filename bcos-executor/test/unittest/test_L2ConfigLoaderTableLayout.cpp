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
 * @file test_L2ConfigLoaderTableLayout.cpp
 * @brief L2ConfigLoaderImpl must read the SystemConfig slots from the table the genesis
 *        import wrote them to. Genesis derives that table through
 *        account::ethLaneAccountTableName, which on a Binary-layout node is
 *        "/s/<20 raw bytes>", not "/apps/<40 hex>". A loader that spells the Hex name by hand
 *        sees an empty table on every Binary node and refuses to start ("key not set").
 */
#include <bcos-framework/ledger/AccountTableName.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/L2ConfigLoader.h>
#include <bcos-framework/ledger/L2SystemConfigTable.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
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

namespace l2_config_loader_table_layout_test
{
namespace
{
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
            result.emplace_back(it == data.end() ? std::nullopt : std::make_optional(it->second));
        }
        co_return result;
    }
};

/// Switch the process-wide account-table mode for one test and restore it afterwards, so
/// the other cases in this binary keep the Hex default they were written against.
struct AddressTableModeGuard
{
    account::AddressTableMode previous{account::nodeAddressTableMode()};
    explicit AddressTableModeGuard(account::AddressTableMode mode)
    {
        account::setNodeAddressTableMode(mode);
    }
    ~AddressTableModeGuard() { account::setNodeAddressTableMode(previous); }
    AddressTableModeGuard(AddressTableModeGuard const&) = delete;
    AddressTableModeGuard& operator=(AddressTableModeGuard const&) = delete;
};

std::array<uint8_t, 24> low192(uint64_t value)
{
    std::array<uint8_t, 24> result{};
    for (int shift = 56, i = 0; shift >= 0; shift -= 8, ++i)
    {
        result[16 + i] = static_cast<uint8_t>((value >> shift) & 0xFFU);
    }
    return result;
}

/// Write one packed Entry slot (enableNumber 0) for `configKey` under `tableName`.
void putSlot(FakeSlotStorage& storage, std::string const& tableName, std::string_view configKey,
    uint64_t value)
{
    auto slot = l2_loader_detail::mappingStringSlot(configKey, L2_SYSTEM_CONFIG_BASE_SLOT);
    std::array<uint8_t, 32> packed{};
    auto const valueBytes = low192(value);
    std::copy(valueBytes.begin(), valueBytes.end(), packed.begin() + 8);
    bcos::storage::Entry entry;
    entry.set(std::string(reinterpret_cast<char const*>(packed.data()), packed.size()));
    storage.data.emplace(
        StateKey(
            tableName, std::string_view(reinterpret_cast<char const*>(slot.data()), slot.size())),
        std::move(entry));
}

void seedAllKeys(FakeSlotStorage& storage, std::string const& tableName)
{
    putSlot(storage, tableName, "chain_id", 20200);
    putSlot(storage, tableName, "gas_limit", 30'000'000);
    putSlot(storage, tableName, "block_tx_count_limit", 3);
    putSlot(storage, tableName, "compatibility_version", 0x03120000);
}

std::string hexRuleTableName()
{
    return std::string(bcos::ledger::SYS_DIRECTORY::USER_APPS) +
           std::string(L2_SYSTEM_CONFIG_ADDRESS_HEX);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(L2ConfigLoaderTableLayoutTest)

BOOST_AUTO_TEST_CASE(HexLayoutTableIsTheAppsHexName)
{
    AddressTableModeGuard guard(account::AddressTableMode::Hex);
    BOOST_CHECK_EQUAL(l2SystemConfigTableName(), hexRuleTableName());
    // The genesis import rule and the loader rule are the same function; pin that identity.
    BOOST_CHECK_EQUAL(l2SystemConfigTableName(),
        account::ethLaneAccountTableName(bcos::Address{
            L2_SYSTEM_CONFIG_ADDRESS_HEX, bcos::Address::FromHex, bcos::Address::AlignRight}));

    // The production loader is handed that name and reads exactly that table.
    FakeSlotStorage storage;
    seedAllKeys(storage, hexRuleTableName());
    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, l2SystemConfigTableName());
    LedgerConfig config;
    task::syncWait(loader.loadIntoLedgerConfig(1, config));
    BOOST_CHECK_EQUAL(config.blockTxCountLimit(), 3U);
}

BOOST_AUTO_TEST_CASE(BinaryLayoutReadsTheRawBytesTable)
{
    AddressTableModeGuard guard(account::AddressTableMode::Binary);

    auto const table = l2SystemConfigTableName();
    // "/s/" + 20 raw address bytes: the physical name genesis wrote on this node.
    BOOST_REQUIRE_EQUAL(table.size(), 3U + 20U);
    BOOST_CHECK_EQUAL(table.substr(0, 3), "/s/");
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(table[3]), 0x43U);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(table.back()), 0xC0U);
    BOOST_CHECK_EQUAL(
        table, account::ethLaneAccountTableName(bcos::Address{L2_SYSTEM_CONFIG_ADDRESS_HEX,
                   bcos::Address::FromHex, bcos::Address::AlignRight}));

    FakeSlotStorage storage;
    seedAllKeys(storage, table);
    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, table);
    LedgerConfig config;
    task::syncWait(loader.loadIntoLedgerConfig(1, config));

    BOOST_REQUIRE(config.chainId().has_value());
    BOOST_CHECK_EQUAL(fromBigEndian<u256>(config.chainId()->bytes), u256(20200));
    BOOST_CHECK_EQUAL(std::get<0>(config.gasLimit()), 30'000'000U);
    BOOST_CHECK_EQUAL(config.blockTxCountLimit(), 3U);
    BOOST_CHECK_EQUAL(config.compatibilityVersion(), 0x03120000U);
}

/// The regression itself: slots written under the hand-spelled Hex name are invisible to a
/// Binary-layout node, and the loader must say so rather than serve defaults.
BOOST_AUTO_TEST_CASE(BinaryLayoutDoesNotReadTheHexName)
{
    AddressTableModeGuard guard(account::AddressTableMode::Binary);

    FakeSlotStorage storage;
    seedAllKeys(storage, hexRuleTableName());
    L2ConfigLoaderImpl<FakeSlotStorage> loader(storage, l2SystemConfigTableName());
    LedgerConfig config;
    BOOST_CHECK_EXCEPTION(task::syncWait(loader.loadIntoLedgerConfig(1, config)),
        std::runtime_error, [](std::runtime_error const& error) {
            return std::string(error.what()).find("is not set") != std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace l2_config_loader_table_layout_test
