/**
 * @file Web3Eip7702FillTest.cpp
 * @brief Protocol-field and extra-bytes parsing for Web3 authorization lists (EIP-7702).
 */

#include "../src/Web3Eip7702Fill.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::rpc;

namespace
{
Web3Transaction makeSampleEip7702Tx()
{
    Web3Transaction w3;
    w3.type = TransactionType::EIP7702;
    w3.chainId = 7;
    w3.nonce = 42;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 2;
    w3.gasLimit = 21000;
    w3.to = Address("0x1111111111111111111111111111111111111111");
    w3.value = 0;
    w3.data = bytes{};
    w3.signatureR = bytes(32, 0x11);
    w3.signatureS = bytes(32, 0x22);
    w3.signatureV = 0;

    AuthorizationListEntry auth;
    auth.chainId = 7;
    auth.address = Address("0x2222222222222222222222222222222222222222");
    auth.nonce = 7;
    auth.yParity = 1;
    auth.r = h256(fromHex(std::string(64, 'a')));
    auth.s = h256(fromHex(std::string(64, 'b')));
    w3.authorizationList.push_back(auth);
    return w3;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3Eip7702Fill)

BOOST_AUTO_TEST_CASE(parse_eip7702_from_tars_structured_authorization_list)
{
    auto w3 = makeSampleEip7702Tx();
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

    auto const parsed = parseEip7702FromWeb3Transaction(txImpl);
    BOOST_CHECK_EQUAL(parsed.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP7702));
    BOOST_REQUIRE(parsed.authorizationList);
    BOOST_CHECK_EQUAL(parsed.authorizationList->size(), 1U);
    BOOST_CHECK_EQUAL(txImpl.web3AuthorizationList().size(), 1U);
    BOOST_CHECK_EQUAL((*parsed.authorizationList)[0].chainId, 7U);
    BOOST_CHECK_EQUAL((*parsed.authorizationList)[0].nonce, 7U);
}

BOOST_AUTO_TEST_CASE(parse_eip7702_authorization_list_from_extra_when_tars_list_empty)
{
    auto w3 = makeSampleEip7702Tx();
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    tarsHolder->web3TypedTxKind =
        static_cast<tars::Char>(static_cast<uint8_t>(TransactionType::EIP7702));
    tarsHolder->data.authorizationList.clear();
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(txImpl.web3AuthorizationList().empty());

    auto const parsed = parseEip7702FromWeb3Transaction(txImpl);
    BOOST_CHECK_EQUAL(parsed.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP7702));
    BOOST_REQUIRE(parsed.authorizationList);
    BOOST_CHECK_EQUAL(parsed.authorizationList->size(), 1U);
}

BOOST_AUTO_TEST_CASE(skip_tars_tuple_with_invalid_decimal_fields)
{
    auto w3 = makeSampleEip7702Tx();
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    tarsHolder->data.authorizationList[0].chainId = "not-a-number";
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

    auto const parsed = parseEip7702FromWeb3Transaction(txImpl);
    BOOST_CHECK_EQUAL(parsed.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP7702));
    BOOST_CHECK(!parsed.authorizationList || parsed.authorizationList->empty());
}

BOOST_AUTO_TEST_CASE(parse_eip7702_from_extra_when_tars_kind_zero_path_c)
{
    auto w3 = makeSampleEip7702Tx();
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    tarsHolder->web3TypedTxKind = 0;
    tarsHolder->data.authorizationList.clear();
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

    auto const parsed = parseEip7702FromWeb3Transaction(txImpl);
    BOOST_CHECK_EQUAL(parsed.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP7702));
    BOOST_REQUIRE(parsed.authorizationList);
    BOOST_CHECK_EQUAL(parsed.authorizationList->size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
