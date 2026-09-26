// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Storage2StateBinaryModeTest — end-to-end coverage for the Eth/OP lane running against a node
// in the binary address-table layout (AddressTableMode::Binary): applyDiff must write the
// account table under "/s/<20 raw bytes>", the read path must resolve it back to the same
// account, and visitAccounts' dual-prefix scan must collect it (stateRootOf's mandatory path).

#include <bcos-evm/adapter/Storage2State.h>
#include <bcos-evm/adapter/Storage2StateHelpers.h>

#include <bcos-framework/ledger/AccountTableName.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/testutils/ScopedNodeAddressTableMode.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstring>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

// Independent re-derivation of the binary-layout physical name (do NOT call the helper under
// test — the test must pin the encoding, not mirror it).
std::string binaryLayoutTableName(const evmc::address& addr)
{
    std::string name{"/s/"};
    name.append(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes));
    return name;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Storage2StateBinaryModeTest)

BOOST_AUTO_TEST_CASE(BinaryModeRoundTrip)
{
    const bcos::test::ScopedNodeAddressTableMode guard(
        bcos::ledger::account::AddressTableMode::Binary);
    MutableStorage storage;

    evmc::address addr{};
    addr.bytes[0] = 0x12;
    addr.bytes[19] = 0xff;

    evmc::bytes32 slotKey{};
    slotKey.bytes[31] = 0x01;
    evmc::bytes32 slotValue{};
    slotValue.bytes[31] = 0x2a;

    // Write path: a full modified_accounts entry (nonce/balance/code/one slot).
    {
        bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
        evmone::state::StateDiff diff;
        evmone::state::StateDiff::Entry entry;
        entry.addr = addr;
        entry.nonce = 1;
        entry.balance = 42;
        entry.code = evmc::bytes{0x60, 0x80};
        entry.modified_storage.emplace_back(slotKey, slotValue);
        diff.modified_accounts.push_back(std::move(entry));
        bridge.applyDiff(diff);
        BOOST_CHECK(!bridge.poisoned());
    }

    // Physical layout: the rows live under "/s/<20 raw bytes>", nothing under "/apps/<40hex>".
    {
        auto balanceRow = bcos::task::syncWait(bcos::storage2::readOne(
            storage, StateKey{binaryLayoutTableName(addr),
                         std::string{bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE}}));
        BOOST_REQUIRE(balanceRow.has_value());
        BOOST_CHECK(balanceRow->get() == "42");

        auto hexRow = bcos::task::syncWait(bcos::storage2::readOne(
            storage, StateKey{std::string{"/apps/12000000000000000000000000000000000000ff"},
                         std::string{bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE}}));
        BOOST_CHECK(!hexRow.has_value());

        // The SYS_TABLES liveness marker uses the binary name as its key too.
        auto marker = bcos::task::syncWait(bcos::storage2::readOne(
            storage, StateKey{std::string{bcos::ledger::SYS_TABLES}, binaryLayoutTableName(addr)}));
        BOOST_CHECK(marker.has_value());
    }

    // Read path on a fresh instance (cold caches).
    {
        bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
        const auto account = bridge.get_account(addr);
        BOOST_REQUIRE(account.has_value());
        BOOST_CHECK(account->balance == 42);
        BOOST_CHECK_EQUAL(account->nonce, 1);
        BOOST_CHECK(bridge.get_account_code(addr) == evmc::bytes({0x60, 0x80}));
        BOOST_CHECK(bridge.get_storage(addr, slotKey) == slotValue);
        BOOST_CHECK(!bridge.poisoned());
    }

    // visitAccounts (stateRootOf's mandatory path): the dual-prefix scan collects the
    // binary-layout account exactly once.
    {
        bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
        std::vector<evmc::address> visited;
        const bool complete = bridge.visitAccounts([&visited, &slotValue](const auto& view) {
            visited.push_back(view.addr);
            BOOST_CHECK(view.balance == 42);
            BOOST_REQUIRE_EQUAL(view.storage.size(), 1);
            BOOST_CHECK(view.storage.begin()->second == slotValue);
            return true;
        });
        BOOST_CHECK(complete);
        BOOST_CHECK(!bridge.poisoned());
        BOOST_REQUIRE_EQUAL(visited.size(), 1);
        BOOST_CHECK_EQUAL(std::memcmp(visited[0].bytes, addr.bytes, sizeof(addr.bytes)), 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
