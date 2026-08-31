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
 * @file NormalizeTest.cpp
 * @brief Field-by-field forgery of the tars mirror on a genuinely signed transaction.
 *
 * Every case starts from a real signed raw transaction, projects it to tars exactly the way the
 * RPC ingress path does, then rewrites ONE mirror field -- which is precisely what a malicious
 * peer can do over P2P while leaving the signature and the signed envelope untouched.
 *
 * These cases cannot be written through the RPC entry point: a transaction that arrives there
 * has its mirror built from the envelope, so mirror and envelope agree by construction and there
 * is nothing to detect. The tars struct has to be built directly.
 */

#include "bcos-tx-validator/Normalize.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txvalidator;

namespace bcos::test
{
namespace
{
// Real signed transactions (same vectors the RLP suite round-trips).
// EIP-1559, chainId 5, carries a non-empty accessList and an authorization-free payload.
constexpr std::string_view kRaw1559 =
    "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc"
    "16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb"
    "697baef842a00000000000000000000000000000000000000000000000000000000000000003a0000000000000"
    "0000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c"
    "189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741"
    "c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
// Legacy EIP-155.
constexpr std::string_view kRawLegacy =
    "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000afa905"
    "9cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1d78b58c40002"
    "6a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690516512020171c1ec8"
    "70f6ff45398cc8609250326be89915fb538e7bd718";

/// Project a signed raw transaction to tars the way the RPC ingress does, then fill
/// extraTransactionHash with the canonical hash (what calculateHash does on that path).
std::shared_ptr<bcostars::protocol::TransactionImpl> makeSignedTx(std::string_view rawHex)
{
    auto raw = fromHexWithPrefix(rawHex);
    auto ref = bcos::ref(raw);
    rpc::Web3Transaction web3;
    auto err = codec::rlp::decode(ref, web3);
    BOOST_REQUIRE_MESSAGE(err == nullptr, "fixture must decode");

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = web3.takeToTarsTransaction()]() mutable { return &inner; });
    crypto::Keccak256 hasher;
    tx->calculateHash(hasher);
    BOOST_REQUIRE(!tx->inner().extraTransactionHash.empty());
    return tx;
}

std::string hexOf(std::vector<tars::Char> const& v)
{
    return toHex(std::string_view{v.data(), v.size()});
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NormalizeTest)

// ---------------------------------------------------------------- mirror rewrites

BOOST_AUTO_TEST_CASE(forgedRecipientIsReplacedByTheSignedOne)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestTo = tx->inner().data.to;
    BOOST_REQUIRE(!honestTo.empty());

    // The attack: keep signature and envelope, redirect the funds.
    tx->mutableInner().data.to = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.to, honestTo);
}

BOOST_AUTO_TEST_CASE(forgedValueIsReplacedByTheSignedOne)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestValue = tx->inner().data.value;
    tx->mutableInner().data.value = "0xffffffffffffffffffffffff";

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.value, honestValue);
}

BOOST_AUTO_TEST_CASE(forgedCalldataIsReplacedByTheSignedOne)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestInput = hexOf(tx->inner().data.input);
    tx->mutableInner().data.input = {'\xca', '\xfe'};

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(hexOf(tx->inner().data.input), honestInput);
}

BOOST_AUTO_TEST_CASE(forgedNonceAndGasLimitAreReplacedByTheSignedOnes)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestNonce = tx->inner().data.nonce;
    const auto honestGasLimit = tx->inner().data.gasLimit;
    tx->mutableInner().data.nonce = "0x7fffffff";
    tx->mutableInner().data.gasLimit = 1;

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.nonce, honestNonce);
    BOOST_CHECK_EQUAL(tx->inner().data.gasLimit, honestGasLimit);
}

BOOST_AUTO_TEST_CASE(forgedFeeFieldsAreReplacedByTheSignedOnes)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestCap = tx->inner().data.maxFeePerGas;
    const auto honestTip = tx->inner().data.maxPriorityFeePerGas;
    tx->mutableInner().data.maxFeePerGas = "0x1";
    tx->mutableInner().data.maxPriorityFeePerGas = "0x1";

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.maxFeePerGas, honestCap);
    BOOST_CHECK_EQUAL(tx->inner().data.maxPriorityFeePerGas, honestTip);
}

// Issue #5364: the canonical txHash does not commit to data.accessList, so a peer can rewrite it
// and change which slots are pre-warmed under EIP-2929 -- gas divergence between nodes.
BOOST_AUTO_TEST_CASE(forgedAccessListIsReplacedByTheSignedOne)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestSize = tx->inner().data.accessList.size();
    BOOST_REQUIRE_GT(honestSize, 0U);  // the fixture must actually carry one

    bcostars::Web3AccessListEntry forged;
    forged.account = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    tx->mutableInner().data.accessList = {forged};

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.accessList.size(), honestSize);
    BOOST_CHECK(tx->inner().data.accessList[0].account != forged.account);
}

// The other half of #5364: an older peer that simply never populated the mirror field. Rebuilding
// restores it; a field-by-field "mirror must equal envelope, else reject" check would drop the
// transaction instead.
BOOST_AUTO_TEST_CASE(strippedAccessListIsRestoredNotRejected)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto honestSize = tx->inner().data.accessList.size();
    tx->mutableInner().data.accessList.clear();

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.accessList.size(), honestSize);
}

// Mis-declaring a 1559 envelope as legacy would dodge every check keyed on the mirror kind
// (tip-vs-cap above all).
BOOST_AUTO_TEST_CASE(forgedTypedKindIsReplacedByTheSignedOne)
{
    auto tx = makeSignedTx(kRaw1559);
    BOOST_REQUIRE_EQUAL(static_cast<int>(tx->inner().web3TypedTxKind), 2);
    tx->mutableInner().web3TypedTxKind = 0;

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(static_cast<int>(tx->inner().web3TypedTxKind), 2);
}

// attribute is the one unsigned outer field with execution consequences: BlockExecutive routes on
// LIQUID_SCALE_CODEC / LIQUID_CREATE and schedules on DAG.
BOOST_AUTO_TEST_CASE(forgedAttributeIsZeroed)
{
    auto tx = makeSignedTx(kRaw1559);
    tx->mutableInner().attribute =
        static_cast<int32_t>(Transaction::Attribute::LIQUID_SCALE_CODEC) |
        static_cast<int32_t>(Transaction::Attribute::LIQUID_CREATE) |
        static_cast<int32_t>(Transaction::Attribute::DAG);

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().attribute, 0);
}

// The rest of the unsigned surface, in one case. attribute above has execution consequences and
// earns its own test; these do not individually, but together they are the whole argument for
// moving the `data` sub-struct wholesale instead of assigning field by field -- the fields a
// Web3 envelope never carries have to come back as defaults, and nothing else pins that. A later
// "optimisation" into field-by-field assignment would leave every one of them standing, which is
// exactly how #5364 happened.
BOOST_AUTO_TEST_CASE(unsignedLeftoverFieldsAreAllReset)
{
    auto tx = makeSignedTx(kRaw1559);
    auto& inner = tx->mutableInner();

    // Outer fields, cleared explicitly by the commit step.
    inner.extraData = "leftover";
    inner.dataHash.assign(32, 0x11);
    inner.sourceHash = "0xdeadbeef";
    inner.mint = "1000000";
    inner.isSystemTransaction = 1;

    // TransactionData fields a Web3 envelope has no slot for: these are reset only as a
    // consequence of replacing the whole sub-struct.
    inner.data.groupID = "forged-group";
    inner.data.blockLimit = 12345;
    inner.data.abi = "[{\"forged\":true}]";
    inner.data.extension.assign(8, 0x22);
    inner.data.version = 7;
    inner.data.maxFeePerBlobGas = "999";
    // Carried only for legacy/2930 envelopes; kRaw1559 writes maxFeePerGas instead.
    inner.data.gasPrice = "0xdeadbeef";
    inner.data.blobVersionedHashes.emplace_back(32, 0x33);

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);

    auto const& out = tx->inner();
    BOOST_CHECK(out.extraData.empty());
    BOOST_CHECK(out.dataHash.empty());
    BOOST_CHECK(out.sourceHash.empty());
    BOOST_CHECK(out.mint.empty());
    BOOST_CHECK_EQUAL(out.isSystemTransaction, 0);

    BOOST_CHECK(out.data.groupID.empty());
    BOOST_CHECK_EQUAL(out.data.blockLimit, 0);
    BOOST_CHECK(out.data.abi.empty());
    BOOST_CHECK(out.data.extension.empty());
    BOOST_CHECK_EQUAL(out.data.version, 0);
    BOOST_CHECK(out.data.maxFeePerBlobGas.empty());
    BOOST_CHECK(out.data.gasPrice.empty());
    BOOST_CHECK(out.data.blobVersionedHashes.empty());
}

// ---------------------------------------------------------------- hash commitment

BOOST_AUTO_TEST_CASE(forgedWireHashIsRejected)
{
    auto tx = makeSignedTx(kRaw1559);
    // Claiming some other transaction's hash lets a peer slip this one past a hash-keyed
    // dedup/skip check (TransactionSync consults exists(tx->hash()) before verifying).
    tx->mutableInner().extraTransactionHash.assign(32, '\x11');

    BOOST_CHECK(normalize(*tx) == TransactionStatus::InvalidSignature);
}

BOOST_AUTO_TEST_CASE(absentWireHashIsFilledInNotRejected)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto canonical = tx->inner().extraTransactionHash;
    tx->mutableInner().extraTransactionHash.clear();

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK(tx->inner().extraTransactionHash == canonical);
}

BOOST_AUTO_TEST_CASE(corruptedEnvelopeIsRejected)
{
    auto tx = makeSignedTx(kRaw1559);
    auto& payload = tx->mutableInner().extraTransactionBytes;
    payload.back() = static_cast<tars::Char>(payload.back() ^ 0x01);

    BOOST_CHECK(normalize(*tx) != TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(emptyEnvelopeOnAWeb3TransactionIsRejected)
{
    auto tx = makeSignedTx(kRaw1559);
    tx->mutableInner().extraTransactionBytes.clear();

    BOOST_CHECK(normalize(*tx) == TransactionStatus::Malformed);
}

// ---------------------------------------------------------------- preserved state

BOOST_AUTO_TEST_CASE(signatureSurvivesNormalization)
{
    // decodeFromPayload runs with withSignature=false, so the decoded payload carries no
    // signature at all. Anything that rebuilt the whole tars struct from it would destroy the
    // signature of every transaction it touched.
    auto tx = makeSignedTx(kRaw1559);
    const auto before = tx->inner().signature;
    BOOST_REQUIRE(!before.empty());
    tx->mutableInner().data.to = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK(tx->inner().signature == before);
}

BOOST_AUTO_TEST_CASE(localBookkeepingSurvivesNormalization)
{
    auto tx = makeSignedTx(kRaw1559);
    tx->setImportTime(4242);
    const auto envelope = tx->inner().extraTransactionBytes;
    const auto outerType = tx->inner().type;

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().importTime, 4242);
    BOOST_CHECK(tx->inner().extraTransactionBytes == envelope);
    BOOST_CHECK_EQUAL(tx->inner().type, outerType);
}

// A rejected transaction must come back untouched: the caller only receives a status code, so a
// half-rebuilt mirror would be invisible to it.
BOOST_AUTO_TEST_CASE(rejectedTransactionIsLeftUnmodified)
{
    auto tx = makeSignedTx(kRaw1559);
    const auto forgedTo = std::string{"0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"};
    tx->mutableInner().data.to = forgedTo;
    tx->mutableInner().extraTransactionHash.assign(32, '\x11');

    BOOST_REQUIRE(normalize(*tx) == TransactionStatus::InvalidSignature);
    // Still the forged value -- nothing was committed.
    BOOST_CHECK_EQUAL(tx->inner().data.to, forgedTo);
    BOOST_CHECK_EQUAL(hexOf(tx->inner().extraTransactionHash), std::string(64, '1'));
}

// ---------------------------------------------------------------- type gate

BOOST_AUTO_TEST_CASE(depositEnvelopeIsRejectedAsUnsupportedType)
{
    // A deposit reaching a pool is forged by construction: it has no signature, its trust anchor
    // is sourceHash, and no pool path checks that. Classification runs before the decode
    // precisely so this reports TxTypeNotSupported and not Malformed -- the decoder does not
    // know 0x7e.
    auto tx = makeSignedTx(kRaw1559);
    tx->mutableInner().extraTransactionBytes[0] = static_cast<tars::Char>(0x7e);

    BOOST_CHECK(normalize(*tx) == TransactionStatus::TxTypeNotSupported);
}

BOOST_AUTO_TEST_CASE(blobEnvelopeIsRejectedAsBlobTxNotAllowed)
{
    // The mirror image of the deposit case: 0x03 IS in bcos::rpc::TransactionType (EIP4844), so
    // the decoder would accept it. Only a gate ahead of the decode catches both. The code is
    // the dedicated one #5520 introduced for exactly this verdict, not the generic type gate --
    // both gates run the same classifier and must not disagree on what to report.
    auto tx = makeSignedTx(kRaw1559);
    tx->mutableInner().extraTransactionBytes[0] = static_cast<tars::Char>(0x03);

    BOOST_CHECK(normalize(*tx) == TransactionStatus::BlobTxNotAllowed);
}

BOOST_AUTO_TEST_CASE(reservedEnvelopeTypeIsRejectedAsMalformed)
{
    auto tx = makeSignedTx(kRaw1559);
    tx->mutableInner().extraTransactionBytes[0] = static_cast<tars::Char>(0x7f);

    BOOST_CHECK(normalize(*tx) == TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(unknownOuterTypeIsRejected)
{
    auto tx = makeSignedTx(kRaw1559);
    tx->mutableInner().type = static_cast<tars::Char>(42);

    BOOST_CHECK(normalize(*tx) == TransactionStatus::Malformed);
}

// ---------------------------------------------------------------- legacy / BCOS

BOOST_AUTO_TEST_CASE(legacyTransactionNormalizesAndDetectsForgery)
{
    auto tx = makeSignedTx(kRawLegacy);
    const auto honestTo = tx->inner().data.to;
    tx->mutableInner().data.to = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.to, honestTo);
}

BOOST_AUTO_TEST_CASE(bcosTransactionIsNotTouched)
{
    // A BCOS transaction's dataHash covers the whole TransactionData, so signature and execution
    // already read the same bytes; normalization must leave it completely alone.
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    tx->mutableInner().data.to = "0x1234567890123456789012345678901234567890";
    tx->mutableInner().data.groupID = "group0";
    tx->mutableInner().data.blockLimit = 500;
    tx->mutableInner().attribute = 4;

    BOOST_CHECK(normalize(*tx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(tx->inner().data.groupID, "group0");
    BOOST_CHECK_EQUAL(tx->inner().data.blockLimit, 500);
    BOOST_CHECK_EQUAL(tx->inner().attribute, 4);
}

BOOST_AUTO_TEST_SUITE_END()


}  // namespace bcos::test
