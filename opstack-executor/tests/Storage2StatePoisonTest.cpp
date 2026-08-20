// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Storage2StatePoisonTest — the shared poison sink added in the StorageStateView→Storage2State
// merge (Part 2): every Storage2State constructed with the same shared_ptr<SharedErrorSlot>
// reports poison to it (mutex-guarded, first-write-wins), so a read error in ANY per-tx execution
// instance is visible to the block-level finalize check that owns the slot (op-geth's dbErr
// accumulating across the block). Without a shared slot the poison stays per-instance (the
// pre-merge behaviour).

#include <opstack-executor/Storage2State.h>
#include <opstack-executor/Storage2StateHelpers.h>

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

using evmc::literals::operator""_address;

constexpr evmc::address kPoisonAddr = 0x00000000000000000000000000000000deadc0de_address;

/// A 32-byte storage-slot key whose value is NOT 32 bytes. Storage2State::fetchStorage validates
/// the slot value length and throws std::length_error — a deterministic poison trigger needing
/// no storage-backend fault injection.
void seedCorruptSlot(MutableStorage& storage, evmc::address const& addr)
{
    const std::string table = bcos::evm::evmstate::accountTableName(addr);
    bcos::storage::Entry e;
    e.set(std::string("abcd"));  // 4 bytes — not a valid 32-byte slot value
    bcos::task::syncWait(
        bcos::storage2::writeOne(storage, StateKey{table, std::string(32, '\x01')}, std::move(e)));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Storage2StatePoisonTest)

/// Part-2 aggregation: a read fault in instance A (per-tx execution) poisons the shared block
/// slot; instance B (block-level finalize — a different Storage2State over the same view) sees
/// poisoned()==true without having performed any read itself.
BOOST_AUTO_TEST_CASE(SharedSinkAggregatesPoisonAcrossInstances)
{
    MutableStorage storage;
    seedCorruptSlot(storage, kPoisonAddr);

    auto sharedError = std::make_shared<bcos::evm::evmstate::SharedErrorSlot>();
    bcos::evm::evmstate::Storage2State<MutableStorage> txInstance(storage, sharedError);
    bcos::evm::evmstate::Storage2State<MutableStorage> finalizeBridge(storage, sharedError);

    evmc::bytes32 slotKey{};
    std::memset(slotKey.bytes, 0x01, sizeof(slotKey.bytes));

    // Instance A reads the corrupt slot → length_error → poison → shared slot set.
    (void)txInstance.get_storage(kPoisonAddr, slotKey);
    BOOST_CHECK(txInstance.poisoned());

    // Instance B has read nothing, yet the shared slot marks it poisoned — the block-level
    // finalize check (Storage2State.h's poison-flag contract) must fail the whole block.
    BOOST_CHECK(finalizeBridge.poisoned());
    BOOST_CHECK(!finalizeBridge.firstError().empty());
    // firstError() prefers the shared slot — the block-wide first error.
    BOOST_CHECK(finalizeBridge.firstError() == txInstance.firstError());
    // The message is the fetchStorage length error, preserved through the shared slot.
    BOOST_CHECK(finalizeBridge.firstError().find("storage slot value size mismatch") !=
                std::string_view::npos);
}

/// Pre-merge behaviour preserved: without a shared slot the two instances are independent — a
/// fault in one does not poison the other.
BOOST_AUTO_TEST_CASE(PerInstancePoisonStaysLocalWithoutSharedSlot)
{
    MutableStorage storage;
    seedCorruptSlot(storage, kPoisonAddr);

    bcos::evm::evmstate::Storage2State<MutableStorage> txInstance(storage);
    bcos::evm::evmstate::Storage2State<MutableStorage> finalizeBridge(storage);

    evmc::bytes32 slotKey{};
    std::memset(slotKey.bytes, 0x01, sizeof(slotKey.bytes));

    (void)txInstance.get_storage(kPoisonAddr, slotKey);
    BOOST_CHECK(txInstance.poisoned());
    BOOST_CHECK(!finalizeBridge.poisoned());
    BOOST_CHECK(finalizeBridge.firstError().empty());
}

BOOST_AUTO_TEST_SUITE_END()
