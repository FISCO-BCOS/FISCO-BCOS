/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TransactionImplEip7702Test.cpp
 * @brief TransactionImpl EIP-7702 authorizationList cache and holder sizing (spec §9.2).
 */

#include <bcos-framework/protocol/Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <boost/test/unit_test.hpp>
#include <functional>

namespace bcostars::test
{
namespace
{
std::shared_ptr<bcostars::Transaction> makeTarsEip7702Holder()
{
    auto holder = std::make_shared<bcostars::Transaction>();
    holder->type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    holder->web3TypedTxKind = static_cast<tars::Char>(4);  // EIP-7702

    Web3AuthorizationListEntry entry;
    entry.chainId = "9";
    entry.address = "cccccccccccccccccccccccccccccccccccccccc";
    entry.nonce = "2";
    entry.yParity = 0;
    entry.r.assign(32, 0xaa);
    entry.s.assign(32, 0xbb);
    holder->data.authorizationList.emplace_back(std::move(entry));
    return holder;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TransactionImplEip7702)

BOOST_AUTO_TEST_CASE(web3AuthorizationList_cacheReturnsStableReference)
{
    auto holder = makeTarsEip7702Holder();
    bcostars::protocol::TransactionImpl txImpl(
        std::function<bcostars::Transaction*()>([holder]() { return holder.get(); }));

    auto const& first = txImpl.web3AuthorizationList();
    auto const& second = txImpl.web3AuthorizationList();
    BOOST_REQUIRE_EQUAL(first.size(), 1U);
    BOOST_CHECK_EQUAL(&first, &second);
    BOOST_CHECK_EQUAL(first[0].chainIdDec, "9");
    BOOST_CHECK_EQUAL(first[0].nonceDec, "2");
    BOOST_CHECK_EQUAL(first[0].addressHex, "cccccccccccccccccccccccccccccccccccccccc");
}

BOOST_AUTO_TEST_CASE(transactionImpl_fitsAnyTransactionHolder)
{
    constexpr std::size_t kAnyTransactionMaxSize = 224;
    static_assert(sizeof(bcostars::protocol::TransactionImpl) <= kAnyTransactionMaxSize,
        "TransactionImpl must fit in AnyHolder<Transaction, 224>");
    BOOST_CHECK_LE(sizeof(bcostars::protocol::TransactionImpl), kAnyTransactionMaxSize);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcostars::test
