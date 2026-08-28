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
 * @file CheckSetTest.cpp
 * @brief The (kind x context x policy) routing table, asserted as a table.
 */

#include "bcos-tx-validator/CheckSet.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <set>

using namespace bcos::txvalidator;

namespace bcos::test
{
namespace
{
constexpr std::array kKinds{TxKind::Bcos, TxKind::Web3Legacy, TxKind::Web3AccessList,
    TxKind::Web3DynamicFee, TxKind::Web3SetCode, TxKind::Rejected};
constexpr std::array kContexts{AdmissionContext::PoolAdmission,
    AdmissionContext::ProposalVerification, AdmissionContext::EESTReplay};
constexpr std::array kPolicies{SignaturePolicy::Required, SignaturePolicy::Disabled};
}  // namespace

BOOST_AUTO_TEST_SUITE(CheckSetTest)

// The PoolAdmission column, spelled out. If someone edits poolAdmissionCheckSet, this is what
// says so.
BOOST_AUTO_TEST_CASE(poolAdmissionColumnIsExact)
{
    BOOST_CHECK(checkSet(TxKind::Bcos, AdmissionContext::PoolAdmission) ==
                (Check::TypeGate | Check::ToFieldFormat | Check::Signature |
                    Check::BcosGroupChainId | Check::BcosPoolNonce));

    constexpr auto legacy = Check::TypeGate | Check::ToFieldFormat | Check::Signature |
                            Check::MaxGasLimit | Check::FeeCapVsBaseFee | Check::ChainId |
                            Check::SenderIsEOA | Check::NonceNotMax | Check::Web3NonceWindow |
                            Check::InitCodeSize | Check::Balance | Check::IntrinsicGas;
    BOOST_CHECK(checkSet(TxKind::Web3Legacy, AdmissionContext::PoolAdmission) == legacy);
    BOOST_CHECK(checkSet(TxKind::Web3AccessList, AdmissionContext::PoolAdmission) ==
                (legacy | Check::TypeByRevision));
    BOOST_CHECK(checkSet(TxKind::Web3DynamicFee, AdmissionContext::PoolAdmission) ==
                (legacy | Check::TypeByRevision | Check::TipNotAboveCap));
    BOOST_CHECK(checkSet(TxKind::Web3SetCode, AdmissionContext::PoolAdmission) ==
                (legacy | Check::TypeByRevision | Check::TipNotAboveCap | Check::SetCodeHasTo |
                    Check::AuthListNonEmpty));
    BOOST_CHECK(checkSet(TxKind::Rejected, AdmissionContext::PoolAdmission) == Check::TypeGate);
}

// The other two columns are derived, not written out. These are the derivation rules.
BOOST_AUTO_TEST_CASE(derivedColumnsHoldForEveryKind)
{
    for (auto kind : kKinds)
    {
        const auto pool = checkSet(kind, AdmissionContext::PoolAdmission);

        BOOST_CHECK(checkSet(kind, AdmissionContext::EESTReplay) ==
                    (pool & ~(Check::Balance | Check::Web3NonceWindow)));

        // Proposal verification is a subset -- it may never check something pool admission does
        // not, or a proposal could fail for a reason a directly-submitted transaction survives.
        const auto proposal = checkSet(kind, AdmissionContext::ProposalVerification);
        BOOST_CHECK((proposal & ~pool) == Check::None);

        for (auto context : kContexts)
        {
            BOOST_CHECK(effectiveCheckSet(kind, context, SignaturePolicy::Required) ==
                        checkSet(kind, context));
            BOOST_CHECK(effectiveCheckSet(kind, context, SignaturePolicy::Disabled) ==
                        (checkSet(kind, context) & ~(Check::Signature | c_senderDependent)));
        }
    }
}

// Reverse assertion 1 (the P0 guard). Under Required, every real kind must actually check the
// signature. An earlier design derived this from Transaction::tainted(), which defaults to true
// and means "must be verified" -- reading it as "already verified" made every ordinary
// eth_sendRawTransaction skip signature recovery entirely.
BOOST_AUTO_TEST_CASE(requiredPolicyAlwaysChecksTheSignature)
{
    for (auto kind : kKinds)
    {
        if (kind == TxKind::Rejected)
        {
            continue;  // TypeGate only, and it always fails
        }
        for (auto context : kContexts)
        {
            BOOST_CHECK_MESSAGE(
                contains(
                    effectiveCheckSet(kind, context, SignaturePolicy::Required), Check::Signature),
                "Signature missing for kind " << static_cast<int>(kind) << " context "
                                              << static_cast<int>(context));
        }
    }
}

// Reverse assertion 2. Turning signature verification off must not disable BCOS replay
// protection: nonce/blockLimit for a BCOS transaction need no sender.
BOOST_AUTO_TEST_CASE(disabledPolicyKeepsBcosReplayProtection)
{
    for (auto context : kContexts)
    {
        BOOST_CHECK(contains(effectiveCheckSet(TxKind::Bcos, context, SignaturePolicy::Disabled),
            Check::BcosPoolNonce));
    }
    // ... and BcosPoolNonce must not be classified as sender-dependent in the first place.
    BOOST_CHECK(!contains(c_senderDependent, Check::BcosPoolNonce));
}

BOOST_AUTO_TEST_CASE(disabledPolicyDropsEverySenderDependentCheck)
{
    for (auto kind : kKinds)
    {
        for (auto context : kContexts)
        {
            const auto effective = effectiveCheckSet(kind, context, SignaturePolicy::Disabled);
            BOOST_CHECK(!contains(effective, Check::Signature));
            BOOST_CHECK((effective & c_senderDependent) == Check::None);
            // Checks that need no sender still run.
            const auto declared = checkSet(kind, context);
            BOOST_CHECK(
                contains(effective, Check::TypeGate) == contains(declared, Check::TypeGate));
            BOOST_CHECK(contains(effective, Check::ChainId) == contains(declared, Check::ChainId));
            BOOST_CHECK(contains(effective, Check::IntrinsicGas) ==
                        contains(declared, Check::IntrinsicGas));
        }
    }
}

// A check that is a member of some set but absent from c_checkOrder would silently never run.
BOOST_AUTO_TEST_CASE(checkOrderCoversEveryReachableCheckExactlyOnce)
{
    std::set<uint32_t> seen;
    for (auto check : c_checkOrder)
    {
        BOOST_CHECK_MESSAGE(seen.insert(static_cast<uint32_t>(check)).second,
            "duplicate entry in c_checkOrder: " << static_cast<uint32_t>(check));
    }

    Check reachable = Check::None;
    for (auto kind : kKinds)
    {
        for (auto context : kContexts)
        {
            for (auto policy : kPolicies)
            {
                reachable = reachable | effectiveCheckSet(kind, context, policy);
            }
        }
    }
    for (auto check : c_checkOrder)
    {
        // Every ordered check is reachable from some cell -- an unreachable one is dead weight.
        BOOST_CHECK_MESSAGE(contains(reachable, check),
            "c_checkOrder lists an unreachable check: " << static_cast<uint32_t>(check));
    }
    // ... and every reachable check is ordered.
    Check ordered = Check::None;
    for (auto check : c_checkOrder)
    {
        ordered = ordered | check;
    }
    BOOST_CHECK((reachable & ~ordered) == Check::None);
}

// TypeGate must be first: for a refused envelope type nothing else is meaningful, and the type
// gate is what turns a blob or deposit arriving over P2P into a rejection.
BOOST_AUTO_TEST_CASE(typeGateIsEvaluatedFirstAndSignatureBeforeAccountState)
{
    BOOST_CHECK(c_checkOrder.front() == Check::TypeGate);

    const auto indexOf = [](Check target) {
        return std::distance(c_checkOrder.begin(), std::ranges::find(c_checkOrder, target));
    };
    const auto signature = indexOf(Check::Signature);
    for (auto dependent :
        {Check::SenderIsEOA, Check::NonceNotMax, Check::Web3NonceWindow, Check::Balance})
    {
        BOOST_CHECK_MESSAGE(
            signature < indexOf(dependent), "sender-dependent check ordered before Signature");
    }
    // One config read is cheaper than an account read.
    BOOST_CHECK(indexOf(Check::ChainId) < indexOf(Check::Balance));
}

// The proposal column is what a malicious leader's proposal is judged against, so every check
// whose answer is a function of the transaction and the chain config has to survive it: those
// are identical on every honest node, and enforcing them cannot split a block.
BOOST_AUTO_TEST_CASE(proposalVerificationKeepsProtocolInvariants)
{
    const auto pool = checkSet(TxKind::Web3DynamicFee, AdmissionContext::PoolAdmission);
    const auto proposal = checkSet(TxKind::Web3DynamicFee, AdmissionContext::ProposalVerification);

    // Exactly three come off, and no more.
    BOOST_CHECK((pool & ~proposal) == (Check::Balance | Check::SenderIsEOA | Check::NonceNotMax));

    for (auto kind : kKinds)
    {
        const auto set = checkSet(kind, AdmissionContext::ProposalVerification);
        BOOST_CHECK(contains(set, Check::TypeGate));
        // Balance is the one check whose answer depends on where in the block a transaction
        // sits: one funded by an earlier transaction in the same proposal fails it.
        BOOST_CHECK(!contains(set, Check::Balance));
    }

    // ChainId above all: nothing downstream re-checks it. EthereumTransition.h states plainly
    // that validate_transaction does not look at tx.chain_id, so dropping it here would let a
    // leader have the whole network execute a transaction signed for a different chain.
    BOOST_CHECK(contains(proposal, Check::ChainId));
    BOOST_CHECK(contains(proposal, Check::IntrinsicGas));
    BOOST_CHECK(contains(proposal, Check::TypeByRevision));
    BOOST_CHECK(contains(proposal, Check::FeeCapVsBaseFee));
    BOOST_CHECK(contains(proposal, Check::InitCodeSize));
    BOOST_CHECK(contains(proposal, Check::MaxGasLimit));
}

BOOST_AUTO_TEST_CASE(eestReplayDropsOnlyBalanceAndNonceWindow)
{
    const auto pool = checkSet(TxKind::Web3DynamicFee, AdmissionContext::PoolAdmission);
    const auto eest = checkSet(TxKind::Web3DynamicFee, AdmissionContext::EESTReplay);
    BOOST_CHECK((pool & ~eest) == (Check::Balance | Check::Web3NonceWindow));
    // Everything else survives -- fixtures still have to be well-formed, correctly signed, and
    // on the right chain.
    BOOST_CHECK(contains(eest, Check::Signature));
    BOOST_CHECK(contains(eest, Check::ChainId));
    BOOST_CHECK(contains(eest, Check::IntrinsicGas));
    BOOST_CHECK(contains(eest, Check::TypeGate));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
