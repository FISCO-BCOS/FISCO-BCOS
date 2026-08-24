/**
 * @file Web3AccessListResolverTest.cpp
 * @brief Tars-field resolution for Web3 access lists (admission already matched RLP).
 */

#include "../src/Web3AccessListResolver.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/Web3TxConsistency.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::rpc;
using namespace bcostars::protocol;

namespace
{
constexpr std::string_view kEip1559RawTx =
    "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881b"
    "c16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4"
    "cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000"
    "000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb"
    "8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4"
    "741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";

/// Grow a typed signing preimage's outer list by one trailing RLP item (0x2a).
bcos::bytes appendTrailingItemToTypedPreimage(bcos::bytesConstRef preimage)
{
    BOOST_REQUIRE(!preimage.empty());
    auto const typeByte = preimage[0];
    BOOST_REQUIRE(typeByte > 0 && typeByte < bcos::codec::rlp::BYTES_HEAD_BASE);
    bcos::bytes buffer(preimage.begin() + 1, preimage.end());
    bcos::bytesRef cursor(buffer.data(), buffer.size());
    auto [error, header] = bcos::codec::rlp::decodeHeader(cursor);
    BOOST_REQUIRE(!error);
    BOOST_REQUIRE(header.isList);
    BOOST_REQUIRE_EQUAL(header.payloadLength, cursor.size());

    constexpr bcos::byte trailing = 0x2a;
    bcos::bytes out;
    out.push_back(typeByte);
    bcos::codec::rlp::encodeHeader(
        out, bcos::codec::rlp::Header{.isList = true, .payloadLength = header.payloadLength + 1});
    out.insert(out.end(), cursor.begin(), cursor.end());
    out.push_back(trailing);
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3AccessListResolver)

BOOST_AUTO_TEST_CASE(parse_eip1559_from_tars_structured_access_list)
{
    auto bytes = fromHexWithPrefix(kEip1559RawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);
    BOOST_CHECK(w3.type == TransactionType::EIP1559);
    BOOST_CHECK(!w3.accessList.empty());

    auto tarsHolder = std::make_shared<bcostars::Transaction>(takeToTarsTransaction(w3));
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

    BOOST_CHECK(web3TarsFieldsMatchSignedExtra(txImpl));

    auto const resolved = resolveWeb3AccessList(txImpl);
    BOOST_CHECK_EQUAL(resolved.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP1559));
    BOOST_REQUIRE(resolved.accessList);
    BOOST_CHECK_EQUAL(resolved.accessList->size(), w3.accessList.size());
}

BOOST_AUTO_TEST_CASE(reject_tars_access_list_mismatch_at_admission)
{
    auto bytes = fromHexWithPrefix(kEip1559RawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);

    auto tarsHolder = std::make_shared<bcostars::Transaction>(takeToTarsTransaction(w3));
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->data.accessList.clear();
    bcostars::Web3AccessListEntry forged;
    forged.account = "1111111111111111111111111111111111111111";
    tarsHolder->data.accessList.emplace_back(std::move(forged));

    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(!web3TarsFieldsMatchSignedExtra(txImpl));
}

BOOST_AUTO_TEST_CASE(reject_tars_kind_spoof_at_admission)
{
    auto bytes = fromHexWithPrefix(kEip1559RawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);

    auto tarsHolder = std::make_shared<bcostars::Transaction>(takeToTarsTransaction(w3));
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->web3TypedTxKind =
        static_cast<tars::Char>(static_cast<uint8_t>(TransactionType::EIP2930));

    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(!web3TarsFieldsMatchSignedExtra(txImpl));
}

BOOST_AUTO_TEST_CASE(legacy_web3_rejects_forged_tars_access_list)
{
    // Legacy (type 0) fixture from Web3TypeTest::testLegacyTransactionDecode
    constexpr std::string_view rawTx =
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000afa90"
        "59cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1d78b58c400"
        "026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690516512020171c1"
        "ec870f6ff45398cc8609250326be89915fb538e7bd718";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(w3.type), 0);
    BOOST_CHECK(w3.accessList.empty());

    auto tarsHolder = std::make_shared<bcostars::Transaction>(takeToTarsTransaction(w3));
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    BOOST_CHECK_EQUAL(tarsHolder->web3TypedTxKind, 0);

    bcostars::protocol::TransactionImpl clean([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(web3TarsFieldsMatchSignedExtra(clean));
    auto const resolved = resolveWeb3AccessList(clean);
    BOOST_CHECK_EQUAL(resolved.web3TypedTxKind, 0);
    BOOST_CHECK(!resolved.accessList);

    // Forge a Tars access list on a legacy tx — must be rejected at admission.
    bcostars::Web3AccessListEntry forged;
    forged.account = "1111111111111111111111111111111111111111";
    tarsHolder->data.accessList.emplace_back(std::move(forged));
    bcostars::protocol::TransactionImpl poisoned([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(!web3TarsFieldsMatchSignedExtra(poisoned));
}

BOOST_AUTO_TEST_CASE(reject_typed_signing_preimage_with_trailing_items)
{
    auto bytes = fromHexWithPrefix(kEip1559RawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);

    auto const cleanPreimage = w3.encodeForSign();
    bcos::bytes cleanCopy(cleanPreimage.begin(), cleanPreimage.end());
    bcos::bytesRef cleanRef(cleanCopy.data(), cleanCopy.size());
    Web3Transaction decoded{};
    BOOST_REQUIRE(bcos::codec::rlp::decodeFromPayload(cleanRef, decoded) == nullptr);

    auto poisonedPreimage = appendTrailingItemToTypedPreimage(ref(cleanPreimage));
    bcos::bytesRef poisonedRef(poisonedPreimage.data(), poisonedPreimage.size());
    Web3Transaction junk{};
    auto const err = bcos::codec::rlp::decodeFromPayload(poisonedRef, junk);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK(err->errorMessage().find("trailing") != std::string::npos);

    auto tarsHolder = std::make_shared<bcostars::Transaction>(takeToTarsTransaction(w3));
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->extraTransactionBytes.assign(poisonedPreimage.begin(), poisonedPreimage.end());
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(!web3TarsFieldsMatchSignedExtra(txImpl));
}

BOOST_AUTO_TEST_CASE(returns_empty_for_non_web3_transaction)
{
    auto tarsHolder = std::make_shared<bcostars::Transaction>();
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK_EQUAL(
        txImpl.type(), static_cast<uint8_t>(bcos::protocol::TransactionType::BCOSTransaction));

    auto const resolved = resolveWeb3AccessList(txImpl);
    BOOST_CHECK_EQUAL(resolved.web3TypedTxKind, 0);
    BOOST_CHECK(!resolved.accessList);
    BOOST_CHECK(web3TarsFieldsMatchSignedExtra(txImpl));
}

BOOST_AUTO_TEST_SUITE_END()
