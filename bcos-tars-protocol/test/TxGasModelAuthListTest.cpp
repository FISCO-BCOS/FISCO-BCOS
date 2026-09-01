/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// The intrinsic-gas formula reads tx.authorizationList(), which on the tars implementation is an
// unauthenticated mirror: the Web3 signature binds extraTransactionBytes only, and nothing
// rebuilds data.authorizationList from it before pricing. evmone can charge the authorization
// cost with no gate because its authorization_list is a decoded field that only the type-4 RLP
// form populates. These cases pin the gate that restates that precondition here.
//
// They live in bcos-tars-protocol rather than next to the header because bcos-framework is the
// interface library: it has no Transaction it could instantiate.

#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-framework/protocol/TxGasModel.h>
#include <boost/test/unit_test.hpp>

using namespace bcostars::protocol;
using bcos::protocol::gas::compute_tx_intrinsic_cost;

namespace bcos::test
{
namespace
{
constexpr int64_t AUTH_COST = 25000;
constexpr uint8_t WEB3_OUTER_TYPE = 1;  // TransactionType::Web3Transaction

/// A Web3 transaction carrying `entries` authorization mirrors and declaring `typedKind`.
/// Deliberately unsigned: compute_tx_intrinsic_cost reads to/input/accessList/authList/kind and
/// never touches the signature, which is the whole point -- the mirror is what a peer controls.
std::shared_ptr<TransactionImpl> makeTx(uint8_t typedKind, size_t entries)
{
    auto tx = std::make_shared<TransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.type = static_cast<tars::Char>(WEB3_OUTER_TYPE);
    inner.web3TypedTxKind = static_cast<tars::Char>(typedKind);
    inner.data.to = "0102030405060708090a0b0c0d0e0f1011121314";
    for (size_t i = 0; i < entries; ++i)
    {
        bcostars::AuthorizationEntry entry;
        entry.chainID = 1;
        entry.address = "00000000000000000000000000000000000000ff";
        entry.nonce = 0;
        entry.r = "0x01";
        entry.s = "0x02";
        entry.v = 0;
        inner.data.authorizationList.emplace_back(std::move(entry));
    }
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TxGasModelAuthListTest)

// A set-code transaction is what evmone would price, so the cost must be charged in full.
BOOST_AUTO_TEST_CASE(setCodeTransactionIsChargedPerEntry)
{
    auto const none = compute_tx_intrinsic_cost(EVMC_PRAGUE, *makeTx(4, 0)).intrinsic;
    auto const three = compute_tx_intrinsic_cost(EVMC_PRAGUE, *makeTx(4, 3)).intrinsic;

    BOOST_CHECK_EQUAL(three - none, 3 * AUTH_COST);
}

// The finding: a legacy / access-list / 1559 envelope cannot carry authorizations, so a mirror
// claiming otherwise must not move the price. Ungated, this charged 3 * 25000 and could push an
// otherwise valid transaction into IntrinsicGasTooLow.
BOOST_AUTO_TEST_CASE(stuffedAuthListOnANonSetCodeKindIsNotCharged)
{
    for (uint8_t kind : {0, 1, 2})
    {
        auto const empty = compute_tx_intrinsic_cost(EVMC_PRAGUE, *makeTx(kind, 0)).intrinsic;
        auto const stuffed = compute_tx_intrinsic_cost(EVMC_PRAGUE, *makeTx(kind, 3)).intrinsic;

        BOOST_CHECK_EQUAL(stuffed, empty);
    }
}

// Set-code transactions do not exist before Prague, and both pipelines reject them earlier than
// pricing. Pinned anyway: this header is shared, and it should not inherit either caller's
// evaluation order as an assumption.
BOOST_AUTO_TEST_CASE(setCodeAuthListIsNotChargedBeforePrague)
{
    auto const empty = compute_tx_intrinsic_cost(EVMC_CANCUN, *makeTx(4, 0)).intrinsic;
    auto const stuffed = compute_tx_intrinsic_cost(EVMC_CANCUN, *makeTx(4, 3)).intrinsic;

    BOOST_CHECK_EQUAL(stuffed, empty);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
