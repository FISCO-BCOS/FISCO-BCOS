/*
 * EIP-2929 checkpoint journal unit tests (TE scope revert warm rollback).
 */
#include "bcos-executor/src/vm/Eip2929AccessState.h"
#include "bcos-executor/src/CallParameters.h"
#include <boost/test/unit_test.hpp>
#include <cstring>

using bcos::executor::Eip2929AccessState;

BOOST_AUTO_TEST_SUITE(Eip2929AccessStateCheckpoint)

static evmc_address addrByte(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}

BOOST_AUTO_TEST_CASE(journal_records_only_with_active_checkpoint)
{
    Eip2929AccessState st;
    auto a = addrByte(0x01);
    BOOST_CHECK(st.warmUpAddress(a));
    st.pushCheckpoint();
    auto b = addrByte(0x02);
    BOOST_CHECK(st.warmUpAddress(b));
    st.rollbackCheckpoint();
    BOOST_CHECK(!st.containsAddress(b));
    BOOST_CHECK(st.containsAddress(a));
}

BOOST_AUTO_TEST_CASE(commit_merges_child_warm_then_parent_rollback_clears_all)
{
    Eip2929AccessState st;
    st.pushCheckpoint();  // parent CP
    auto x = addrByte(0xaa);
    st.pushCheckpoint();  // child CP
    BOOST_CHECK(st.warmUpAddress(x));
    st.commitCheckpoint();  // merge X into parent journal
    BOOST_REQUIRE(st.hasActiveCheckpoint());
    BOOST_CHECK(st.containsAddress(x));
    st.rollbackCheckpoint();  // parent fails — merged child warm is rolled back too
    BOOST_CHECK(!st.containsAddress(x));
}

/// Committed child journals merge into parent; parent rollback erases all merged warms.
BOOST_AUTO_TEST_CASE(nested_commit_checkpoint_merges_journal)
{
    Eip2929AccessState st;
    auto a = addrByte(0xb1);
    auto b = addrByte(0xb2);

    st.pushCheckpoint();  // parent CP
    st.pushCheckpoint();  // child CP
    BOOST_CHECK(st.warmUpAddress(a));
    st.commitCheckpoint();  // merge A into parent journal
    BOOST_REQUIRE(st.hasActiveCheckpoint());
    BOOST_CHECK(st.containsAddress(a));

    st.pushCheckpoint();  // second child CP
    BOOST_CHECK(st.warmUpAddress(b));
    st.commitCheckpoint();    // merge B into parent journal
    st.rollbackCheckpoint();  // parent fails — merged A and B rolled back
    BOOST_CHECK(!st.containsAddress(a));
    BOOST_CHECK(!st.containsAddress(b));
}

BOOST_AUTO_TEST_CASE(parent_warm_survives_child_rollback_erases_child_only)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto parentOnly = addrByte(0xa1);
    BOOST_CHECK(st.warmUpAddress(parentOnly));
    st.pushCheckpoint();
    auto childOnly = addrByte(0xa2);
    BOOST_CHECK(st.warmUpAddress(childOnly));
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(parentOnly));
    BOOST_CHECK(!st.containsAddress(childOnly));
}

/// Simulates evmone access_account during CREATE with no setCreateRollbackPin — EIP violation path.
BOOST_AUTO_TEST_CASE(evmone_journal_warm_erased_without_create_pin)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto contract = addrByte(0x55);
    BOOST_CHECK(st.warmUpAddress(contract));
    st.rollbackCheckpoint();
    BOOST_CHECK(!st.containsAddress(contract));
}

BOOST_AUTO_TEST_CASE(initial_tx_warm_bypasses_journal)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    evmc_address origin = addrByte(0x11);
    st.warmUpInitialTxSet(origin, std::nullopt, EVMC_CANCUN);
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(origin));  // baseline survives rollback
}

BOOST_AUTO_TEST_CASE(access_list_warm_bypasses_journal)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    evmc_address listAddr = addrByte(0x44);
    evmc_bytes32 slot{};
    slot.bytes[31] = 0x07;
    bcos::executor::Eip2930AccessList list{{"ignored", {bcos::h256(slot.bytes, bcos::h256::SIZE)}}};
    st.warmUpAccessList(list, [&](std::string const&) { return listAddr; });
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(listAddr));
    BOOST_CHECK(st.containsStorage(listAddr, slot));
}

BOOST_AUTO_TEST_CASE(eip7702_warm_bypasses_journal)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto authority = addrByte(0x22);
    auto target = addrByte(0x33);
    BOOST_CHECK(st.warmUpAddressNoJournal(authority));
    BOOST_CHECK(st.warmUpAddressNoJournal(target));
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(authority));
    BOOST_CHECK(st.containsAddress(target));
}

BOOST_AUTO_TEST_CASE(create_pin_keeps_address_when_only_journal_warm_would_be_erased)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto contract = addrByte(0x56);
    st.setCreateRollbackPin(contract);
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(contract));
}

/// Pin protects journal warmth from warmUpAddress even when pin itself adds no journal entry.
BOOST_AUTO_TEST_CASE(create_pin_without_journal_entry)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto contract = addrByte(0x57);
    BOOST_CHECK(st.warmUpAddress(contract));
    st.setCreateRollbackPin(contract);
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(contract));
}

BOOST_AUTO_TEST_CASE(create_address_survives_failed_child_scope_rollback)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto contract = addrByte(0x42);
    st.pushCheckpoint();
    st.setCreateRollbackPin(contract);
    BOOST_CHECK(st.containsAddress(contract));
    BOOST_CHECK(!st.warmUpAddress(contract));
    evmc_bytes32 slot{};
    slot.bytes[31] = 0x01;
    BOOST_CHECK(st.warmUpStorage(contract, slot));
    auto inner = addrByte(0x43);
    BOOST_CHECK(st.warmUpAddress(inner));
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(contract));
    BOOST_CHECK(!st.containsStorage(contract, slot));
    BOOST_CHECK(!st.containsAddress(inner));
}

/// Child CREATE succeeds (commit) but parent scope later rolls back — pin must propagate.
BOOST_AUTO_TEST_CASE(create_pin_survives_commit_then_parent_rollback)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto contract = addrByte(0x58);
    st.pushCheckpoint();
    st.setCreateRollbackPin(contract);
    st.commitCheckpoint();
    BOOST_REQUIRE(st.hasActiveCheckpoint());
    BOOST_CHECK(st.containsAddress(contract));
    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(contract));
}

/// Multiple successful child CREATE commits in one parent scope keep all pinned addresses warm.
BOOST_AUTO_TEST_CASE(multiple_create_pins_survive_commit_then_parent_rollback)
{
    Eip2929AccessState st;
    st.pushCheckpoint();
    auto contract1 = addrByte(0x59);
    auto contract2 = addrByte(0x5a);

    st.pushCheckpoint();
    st.setCreateRollbackPin(contract1);
    st.commitCheckpoint();

    st.pushCheckpoint();
    st.setCreateRollbackPin(contract2);
    st.commitCheckpoint();

    st.rollbackCheckpoint();
    BOOST_CHECK(st.containsAddress(contract1));
    BOOST_CHECK(st.containsAddress(contract2));
}

BOOST_AUTO_TEST_SUITE_END()
