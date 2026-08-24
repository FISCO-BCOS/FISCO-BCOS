/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-SD*: selfdestruct / EIP-6780 documented deviation (S0 D3).
 *
 *  All test cases in this file are documentation stubs. SELFDESTRUCT is an EVM
 *  opcode, not a precompile — the compat harness (CompatHostContextHarness) only
 *  drives precompile calls via compatCallBuiltInPrecompiled.  A contract deploy +
 *  SELFDESTRUCT call requires a full executor stack (BlockContext + state storage
 *  + EVM execution).  These tests will be moved or re-implemented in
 *  transaction-executor/tests/ when the integration harness is available.
 *
 *  @file CompatSelfdestructTest.cpp
 */

#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatSelfdestruct)

BOOST_AUTO_TEST_CASE(FC_SD_eip6780_deviation_documented)
{
    BOOST_TEST_MESSAGE(
        "S0-D3: host selfdestruct returns false (no refund) for EIP-6780 safety. "
        "Full EIP-6780 same-tx-create tracking is not yet implemented — Cancun+ may "
        "differ from mainnet on pre-existing contract SELFDESTRUCT. "
        "Move to transaction-executor integration tests when harness supports EVM execution.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_SD_A_pre_cancun_legacy_baseline_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-A TODO: <Cancun baseline. Requires contract deploy + SELFDESTRUCT via EVM "
        "execution (not a precompile call). Skipped in compat harness — move to "
        "transaction-executor/tests/ when available.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_SD_B_cancun_existing_contract_should_not_delete_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-B TODO: Cancun+ existing contract path (deploy in tx1, SELFDESTRUCT in tx2). "
        "Primary detector for EIP-6780 divergence on pre-existing contracts. "
        "Requires full executor stack — move to transaction-executor integration tests.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_SD_C_cancun_same_tx_create_then_selfdestruct_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-C TODO: Cancun+ same-tx CREATE->SELFDESTRUCT exception branch. "
        "Pair with SD-B to validate conditional semantics (B keep / C destroy). "
        "Requires full executor stack — move to transaction-executor integration tests.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatSelfdestruct
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
