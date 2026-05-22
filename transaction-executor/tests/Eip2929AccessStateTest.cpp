/*
 * EIP-2929 checkpoint journal unit tests (TE scope revert warm rollback).
 */
#include "bcos-executor/src/vm/Eip2929AccessState.h"
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

BOOST_AUTO_TEST_CASE(commit_merges_child_journal_into_parent)
{
    Eip2929AccessState st;
    st.pushCheckpoint();  // parent CP
    auto x = addrByte(0xaa);
    st.pushCheckpoint();  // child CP
    BOOST_CHECK(st.warmUpAddress(x));
    st.commitCheckpoint();  // merge X into parent journal
    BOOST_REQUIRE(st.hasActiveCheckpoint());
    BOOST_CHECK(st.containsAddress(x));
    st.rollbackCheckpoint();  // parent fails
    BOOST_CHECK(!st.containsAddress(x));
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

BOOST_AUTO_TEST_SUITE_END()
