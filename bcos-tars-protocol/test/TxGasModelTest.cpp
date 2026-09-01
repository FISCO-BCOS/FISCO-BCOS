/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// The whole reason this formula lives in bcos-framework is that execution and admission must
// compute it identically, and its cells move with hard forks. Nothing pinned any of them, so a
// silent drift in the base cost, the create cost, the Istanbul token multiplier, the access-list
// prices, the Shanghai initcode word cost or the EIP-7623 floor would have been invisible.
//
// The `to` decoding is already pinned by toDecodingOnlyWellFormedAddresses in
// transaction-scheduler/tests and is not duplicated here.
//
// The authorization cases are the sharper ones. The formula reads tx.authorizationList(), which
// on the tars implementation is an
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

constexpr int64_t BASE_COST = 21000;
constexpr int64_t CREATE_COST = 32000;
constexpr const char* SOME_ADDRESS = "0102030405060708090a0b0c0d0e0f1011121314";

/// A Web3 transaction carrying `entries` authorization mirrors and declaring `typedKind`.
/// Deliberately unsigned: compute_tx_intrinsic_cost reads to/input/accessList/authList/kind and
/// never touches the signature, which is the whole point -- the mirror is what a peer controls.
std::shared_ptr<TransactionImpl> makeTx(uint8_t typedKind, size_t entries)
{
    auto tx = std::make_shared<TransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.type = static_cast<tars::Char>(WEB3_OUTER_TYPE);
    inner.web3TypedTxKind = static_cast<tars::Char>(typedKind);
    inner.data.to = SOME_ADDRESS;
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

/// A call with no calldata, access list or authorizations: the bare base cost.
std::shared_ptr<TransactionImpl> makeCall()
{
    return makeTx(2, 0);
}

/// Same, but with no `to` -- contract creation.
std::shared_ptr<TransactionImpl> makeCreate()
{
    auto tx = makeCall();
    tx->mutableInner().data.to.clear();
    return tx;
}

int64_t intrinsicOf(evmc_revision rev, TransactionImpl const& tx)
{
    return compute_tx_intrinsic_cost(rev, tx).intrinsic;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TxGasModelTest)

BOOST_AUTO_TEST_CASE(bareCallIsTheBaseCost)
{
    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_PRAGUE, *makeCall()), BASE_COST);
}

// TX_CREATE_COST arrives at Homestead and never leaves.
BOOST_AUTO_TEST_CASE(createCostIsGatedOnHomestead)
{
    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_FRONTIER, *makeCreate()), BASE_COST);
    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_HOMESTEAD, *makeCreate()), BASE_COST + CREATE_COST);
}

// A data token is 4 gas. A zero byte is one token; a non-zero byte is 17 before Istanbul
// (EIP-2028) and 4 after -- so 68 gas per non-zero byte drops to 16.
BOOST_AUTO_TEST_CASE(dataTokenMultiplierChangesAtIstanbul)
{
    auto tx = makeCall();
    tx->mutableInner().data.input = {0x00, 0x01};  // one zero byte, one non-zero

    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_PETERSBURG, *tx), BASE_COST + (1 + 17) * 4);
    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_ISTANBUL, *tx), BASE_COST + (1 + 4) * 4);
}

// EIP-2930: 2400 per address, 1900 per storage key.
BOOST_AUTO_TEST_CASE(accessListIsPricedPerAddressAndKey)
{
    auto tx = makeCall();
    bcostars::Web3AccessListEntry entry;
    entry.account = SOME_ADDRESS;
    entry.storageKeys.emplace_back(32, 0x01);
    entry.storageKeys.emplace_back(32, 0x02);
    tx->mutableInner().data.accessList.emplace_back(std::move(entry));

    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_BERLIN, *tx), BASE_COST + 2400 + 2 * 1900);
}

// EIP-3860: 2 gas per initcode word, creation only, Shanghai onwards.
BOOST_AUTO_TEST_CASE(initcodeWordCostIsGatedOnShanghaiAndCreation)
{
    auto create = makeCreate();
    create->mutableInner().data.input.assign(64, 0x01);  // 2 words, all non-zero
    auto const dataCost = 64 * 4 * 4;

    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_PARIS, *create), BASE_COST + CREATE_COST + dataCost);
    BOOST_CHECK_EQUAL(
        intrinsicOf(EVMC_SHANGHAI, *create), BASE_COST + CREATE_COST + dataCost + 2 * 2);

    // Not charged for a call, at any revision.
    auto call = makeCall();
    call->mutableInner().data.input.assign(64, 0x01);
    BOOST_CHECK_EQUAL(intrinsicOf(EVMC_SHANGHAI, *call), BASE_COST + dataCost);
}

// EIP-7623: the floor is a separate output, zero before Prague.
BOOST_AUTO_TEST_CASE(minCostIsTheEip7623FloorFromPrague)
{
    auto tx = makeCall();
    tx->mutableInner().data.input = {0x00, 0x01};  // 1 + 4 = 5 tokens at Istanbul+

    BOOST_CHECK_EQUAL(compute_tx_intrinsic_cost(EVMC_CANCUN, *tx).min, 0);
    BOOST_CHECK_EQUAL(compute_tx_intrinsic_cost(EVMC_PRAGUE, *tx).min, BASE_COST + 5 * 10);
}

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
