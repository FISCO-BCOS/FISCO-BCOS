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
 * @file L2NativeTxGateTest.cpp
 * @brief Check::BcosTxAllowedOnChain -- a FISCO-native transaction on an OP-Stack L2 chain.
 *
 * An L2 block is verified by op-reth, which rebuilds every transaction from its EIP-2718
 * envelope. A tars BCOSTransaction has no envelope, so one inside a block makes the verifier
 * reject the whole block. feature_l2_ethereum_compat is what says a chain is such an L2.
 *
 * Runs on the shared AdmissionHarness, with one line turning the flag on.
 */

#include "AdmissionHarness.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-tx-validator/CheckSet.h"
#include "bcos-tx-validator/TxPoolNonceChecker.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txvalidator;

namespace bcos::test
{
namespace
{
/// A FISCO-native transaction: outer tars type BCOSTransaction, no EIP-2718 envelope at all.
///
/// Every case below runs it under SignaturePolicy::Disabled, the convention AdmitTest.cpp already
/// uses for BCOS transactions: production is SignaturePolicy::Required, but a tars signature
/// needs a real BCOS key pair and a matching hash, and what this file measures sits AFTER the
/// signature check. That the gate does not skip ahead of the signature check is pinned
/// structurally instead, by theSignatureCheckStillRunsFirst reading the order array -- a stronger
/// statement than one signed fixture reaching a status code.
std::shared_ptr<bcostars::protocol::TransactionImpl> nativeTx(std::string nonce = "native-nonce")
{
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.chainID = "chain0";
    tx->mutableInner().data.nonce = std::move(nonce);
    return tx;
}

/// Turn this chain into an OP-Stack L2. This edits the harness's setup config; run() publishes it
/// through LedgerConfigState::set(), so a case may call this any time before run().
void makeL2(AdmitHarness& harness)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    harness.ledgerConfig->setFeatures(features);
}

std::ptrdiff_t orderIndexOf(Check target)
{
    return std::distance(c_checkOrder.begin(), std::ranges::find(c_checkOrder, target));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(L2NativeTxGateTest)

// The baseline. On an ordinary FISCO chain the flag is off and a native transaction is what the
// chain is for.
BOOST_AUTO_TEST_CASE(nativeTransactionIsAdmittedWhenTheChainIsNotAnL2)
{
    AdmitHarness harness;
    auto tx = nativeTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(nativeTransactionIsRefusedOnAnL2Chain)
{
    AdmitHarness harness;
    makeL2(harness);
    auto tx = nativeTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::BcosTxNotAllowed);
}

// All three contexts carry the bit. A native transaction a leader sealed is exactly as
// unverifiable to op-reth as one submitted over RPC, and the verdict is a function of the chain
// configuration alone, so enforcing it during proposal verification cannot split a block.
BOOST_AUTO_TEST_CASE(everyContextRefusesANativeTransactionOnAnL2Chain)
{
    constexpr std::array contexts{AdmissionContext::PoolAdmission,
        AdmissionContext::ProposalVerification, AdmissionContext::EESTReplay};
    for (auto context : contexts)
    {
        AdmitHarness harness;
        makeL2(harness);
        auto tx = nativeTx();
        BOOST_CHECK_MESSAGE(harness.run(*tx, context, SignaturePolicy::Disabled) ==
                                TransactionStatus::BcosTxNotAllowed,
            "context " << static_cast<int>(context) << " admitted a native transaction on an L2");
    }
    // The table says the same thing, so an edit to a derived column fails here too.
    for (auto context : contexts)
    {
        BOOST_CHECK(contains(checkSet(TxKind::Bcos, context), Check::BcosTxAllowedOnChain));
    }
}

// The bit belongs to TxKind::Bcos only: an L2 chain exists to carry Web3 transactions, so turning
// the flag on must leave them exactly as they were.
BOOST_AUTO_TEST_CASE(web3TransactionIsUnaffectedByTheL2Gate)
{
    AdmitHarness harness;
    makeL2(harness);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);

    for (auto kind : {TxKind::Web3Legacy, TxKind::Web3AccessList, TxKind::Web3DynamicFee,
             TxKind::Web3SetCode, TxKind::Rejected})
    {
        for (auto context : {AdmissionContext::PoolAdmission,
                 AdmissionContext::ProposalVerification, AdmissionContext::EESTReplay})
        {
            BOOST_CHECK_MESSAGE(!contains(checkSet(kind, context), Check::BcosTxAllowedOnChain),
                "the L2 gate leaked into kind " << static_cast<int>(kind));
        }
    }
}

// The verdict must not depend on the account. Stated by counting the two account planes rather
// than inferred from the status code: a status assertion would pass just as well with the reads
// still happening.
BOOST_AUTO_TEST_CASE(theGateDecidesWithoutReadingTheAccount)
{
    AdmitHarness harness;
    makeL2(harness);
    auto tx = nativeTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::BcosTxNotAllowed);
    BOOST_CHECK_EQUAL(harness.accountStateReads(), 0);
    BOOST_CHECK_EQUAL(harness.accountNonceReads(), 0);

    // The table is why: no check in a BCOS transaction's set needs the sender or the account, so
    // verify() performs no account read on that path however this rule is written.
    BOOST_CHECK(!contains(c_senderDependent, Check::BcosTxAllowedOnChain));
    BOOST_CHECK(!contains(c_accountStateDependent, Check::BcosTxAllowedOnChain));
    BOOST_CHECK(contains(c_stateStage, Check::BcosTxAllowedOnChain));
    BOOST_CHECK(!contains(c_poolStage, Check::BcosTxAllowedOnChain));
}

// ... and not on the rest of the chain view either. readChainView derives the EVM revision, the
// base fee and the Web3 chain id only for the checks that read them, and a BCOS transaction's set
// contains none of those -- so tx_gas_price, whose genesis value nothing validates, is not parsed
// on this path. An unparseable one would make readChainView throw, so this is observable end to
// end and not only as a table fact.
BOOST_AUTO_TEST_CASE(theGateDecidesWithoutTheRestOfTheChainView)
{
    for (auto context : {AdmissionContext::PoolAdmission, AdmissionContext::ProposalVerification,
             AdmissionContext::EESTReplay})
    {
        const auto set = checkSet(TxKind::Bcos, context);
        BOOST_CHECK((set & c_revisionDependent) == Check::None);
        BOOST_CHECK((set & c_baseFeeDependent) == Check::None);
        BOOST_CHECK(!contains(set, Check::ChainId));
    }

    AdmitHarness harness;
    makeL2(harness);
    harness.ledgerConfig->setGasPrice({"not a number", 0});
    auto tx = nativeTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::BcosTxNotAllowed);

    // The negative control: the same value DOES stop a Web3 transaction, whose set contains a
    // base-fee reader -- so the case above passes because the field was not derived, not because
    // the value is harmless.
    AdmitHarness web3Harness;
    web3Harness.ledgerConfig->setGasPrice({"not a number", 0});
    auto web3 = admitTx();
    BOOST_CHECK_THROW(web3Harness.run(*web3, AdmissionContext::PoolAdmission), std::exception);
}

// Ordering, end to end: with both nonce checks primed to fail, the reported status is still the
// gate's. A native transaction on an L2 is refused for what it IS, not for the state of a pool it
// was never going to enter.
BOOST_AUTO_TEST_CASE(theGateIsReportedAheadOfTheNonceChecks)
{
    AdmitHarness harness;
    makeL2(harness);
    auto pool = std::make_shared<TxPoolNonceChecker>();
    pool->insert("native-nonce");
    harness.txPoolNonceChecker = pool;
    harness.ledgerNonceChecker =
        std::make_shared<StubLedgerNonceChecker>(TransactionStatus::BlockLimitCheckFail);

    auto tx = nativeTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Disabled) ==
                TransactionStatus::BcosTxNotAllowed);

    // Pinned on the order array as well, so a reordering this case happens not to reach still
    // fails.
    for (auto later : {Check::BcosPoolNonce, Check::BcosLedgerNonce, Check::Balance,
             Check::Web3NonceWindow, Check::SenderIsEOA})
    {
        BOOST_CHECK_MESSAGE(orderIndexOf(Check::BcosTxAllowedOnChain) < orderIndexOf(later),
            "the L2 gate is ordered after check " << static_cast<uint32_t>(later));
    }
}

// The gate sits in the state stage, so the gate stage still runs first. Stated rather than left
// implicit: a native transaction that is ALSO unsigned reports InvalidSignature, and moving the
// L2 rule ahead of signature recovery would be a deliberate change, not an accident.
BOOST_AUTO_TEST_CASE(theSignatureCheckStillRunsFirst)
{
    AdmitHarness harness;
    makeL2(harness);
    auto tx = nativeTx();
    BOOST_CHECK(harness.run(*tx, AdmissionContext::PoolAdmission, SignaturePolicy::Required) ==
                TransactionStatus::InvalidSignature);
    BOOST_CHECK(orderIndexOf(Check::Signature) < orderIndexOf(Check::BcosTxAllowedOnChain));
}

// The status has to travel: MemoryStorage turns it into a bcos::Error carrying the code and
// toString(), and that string is what the JSON-RPC caller reads.
BOOST_AUTO_TEST_CASE(theStatusHasItsOwnLabel)
{
    BOOST_CHECK_EQUAL(toString(TransactionStatus::BcosTxNotAllowed), "BcosTxNotAllowed");
    BOOST_CHECK_EQUAL(static_cast<int32_t>(TransactionStatus::BcosTxNotAllowed), 10024);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
