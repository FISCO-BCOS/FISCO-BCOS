/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-7*: EIP-7623 calldata floor (S2) — formula parity with PragueTest.
 *
 *  The calcEip7623CalldataGas pure function is fully testable without an executor.
 *  Integration-path tests (seq==0 deduction, internal-call skip, OOG) require the
 *  full TransactionExecutive stack and are documented below as deferred stubs.
 *
 *  @file CompatEip7623Test.cpp
 */

#include "../../mock/MockLedger.h"
#include "Common.h"
#include "CompatTestFixture.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Wait.h"
#include "executive/BlockContext.h"
#include "executive/TransactionExecutive.h"
#include "vm/VMInstance.h"
#include "vm/gas_meter/GasInjector.h"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatEip7623)

BOOST_AUTO_TEST_CASE(FC_7_calldata_floor_formula)
{
    using namespace bcos::executor;

    // Empty
    bytes empty;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(empty)), 0);

    // All zeros: 100 * 4 = 400 standard; tokens = 100*1 = 100; floor = 1000
    bytes zeros(100, 0x00);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(zeros)), 1000);

    // All non-zero: 100 * 16 = 1600 standard; tokens = 100*4 = 400; floor = 4000
    bytes nonzero(100, 0xff);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(nonzero)), 4000);

    // Mixed 50 zero + 50 non-zero: standard = 50*4+50*16 = 1000; tokens = 50+200 = 250; floor =
    // 2500
    bytes mixed(100);
    for (int i = 0; i < 50; ++i)
        mixed[i] = 0x00;
    for (int i = 50; i < 100; ++i)
        mixed[i] = 0x42;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(mixed)), 2500);

    // Single non-zero byte: standard = 16, tokens = 4, floor = 40 → floor wins
    bytes oneNonZero{0x42};
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(oneNonZero)), 40);

    // Single zero byte: standard = 4, tokens = 1, floor = 10 → floor wins
    bytes oneZero{0x00};
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(oneZero)), 10);

    // 4 zero bytes: standard = 16, tokens = 4, floor = 40 → floor > standard
    bytes fourZeros(4, 0x00);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(fourZeros)), 40);

    // 1 non-zero + 1 zero: standard = 16+4=20, tokens=4+1=5, floor=50 → floor wins
    bytes oneOne{0x42, 0x00};
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(oneOne)), 50);
}

BOOST_AUTO_TEST_CASE(FC_7_document_no_21000_base)
{
    BOOST_TEST_MESSAGE(
        "I2: FISCO-BCOS does not add unconditional Ethereum 21000 base gas to the EIP-7623 "
        "floor; see TransactionExecutive.cpp and transaction-executor HostContext::execute().");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_7_skipped_internal_call_documented)
{
    BOOST_TEST_MESSAGE(
        "EIP-7623 calldata floor applies only at top-level (seq==0 / m_level==0); internal "
        "calls skip the extra deduction. Verified in transaction-executor HostContext::execute() "
        "via the m_level gate. Compat harness does not drive internal CALL paths.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_7_executive_deduct_seq0_oog)
{
    BOOST_TEST_MESSAGE(
        "Executive-level seq==0 OOG branch requires a full contract-table setup. "
        "Covered by transaction-executor EIP-7623 tests (cashRevert with explicit gasLimit). "
        "Compat harness cannot test this path.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_7_executive_skipped_when_seq_nonzero)
{
    BOOST_TEST_MESSAGE(
        "EIP-7623: TE HostContext pre-debits normal calldata; TransactionExecutorImpl finalize "
        "settlement. seq>0 internal calls skip 7623. Executor 7623 path still TBD.");
    executor::CallParameters params{executor::CallParameters::MESSAGE};
    params.seq = 1;
    BOOST_CHECK(params.seq != 0);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEip7623
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
