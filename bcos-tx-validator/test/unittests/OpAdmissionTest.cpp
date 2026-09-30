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
 * @file OpAdmissionTest.cpp
 * @brief OP-lane admission: the head base fee is the fee floor, and the balance covers the
 *        rollup cost (Check::L1Cost) -- op-geth txpool/validation.go shape.
 */

#include "AdmissionHarness.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"

namespace bcos::test
{
namespace
{
constexpr u256 kGwei = 1000000000ULL;

/// An OP-lane chain: executor_version 3, a head base fee, and tx_gas_price left at a value the
/// OP lane must NOT govern by. The fixture transaction bids maxFee 30 gwei.
struct OpHarness : AdmitHarness
{
    OpHarness()
    {
        ledgerConfig->setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
        ledgerConfig->setBaseFeePerGas(u256(20) * kGwei);
        ledgerConfig->setGasPrice({"0x0", 0});
    }
    /// tx.Cost() of admitTx(spec): gasLimit * feeCap + value.
    static u256 txCost(TxSpec const& spec = {})
    {
        return u256(spec.gasLimit) * spec.maxFeePerGas + spec.value;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpAdmissionTest)

// Acceptance 1: enough for gas * price + value, not for the L1 fee -> refused at the door with
// InsufficientFunds, the status whose RPC text is op-geth's "insufficient funds for gas * price
// + value" (AdmissionError.cpp).
BOOST_AUTO_TEST_CASE(balanceCoveringGasAndValueButNotTheRollupCostIsRejected)
{
    OpHarness harness;
    harness.account.balance = OpHarness::txCost();
    harness.rollupCost = 1;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
    BOOST_CHECK_EQUAL(harness.rollupCostAsks, 1);
}

BOOST_AUTO_TEST_CASE(balanceCoveringTheRollupCostTooIsAdmitted)
{
    OpHarness harness;
    harness.rollupCost = 123456789;
    harness.account.balance = OpHarness::txCost() + *harness.rollupCost;
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
    // Priced on the wire form and the transaction's gas limit, against the snapshot's head.
    BOOST_CHECK(
        harness.lastRollupEnvelope == bcostars::protocol::reassembleWeb3RawTransaction(
                                          tx->extraTransactionBytes(), tx->signatureData()));
    BOOST_CHECK_EQUAL(harness.lastRollupGasLimit, TxSpec{}.gasLimit);
    BOOST_CHECK_EQUAL(harness.lastRollupHeadNumber, harness.ledgerConfig->blockNumber());

    // One wei short of the total is the boundary.
    harness.account.balance -= 1;
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
}

// The row is a no-op without a bound callable: the FISCO and L1 lanes bind none. The plain
// balance rule still charges gas on the OP lane -- the head base fee is non-zero even with
// tx_gas_price at "0x0", so gas * feeCap + value is the floor, one wei short of it is refused.
BOOST_AUTO_TEST_CASE(unboundRollupCostLeavesTheBalanceRuleAlone)
{
    OpHarness harness;
    harness.account.balance = OpHarness::txCost();
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.rollupCostAsks, 0);
    harness.account.balance -= 1;
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::InsufficientFunds);
}

// The L1 lane binds no callable (Initializer.cpp keys the binding on OPSTACK_EXECUTOR_VERSION),
// so the row stands down there; its fee floor stays tx_gas_price and the head base fee in the
// snapshot is not consulted. The table itself has no lane column: the lane is expressed by
// what is bound and by readChainView's choice of floor.
BOOST_AUTO_TEST_CASE(ethereumLaneKeepsTxGasPriceAsTheFeeFloor)
{
    OpHarness harness;
    harness.ledgerConfig->setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
    harness.ledgerConfig->setBaseFeePerGas(u256(40) * kGwei);  // above the 30 gwei bid
    harness.ledgerConfig->setGasPrice({"0x1", 0});
    harness.account.balance = OpHarness::txCost();
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.rollupCostAsks, 0);
}

// Acceptance 2: the fee floor follows the head base fee block by block. The existing
// FeeCapVsBaseFee row does the work once the chain view reads the head instead of tx_gas_price.
BOOST_AUTO_TEST_CASE(headBaseFeeRiseRejectsAndFallReadmits)
{
    OpHarness harness;
    auto tx = admitTx();  // maxFee 30 gwei
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);

    harness.ledgerConfig->setBaseFeePerGas(u256(40) * kGwei);
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::FeeCapLessThanBaseFee);

    harness.ledgerConfig->setBaseFeePerGas(u256(30) * kGwei);  // equal is enough
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// tx_gas_price is a FISCO governance row; on the OP lane it neither rejects nor admits.
BOOST_AUTO_TEST_CASE(txGasPriceDoesNotGovernTheOpLane)
{
    OpHarness harness;
    harness.ledgerConfig->setGasPrice({"0xfffffffffff", 0});  // would reject under the old rule
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

// Only pool admission prices the envelope: a proposal is judged against pre-block state, where
// the balance rules are off, and a replay fixture has no funded accounts.
BOOST_AUTO_TEST_CASE(proposalAndReplayContextsDoNotPriceTheRollupCost)
{
    OpHarness harness;
    harness.account.balance = 0;
    harness.rollupCost = 1;
    auto tx = admitTx({.value = 0});
    BOOST_CHECK(
        harness.run(*tx, AdmissionContext::ProposalVerification) == TransactionStatus::None);
    BOOST_CHECK(harness.run(*tx, AdmissionContext::EESTReplay) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(harness.rollupCostAsks, 0);
}

// Acceptance 3: the entrance refusals for deposits and blobs are untouched by the new row -- and
// neither reaches it.
BOOST_AUTO_TEST_CASE(depositAndBlobEnvelopesStayRefusedOnTheOpLane)
{
    OpHarness harness;
    harness.rollupCost = 1;
    auto blob = admitTx();
    blob->mutableInner().extraTransactionBytes.at(0) = 0x03;
    BOOST_CHECK(harness.run(*blob) == TransactionStatus::BlobTxNotAllowed);

    auto deposit = admitTx();
    deposit->mutableInner().extraTransactionBytes.at(0) = 0x7e;
    BOOST_CHECK(harness.run(*deposit) == TransactionStatus::TxTypeNotSupported);
    BOOST_CHECK_EQUAL(harness.rollupCostAsks, 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
