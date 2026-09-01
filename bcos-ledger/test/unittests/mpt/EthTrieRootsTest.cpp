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
 * @file EthTrieRootsTest.cpp
 * @brief Golden-vector tests for computeIndexedTrieRoot / calculateTransactionsRoot /
 *        calculateReceiptsRoot / calculateWithdrawalsRoot and calculateLogsBloom.
 *
 *        Expected roots are produced by an independent Python MPT reference implementation
 *        (real keccak-256 via pycryptodome; checked in at
 *        tools/opstack-genesis/gen_trieroot_golden.py), NOT by the C++ builder — so
 *        encode(key=rlp(index)) + sort + build are anchored to a second implementation.
 *        The empty-trie root is also pinned to the Ethereum constant.
 */
#include "bcos-ledger/mpt/EthTrieRoots.h"
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace bcos;
using namespace bcos::ledger::mpt;

namespace bcos::ledger::mpt::test
{
BOOST_AUTO_TEST_SUITE(EthTrieRootsSuite)

namespace
{
std::vector<bytesConstRef> refsOf(std::vector<bytes> const& items)
{
    std::vector<bytesConstRef> refs;
    refs.reserve(items.size());
    for (auto const& item : items)
    {
        refs.push_back(bcos::ref(item));
    }
    return refs;
}
}  // namespace

BOOST_AUTO_TEST_CASE(EmptyRoot)
{
    std::vector<bytes> none;
    auto refs = refsOf(none);
    BOOST_CHECK_EQUAL(computeIndexedTrieRoot(refs), mpt::emptyRootHash());
    BOOST_CHECK_EQUAL(mpt::emptyRootHash().hexPrefixed(),
        "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421");
}

BOOST_AUTO_TEST_CASE(TransactionsRoot)
{
    std::vector<bytes> txs;
    txs.push_back(
        fromHex("02e2018001825208809411111111111111111111111111111111111111118080c0010102"));
    txs.push_back(
        fromHex("03f8440180018252089422222222222222222222222222222222222222228080c001e1a00000000000"
                "000000000000000000000000000000000000000000000000000000010102"));
    txs.push_back(
        fromHex("e301843b9aca0082520894333333333333333333333333333333333333333380801b0304"));
    txs.push_back(fromHex("d102843b9aca00825208800582dead1c0506"));
    auto refs = refsOf(txs);
    auto root = calculateTransactionsRoot(refs);
    BOOST_CHECK_EQUAL(
        root.hexPrefixed(), "0xb0324982374a362506a47c4c22316dc9507c08b8f77cf8ee1f763ebbd1bc4806");
}

BOOST_AUTO_TEST_CASE(ReceiptsRoot)
{
    std::vector<bytes> receipts;
    receipts.push_back(fromHex(
        "02f9010801825208b9010000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000c0"));
    receipts.push_back(fromHex(
        "03f9010801825208b9010000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000c0"));
    receipts.push_back(fromHex(
        "f901068080b9010000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000c0"));
    receipts.push_back(fromHex(
        "01f901470182c350b9010000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000f83ef8"
        "3c941111111111111111111111111111111111111111e1a0222222222222222222222222222222222222222222"
        "222222222222222222222284deadbeef"));
    auto refs = refsOf(receipts);
    auto root = calculateReceiptsRoot(refs);
    BOOST_CHECK_EQUAL(
        root.hexPrefixed(), "0x2d82724250d2cb15d23a84a75c0ccce1c4fbd6032110e3be57eccbf68866353d");
}

BOOST_AUTO_TEST_CASE(WithdrawalsRoot)
{
    std::vector<bytes> withdrawals;
    withdrawals.push_back(fromHex("d8010294333333333333333333333333333333333333333304"));
    withdrawals.push_back(
        fromHex("de6481c894aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa85e8d4a51000"));
    withdrawals.push_back(fromHex("da8201000194bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb80"));
    auto refs = refsOf(withdrawals);
    auto root = calculateWithdrawalsRoot(refs);
    BOOST_CHECK_EQUAL(
        root.hexPrefixed(), "0xa42c3ab8f3dd29c60f0182050b7a77a8603d83e035368004b7450857d3cd172e");
}

// 200 items force 2-byte rlp keys (index >= 128): exercises the rlp-key sort order
// (rlp(0)=0x80 sorts after rlp(1)=0x01, and after rlp(128)=0x8180) and cross-length grouping.
BOOST_AUTO_TEST_CASE(ManyItemsTwoByteKeys)
{
    std::vector<bytes> items;
    items.push_back(fromHex("0000"));
    items.push_back(fromHex("0107"));
    items.push_back(fromHex("020e"));
    items.push_back(fromHex("0315"));
    items.push_back(fromHex("041c"));
    items.push_back(fromHex("0523"));
    items.push_back(fromHex("062a"));
    items.push_back(fromHex("0731"));
    items.push_back(fromHex("0838"));
    items.push_back(fromHex("093f"));
    items.push_back(fromHex("0a46"));
    items.push_back(fromHex("0b4d"));
    items.push_back(fromHex("0c54"));
    items.push_back(fromHex("0d5b"));
    items.push_back(fromHex("0e62"));
    items.push_back(fromHex("0f69"));
    items.push_back(fromHex("1070"));
    items.push_back(fromHex("1177"));
    items.push_back(fromHex("127e"));
    items.push_back(fromHex("1385"));
    items.push_back(fromHex("148c"));
    items.push_back(fromHex("1593"));
    items.push_back(fromHex("169a"));
    items.push_back(fromHex("17a1"));
    items.push_back(fromHex("18a8"));
    items.push_back(fromHex("19af"));
    items.push_back(fromHex("1ab6"));
    items.push_back(fromHex("1bbd"));
    items.push_back(fromHex("1cc4"));
    items.push_back(fromHex("1dcb"));
    items.push_back(fromHex("1ed2"));
    items.push_back(fromHex("1fd9"));
    items.push_back(fromHex("20e0"));
    items.push_back(fromHex("21e7"));
    items.push_back(fromHex("22ee"));
    items.push_back(fromHex("23f5"));
    items.push_back(fromHex("24fc"));
    items.push_back(fromHex("2503"));
    items.push_back(fromHex("260a"));
    items.push_back(fromHex("2711"));
    items.push_back(fromHex("2818"));
    items.push_back(fromHex("291f"));
    items.push_back(fromHex("2a26"));
    items.push_back(fromHex("2b2d"));
    items.push_back(fromHex("2c34"));
    items.push_back(fromHex("2d3b"));
    items.push_back(fromHex("2e42"));
    items.push_back(fromHex("2f49"));
    items.push_back(fromHex("3050"));
    items.push_back(fromHex("3157"));
    items.push_back(fromHex("325e"));
    items.push_back(fromHex("3365"));
    items.push_back(fromHex("346c"));
    items.push_back(fromHex("3573"));
    items.push_back(fromHex("367a"));
    items.push_back(fromHex("3781"));
    items.push_back(fromHex("3888"));
    items.push_back(fromHex("398f"));
    items.push_back(fromHex("3a96"));
    items.push_back(fromHex("3b9d"));
    items.push_back(fromHex("3ca4"));
    items.push_back(fromHex("3dab"));
    items.push_back(fromHex("3eb2"));
    items.push_back(fromHex("3fb9"));
    items.push_back(fromHex("40c0"));
    items.push_back(fromHex("41c7"));
    items.push_back(fromHex("42ce"));
    items.push_back(fromHex("43d5"));
    items.push_back(fromHex("44dc"));
    items.push_back(fromHex("45e3"));
    items.push_back(fromHex("46ea"));
    items.push_back(fromHex("47f1"));
    items.push_back(fromHex("48f8"));
    items.push_back(fromHex("49ff"));
    items.push_back(fromHex("4a06"));
    items.push_back(fromHex("4b0d"));
    items.push_back(fromHex("4c14"));
    items.push_back(fromHex("4d1b"));
    items.push_back(fromHex("4e22"));
    items.push_back(fromHex("4f29"));
    items.push_back(fromHex("5030"));
    items.push_back(fromHex("5137"));
    items.push_back(fromHex("523e"));
    items.push_back(fromHex("5345"));
    items.push_back(fromHex("544c"));
    items.push_back(fromHex("5553"));
    items.push_back(fromHex("565a"));
    items.push_back(fromHex("5761"));
    items.push_back(fromHex("5868"));
    items.push_back(fromHex("596f"));
    items.push_back(fromHex("5a76"));
    items.push_back(fromHex("5b7d"));
    items.push_back(fromHex("5c84"));
    items.push_back(fromHex("5d8b"));
    items.push_back(fromHex("5e92"));
    items.push_back(fromHex("5f99"));
    items.push_back(fromHex("60a0"));
    items.push_back(fromHex("61a7"));
    items.push_back(fromHex("62ae"));
    items.push_back(fromHex("63b5"));
    items.push_back(fromHex("64bc"));
    items.push_back(fromHex("65c3"));
    items.push_back(fromHex("66ca"));
    items.push_back(fromHex("67d1"));
    items.push_back(fromHex("68d8"));
    items.push_back(fromHex("69df"));
    items.push_back(fromHex("6ae6"));
    items.push_back(fromHex("6bed"));
    items.push_back(fromHex("6cf4"));
    items.push_back(fromHex("6dfb"));
    items.push_back(fromHex("6e02"));
    items.push_back(fromHex("6f09"));
    items.push_back(fromHex("7010"));
    items.push_back(fromHex("7117"));
    items.push_back(fromHex("721e"));
    items.push_back(fromHex("7325"));
    items.push_back(fromHex("742c"));
    items.push_back(fromHex("7533"));
    items.push_back(fromHex("763a"));
    items.push_back(fromHex("7741"));
    items.push_back(fromHex("7848"));
    items.push_back(fromHex("794f"));
    items.push_back(fromHex("7a56"));
    items.push_back(fromHex("7b5d"));
    items.push_back(fromHex("7c64"));
    items.push_back(fromHex("7d6b"));
    items.push_back(fromHex("7e72"));
    items.push_back(fromHex("7f79"));
    items.push_back(fromHex("8080"));
    items.push_back(fromHex("8187"));
    items.push_back(fromHex("828e"));
    items.push_back(fromHex("8395"));
    items.push_back(fromHex("849c"));
    items.push_back(fromHex("85a3"));
    items.push_back(fromHex("86aa"));
    items.push_back(fromHex("87b1"));
    items.push_back(fromHex("88b8"));
    items.push_back(fromHex("89bf"));
    items.push_back(fromHex("8ac6"));
    items.push_back(fromHex("8bcd"));
    items.push_back(fromHex("8cd4"));
    items.push_back(fromHex("8ddb"));
    items.push_back(fromHex("8ee2"));
    items.push_back(fromHex("8fe9"));
    items.push_back(fromHex("90f0"));
    items.push_back(fromHex("91f7"));
    items.push_back(fromHex("92fe"));
    items.push_back(fromHex("9305"));
    items.push_back(fromHex("940c"));
    items.push_back(fromHex("9513"));
    items.push_back(fromHex("961a"));
    items.push_back(fromHex("9721"));
    items.push_back(fromHex("9828"));
    items.push_back(fromHex("992f"));
    items.push_back(fromHex("9a36"));
    items.push_back(fromHex("9b3d"));
    items.push_back(fromHex("9c44"));
    items.push_back(fromHex("9d4b"));
    items.push_back(fromHex("9e52"));
    items.push_back(fromHex("9f59"));
    items.push_back(fromHex("a060"));
    items.push_back(fromHex("a167"));
    items.push_back(fromHex("a26e"));
    items.push_back(fromHex("a375"));
    items.push_back(fromHex("a47c"));
    items.push_back(fromHex("a583"));
    items.push_back(fromHex("a68a"));
    items.push_back(fromHex("a791"));
    items.push_back(fromHex("a898"));
    items.push_back(fromHex("a99f"));
    items.push_back(fromHex("aaa6"));
    items.push_back(fromHex("abad"));
    items.push_back(fromHex("acb4"));
    items.push_back(fromHex("adbb"));
    items.push_back(fromHex("aec2"));
    items.push_back(fromHex("afc9"));
    items.push_back(fromHex("b0d0"));
    items.push_back(fromHex("b1d7"));
    items.push_back(fromHex("b2de"));
    items.push_back(fromHex("b3e5"));
    items.push_back(fromHex("b4ec"));
    items.push_back(fromHex("b5f3"));
    items.push_back(fromHex("b6fa"));
    items.push_back(fromHex("b701"));
    items.push_back(fromHex("b808"));
    items.push_back(fromHex("b90f"));
    items.push_back(fromHex("ba16"));
    items.push_back(fromHex("bb1d"));
    items.push_back(fromHex("bc24"));
    items.push_back(fromHex("bd2b"));
    items.push_back(fromHex("be32"));
    items.push_back(fromHex("bf39"));
    items.push_back(fromHex("c040"));
    items.push_back(fromHex("c147"));
    items.push_back(fromHex("c24e"));
    items.push_back(fromHex("c355"));
    items.push_back(fromHex("c45c"));
    items.push_back(fromHex("c563"));
    items.push_back(fromHex("c66a"));
    items.push_back(fromHex("c771"));
    auto refs = refsOf(items);
    BOOST_CHECK_EQUAL(computeIndexedTrieRoot(refs).hexPrefixed(),
        "0x1e6f2f0fd22412f2ec4284e0c57f0b1032b6f578b1cb3fd64282eecec528a3a3");
}

BOOST_AUTO_TEST_CASE(LogsBloom)
{
    // 3 receipts with blooms: b0 = all-zero, b1 = 0x01 in byte 255, b2 = 0x80 in byte 0
    bcos::Bloom b0{};
    bcos::Bloom b1{};
    b1[255] = 0x01;
    bcos::Bloom b2{};
    b2[0] = 0x80;
    std::vector<bcos::Bloom> blooms = {b0, b1, b2};
    auto result = calculateLogsBloom(blooms);
    BOOST_CHECK_EQUAL(result[255], 0x01);
    BOOST_CHECK_EQUAL(result[0], 0x80);
    // OR identity: empty range -> zero bloom
    std::vector<bcos::Bloom> none;
    auto empty = calculateLogsBloom(none);
    for (auto byte : empty)
    {
        BOOST_CHECK_EQUAL(byte, 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ledger::mpt::test
