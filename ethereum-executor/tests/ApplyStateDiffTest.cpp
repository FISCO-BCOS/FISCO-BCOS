// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

/// ApplyStateDiffTest.cpp — Task 6.1 write-back fix (B4): a deleted_accounts entry
/// must remove the account's SYS_TABLES marker, not just clear its fields. Otherwise a
/// touch-deleted account lingers as an EIP-161 ghost empty account (existence-marker
/// row survives while all fields are zeroed), diverging from Storage2State::applyDeletedEntry.
///
/// Assertion contract (plan v2, B4): we assert the SYS_TABLES marker is removed — the
/// field rows are zeroed by clearAccountStorage and are not part of this assertion
/// (a range-delete of the field rows would be a bigger change than the fix makes).

#define BOOST_TEST_MODULE ApplyStateDiffTest
#include "TestMemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage2/Storage.h"
#include "opstack-executor/Storage2State.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <string>

using namespace bcos;
using namespace bcos::executor_v1;
using namespace evmc::literals;  // _address

namespace
{
// An ordinary /apps/ account (not one of the c_systemTxsAddress members routed to /sys/).
constexpr evmc::address kGhostAddr = 0x00000000000000000000000000000000deadbeef_address;
}  // namespace

BOOST_AUTO_TEST_CASE(applyStateDiffDeletedAccountRemovesSysTablesMarker)
{
    MutableStorage storage;

    // Set up an EMPTY account on the ledger: only the SYS_TABLES marker row exists
    // (nonce=0, balance=0, no code — the EIP-161 touch-delete scenario).
    ledger::account::EVMAccount<MutableStorage> acc(storage, kGhostAddr, false);
    task::syncWait(acc.create());
    std::string tableName = std::string(task::syncWait(acc.path()));

    // Sanity: the empty account exists on the ledger (SYS_TABLES marker present).
    BOOST_REQUIRE(task::syncWait(
        storage2::existsOne(storage, executor_v1::StateKeyView(ledger::SYS_TABLES, tableName))));

    evmone::state::StateDiff diff;
    diff.deleted_accounts.push_back(kGhostAddr);

    // Merged write-back path: the former eth::applyStateDiff(storage, diff, rev, hash) was
    // folded into Storage2State::applyDiff (Storage2State.h) — one bridge owns both the read
    // and write-back sides, no separate hash argument.
    bcos::evm::evmstate::Storage2State<MutableStorage> stateView(storage);
    stateView.applyDiff(diff);

    // B4: the deleted account's SYS_TABLES marker must be removed — no EIP-161 ghost
    // empty account may linger after touch-delete.
    BOOST_CHECK(!task::syncWait(
        storage2::existsOne(storage, executor_v1::StateKeyView(ledger::SYS_TABLES, tableName))));
}
