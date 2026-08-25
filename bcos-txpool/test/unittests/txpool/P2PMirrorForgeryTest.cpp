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
 * @file P2PMirrorForgeryTest.cpp
 * @brief The ORDER of normalize() inside the TransactionSync verification loop.
 *
 * NormalizeTest already covers what normalize() does. What it cannot cover is where it is
 * called, and that placement is the whole fix on the P2P path:
 *
 *   normalize()            <- must be here
 *   exists(tx->hash())     <- reads the wire-supplied hash for Web3 txs; a peer picks it
 *   clearSenderAndHash()   <- zeroes the wire hash
 *   verify()
 *
 * Moving normalize() after either of the next two lines re-opens the hole while leaving every
 * other test green, so the cases below assert the ordering property directly rather than the
 * end-to-end outcome.
 */

#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tx-validator/Normalize.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
constexpr std::string_view kForgeryRaw1559 =
    "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc"
    "16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb"
    "697baef842a00000000000000000000000000000000000000000000000000000000000000003a0000000000000"
    "0000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c"
    "189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741"
    "c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";

std::shared_ptr<bcostars::protocol::TransactionImpl> forgeryFixtureTx()
{
    auto raw = fromHexWithPrefix(kForgeryRaw1559);
    auto ref = bcos::ref(raw);
    rpc::Web3Transaction web3;
    BOOST_REQUIRE(codec::rlp::decode(ref, web3) == nullptr);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = web3.takeToTarsTransaction()]() mutable { return &inner; });
    crypto::Keccak256 hasher;
    tx->calculateHash(hasher);
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(P2PMirrorForgeryTest)

// The dedup-skip bypass. A peer sets extraTransactionHash to a hash the local pool already holds;
// the loop's exists() check hits and `continue`s, so the transaction is never verified and its
// forged mirror is never reconciled.
BOOST_AUTO_TEST_CASE(peerChosenHashCannotSurviveNormalization)
{
    auto tx = forgeryFixtureTx();
    const auto pooledHash =
        crypto::HashType{"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    auto& wire = tx->mutableInner().extraTransactionHash;
    wire.assign(pooledHash.begin(), pooledHash.end());

    // Before normalization hash() is exactly what the peer wrote -- this is what exists() would
    // have been asked about under the old ordering.
    BOOST_CHECK(tx->hash() == pooledHash);

    // Normalization runs first and rejects the claim outright.
    BOOST_CHECK(txvalidator::normalize(*tx) == TransactionStatus::InvalidSignature);
}

// With a truthful hash the transaction normalizes, and hash() then returns the recomputed value,
// so the exists() dedup below it still works -- it is just no longer peer-controlled.
BOOST_AUTO_TEST_CASE(honestTransactionStillDedupsAfterNormalization)
{
    auto tx = forgeryFixtureTx();
    const auto canonical = tx->hash();

    BOOST_CHECK(txvalidator::normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK(tx->hash() == canonical);
}

// The ordering constraint against clearSenderAndHash(), stated as an executable claim: once the
// wire hash has been zeroed, the comparison in normalize() has nothing to compare against and
// the very same forgery passes. This is what makes the call-site position load-bearing rather
// than incidental.
BOOST_AUTO_TEST_CASE(normalizingAfterClearSenderAndHashWouldMissTheForgery)
{
    auto tx = forgeryFixtureTx();
    tx->mutableInner().extraTransactionHash.assign(32, '\xaa');
    tx->mutableInner().data.to = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    // Correct order: caught.
    BOOST_CHECK(txvalidator::normalize(*tx) == TransactionStatus::InvalidSignature);

    // Wrong order: clearSenderAndHash() first drops the peer's claim, so there is nothing left
    // to contradict and the forged transaction is accepted.
    tx->clearSenderAndHash();
    BOOST_CHECK(txvalidator::normalize(*tx) == TransactionStatus::None);
}

// The mirror rewrite itself, on the P2P path: signature and envelope untouched, recipient
// redirected. Normalization puts the signed recipient back.
BOOST_AUTO_TEST_CASE(redirectedRecipientIsRestoredFromTheEnvelope)
{
    auto tx = forgeryFixtureTx();
    const auto honestTo = tx->inner().data.to;
    const auto signature = tx->inner().signature;
    tx->mutableInner().data.to = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    BOOST_CHECK(txvalidator::normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.to, honestTo);
    BOOST_CHECK(tx->inner().signature == signature);
}

// A blob envelope arriving over P2P. The RPC entry and the mempool both gate on this, but the
// sync path had no type gate at all.
BOOST_AUTO_TEST_CASE(blobEnvelopeOverP2PIsRejected)
{
    auto tx = forgeryFixtureTx();
    tx->mutableInner().extraTransactionBytes[0] = static_cast<tars::Char>(0x03);
    BOOST_CHECK(txvalidator::normalize(*tx) == TransactionStatus::TxTypeNotSupported);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
