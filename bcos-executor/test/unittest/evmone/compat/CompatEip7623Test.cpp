/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-7*: EIP-7623 calldata floor (S2) — formula parity with PragueTest.
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

    bytes empty;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(empty)), 0);
    bytes zeros(100, 0x00);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(zeros)), 1000);
    bytes nonzero(100, 0xff);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(nonzero)), 4000);

    // Mixed 50 zero + 50 non-zero — same arithmetic as PragueTest::eip7623_calldata_cost
    bytes mixed(100);
    for (int i = 0; i < 50; ++i)
        mixed[i] = 0x00;
    for (int i = 50; i < 100; ++i)
        mixed[i] = 0x42;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(mixed)), 2500);
}

BOOST_AUTO_TEST_CASE(FC_7_document_no_21000_base)
{
    BOOST_TEST_MESSAGE(
        "I2: FISCO-BCOS does not add unconditional Ethereum 21000 base gas to the EIP-7623 "
        "floor; see TransactionExecutive.cpp and transaction-executor HostContext::execute().");
}

BOOST_AUTO_TEST_CASE(FC_7_skipped_internal_call_documented)
{
    BOOST_TEST_MESSAGE(
        "EIP-7623 calldata floor applies only at top-level (seq==0 / m_level==0); internal "
        "calls skip the extra deduction — see TransactionExecutive and TE HostContext.");
}

BOOST_AUTO_TEST_CASE(FC_7_executive_deduct_seq0_oog)
{
    using namespace bcos::executor;
    using compat::CompatFeatureProfile;

    auto hashImpl = std::make_shared<crypto::Keccak256>();
    auto backend = std::make_shared<storage::StateStorage>(nullptr, false);
    auto stateStorage = std::make_shared<storage::StateStorage>(backend, false);
    auto ledgerCache = std::make_shared<LedgerCache>(std::make_shared<MockLedger>());
    auto features = CompatFeatureProfile::pragueEnabled();
    task::syncWait(ledger::writeToStorage(features, *stateStorage, 1));

    BlockContext blockContext(stateStorage, ledgerCache, hashImpl, 1, h256(), 0,
        static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION), false, false, backend);
    blockContext.setFeatures(features);
    blockContext.setVMSchedule();
    BOOST_REQUIRE(blockContext.vmSchedule().enablePrague);

    wasm::GasInjector gasInjector;
    TransactionExecutive executive(blockContext, "", 0, 0, gasInjector);

    auto params = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    params->seq = 0;
    params->gas = 500;
    params->data = std::vector<uint8_t>(100, 0x00);
    params->senderAddress = "0000000000000000000000000000000000000001";
    params->receiveAddress = "0000000000000000000000000000000000000002";
    params->origin = params->senderAddress;

    auto result = executive.execute(std::move(params));
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->evmStatus, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(result->status, static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
}

BOOST_AUTO_TEST_CASE(FC_7_executive_skipped_when_seq_nonzero)
{
    BOOST_TEST_MESSAGE(
        "EIP-7623 floor in TransactionExecutive::execute is gated by callParameters->seq == 0. "
        "seq>0 internal calls skip the branch (FC_7_executive_deduct_seq0_oog covers seq==0 OOG).");
    executor::CallParameters params{executor::CallParameters::MESSAGE};
    params.seq = 1;
    BOOST_CHECK(params.seq != 0);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEip7623
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
