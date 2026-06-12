/**
 * @file Web3AccessListResolverTest.cpp
 * @brief Protocol-field and extra-bytes resolution for Web3 access lists (EIP-2930 / 1559).
 */

#include "../src/Web3AccessListResolver.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::rpc;

BOOST_AUTO_TEST_SUITE(Web3AccessListResolver)

BOOST_AUTO_TEST_CASE(parse_eip1559_from_tars_structured_access_list)
{
    // Same fixture as bcos-rpc Web3TypeTest::testEIP1559Transaction
    constexpr std::string_view rawTx =
        "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881b"
        "c16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4"
        "cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000"
        "000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb"
        "8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4"
        "741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);
    BOOST_CHECK(w3.type == TransactionType::EIP1559);
    BOOST_CHECK(!w3.accessList.empty());

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

    auto const resolved = resolveWeb3AccessList(txImpl);
    BOOST_CHECK_EQUAL(resolved.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP1559));
    BOOST_REQUIRE(resolved.accessList);
    BOOST_CHECK_EQUAL(resolved.accessList->size(), w3.accessList.size());
    BOOST_CHECK_EQUAL(txImpl.web3AccessList().size(), w3.accessList.size());
}

BOOST_AUTO_TEST_CASE(parse_eip1559_access_list_from_extra_when_tars_list_empty)
{
    constexpr std::string_view rawTx =
        "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881b"
        "c16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4"
        "cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000"
        "000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb"
        "8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4"
        "741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = ref(bytes);
    Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->data.accessList.clear();
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });
    BOOST_CHECK(txImpl.web3AccessList().empty());

    auto const resolved = resolveWeb3AccessList(txImpl);
    BOOST_CHECK_EQUAL(resolved.web3TypedTxKind, static_cast<uint8_t>(TransactionType::EIP1559));
    BOOST_REQUIRE(resolved.accessList);
    BOOST_CHECK(!resolved.accessList->empty());
}

BOOST_AUTO_TEST_SUITE_END()
