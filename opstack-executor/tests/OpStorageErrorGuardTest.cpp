// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpStorageErrorGuardTest — the OP fail-loud storage channel (step 3.1.2):
// EthereumState's optional error slot reports every swallowed read failure
// while still returning the legacy zero value, and the block-boundary check
// (OpStorageErrorGuard::throwIfPoisoned) turns a poisoned block into
// bcos::evm::engine::OpStorageError. The default (slot unset) keeps the L1
// fail-safe behaviour byte-identical — pinned by the first case below.
//
// The three read wrappers (readAccount/readStorage/readCode) are private, so
// each case reaches them through the public funnel exactly as the evmc Host
// does: find() → readAccount, get_storage() → readStorage, get_code() →
// readCode (only reached once the journal entry carries a non-empty code
// hash).

#include <opstack-executor/OpStorageErrorGuard.h>

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using bcos::executor_v1::opstack::OpStorageErrorGuard;
using bcos::executor_v1::opstack::OpStorageErrorSlot;
namespace eth = bcos::executor_v1::eth;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

using evmc::literals::operator""_address;
constexpr evmc::address kAddr = 0x00000000000000000000000000000000deadc0de_address;

/// A code hash that is not EthAccount::EMPTY_CODE_HASH, so get_code() proceeds
/// past the journal and reaches readCode().
const evmc::bytes32 kNonEmptyCodeHash = [] {
    evmc::bytes32 h{};
    std::memset(h.bytes, 0x02, sizeof(h.bytes));
    return h;
}();

evmc::bytes32 slotKey()
{
    evmc::bytes32 key{};
    std::memset(key.bytes, 0x01, sizeof(key.bytes));
    return key;
}

/// Storage wrapper whose reads fail on demand — a deterministic storage fault
/// needing no corrupt-row trickery (a corrupt row cannot exercise readStorage:
/// the 32-byte copy performs no length validation). Every EVMAccount read
/// (nonce/balance/codeHash/code/storage slot, and exists() via existsOne's
/// readOne fallback) funnels through readOne; hasStorageImpl's scan funnels
/// through range and is forwarded untouched (an armed fault always throws on
/// the account-field reads before the scan is reached).
struct FaultyStorage
{
    MutableStorage& m_inner;
    bool m_armed = true;

    auto readOne(auto key, auto&&... args)
        -> bcos::task::Task<std::optional<bcos::storage::Entry>>
    {
        if (m_armed)
            throw std::runtime_error("injected storage fault");
        co_return co_await m_inner.readOne(std::move(key), std::forward<decltype(args)>(args)...);
    }

    auto range(auto&&... args)
    {
        return m_inner.range(std::forward<decltype(args)>(args)...);
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpStorageErrorGuardSuite)

/// L1 contract unchanged: without an injected handler the three read wrappers
/// swallow the fault and return the zero value, exactly as before the slot
/// existed.
BOOST_AUTO_TEST_CASE(DefaultSlotKeepsLegacySilentZeroReads)
{
    MutableStorage inner;
    FaultyStorage storage{inner};
    eth::EthereumState<FaultyStorage> state(storage);

    // readAccount path.
    BOOST_CHECK(state.find(kAddr) == nullptr);

    // readStorage path: get() asserts the account is in the journal, so park
    // it there first, then read a fresh slot.
    auto& acc = state.get_or_insert(kAddr);
    BOOST_CHECK(evmc::is_zero(state.get_storage(kAddr, slotKey()).current));

    // readCode path: only reached when code_hash != EMPTY_CODE_HASH.
    acc.code_hash = kNonEmptyCodeHash;
    BOOST_CHECK(state.get_code(kAddr).empty());
}

/// With the handler injected the read still returns the zero value (no
/// mid-journal exception), but the slot is poisoned and the block-boundary
/// check fails loud with OpStorageError.
BOOST_AUTO_TEST_CASE(GuardPoisonsAndThrowIfPoisonedThrowsOpStorageError)
{
    for (int which = 0; which < 3; ++which)
    {
        MutableStorage inner;
        FaultyStorage storage{inner};
        eth::EthereumState<FaultyStorage> state(storage);
        OpStorageErrorGuard guard(state);
        BOOST_CHECK(!guard.poisoned());

        switch (which)
        {
        case 0:  // readAccount
            BOOST_CHECK(state.find(kAddr) == nullptr);
            break;
        case 1:  // readStorage
        {
            storage.m_armed = false;
            state.get_or_insert(kAddr);  // clean read: park the account in the journal
            storage.m_armed = true;
            BOOST_CHECK(evmc::is_zero(state.get_storage(kAddr, slotKey()).current));
            break;
        }
        default:  // readCode
        {
            storage.m_armed = false;
            auto& acc = state.get_or_insert(kAddr);
            acc.code_hash = kNonEmptyCodeHash;
            storage.m_armed = true;
            BOOST_CHECK(state.get_code(kAddr).empty());
            break;
        }
        }
        BOOST_CHECK(guard.poisoned());
        BOOST_CHECK(guard.slot()->firstErrorMessage().find("injected storage fault") !=
                    std::string::npos);
        BOOST_CHECK_THROW(guard.throwIfPoisoned(), bcos::evm::engine::OpStorageError);
    }
}

/// op-geth dbErr semantics: first-write-wins, and one shared slot aggregates
/// across per-tx EthereumState instances (a fault in instance A is visible to
/// the block-level check holding only the slot).
BOOST_AUTO_TEST_CASE(SharedSlotAggregatesAcrossInstancesFirstErrorWins)
{
    MutableStorage inner;
    FaultyStorage storageA{inner};
    FaultyStorage storageB{inner};

    auto slot = std::make_shared<OpStorageErrorSlot>();
    eth::EthereumState<FaultyStorage> stateA(storageA);
    eth::EthereumState<FaultyStorage> stateB(storageB);
    OpStorageErrorGuard guardA(stateA, slot);
    OpStorageErrorGuard guardB(stateB, slot);

    (void)stateA.find(kAddr);  // poisons the shared slot
    BOOST_CHECK(guardB.poisoned());  // visible without any read on B
    BOOST_CHECK_THROW(guardB.throwIfPoisoned(), bcos::evm::engine::OpStorageError);

    // First-write-wins at the slot level.
    OpStorageErrorSlot direct;
    direct.record(std::make_exception_ptr(std::runtime_error("first")));
    direct.record(std::make_exception_ptr(std::runtime_error("second")));
    BOOST_CHECK_EQUAL(direct.firstErrorMessage(), "first");
}

/// A healthy block leaves the slot clean: reads over working storage do not
/// poison, and throwIfPoisoned is a no-op.
BOOST_AUTO_TEST_CASE(CleanReadsDoNotPoison)
{
    MutableStorage inner;
    FaultyStorage storage{inner, /*m_armed=*/false};
    eth::EthereumState<FaultyStorage> state(storage);
    OpStorageErrorGuard guard(state);

    BOOST_CHECK(state.find(kAddr) == nullptr);

    auto& acc = state.get_or_insert(kAddr);
    BOOST_CHECK(evmc::is_zero(state.get_storage(kAddr, slotKey()).current));

    acc.code_hash = kNonEmptyCodeHash;
    BOOST_CHECK(state.get_code(kAddr).empty());

    BOOST_CHECK(!guard.poisoned());
    BOOST_CHECK_NO_THROW(guard.throwIfPoisoned());
}

BOOST_AUTO_TEST_SUITE_END()
