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
 * @file AccountTest.cpp
 * @brief Unit tests for Account 4-tuple RLP encode/decode (spec §5.4)
 */

#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

#include "bcos-ledger/test/unittests/ExceptionCheck.h"

namespace bcos::ledger::mpt::test
{
using bcos::test::errinfoContains;

BOOST_AUTO_TEST_SUITE(AccountSuite)

// ---------------------------------------------------------------------------
// Test 1: a default-constructed account round-trips and carries the well-known
// empty roots.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(EmptyAccountRoundTrip)
{
    Account account;
    BOOST_CHECK_EQUAL(account.nonce, 0);
    BOOST_CHECK_EQUAL(account.balance, 0);
    BOOST_CHECK_EQUAL(account.storageRoot, emptyRootHash());
    BOOST_CHECK_EQUAL(account.codeHash, emptyCodeHash());

    bcos::bytes const rlp = account.encode();
    Account const decoded = Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size()));

    BOOST_CHECK_EQUAL(decoded.nonce, 0);
    BOOST_CHECK_EQUAL(decoded.balance, 0);
    BOOST_CHECK_EQUAL(decoded.storageRoot, emptyRootHash());
    BOOST_CHECK_EQUAL(decoded.codeHash, emptyCodeHash());
}

// ---------------------------------------------------------------------------
// Test 2: nonce 0 encodes to the empty string (0x80, not 0x00) and round-trips;
// nonce 256 (a 2-byte big-endian integer) also round-trips.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(NonceZeroAndTwoFiftySixRoundTrip)
{
    {
        Account account;
        account.nonce = 0;
        bcos::bytes const rlp = account.encode();
        Account const decoded = Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size()));
        BOOST_CHECK_EQUAL(decoded.nonce, 0);
    }
    {
        Account account;
        account.nonce = 256;
        bcos::bytes const rlp = account.encode();
        Account const decoded = Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size()));
        BOOST_CHECK_EQUAL(decoded.nonce, 256);
    }
}

// ---------------------------------------------------------------------------
// Test 3: a large u256 balance round-trips exactly (no truncation / sign issues).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(LargeBalanceRoundTrip)
{
    Account account;
    account.balance = bcos::u256{"0x123456789abcdef0123456789abcdef0123456789abcdef0"};
    account.nonce = 42;

    bcos::bytes const rlp = account.encode();
    Account const decoded = Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size()));

    BOOST_CHECK_EQUAL(decoded.balance, account.balance);
    BOOST_CHECK_EQUAL(decoded.nonce, 42);
}

// ---------------------------------------------------------------------------
// Test 4: non-default hashes survive the round-trip as fixed 32-byte strings.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(NonDefaultHashesRoundTrip)
{
    Account account;
    account.storageRoot =
        bcos::h256("0x1111111111111111111111111111111111111111111111111111111111111111");
    account.codeHash =
        bcos::h256("0x2222222222222222222222222222222222222222222222222222222222222222");

    bcos::bytes const rlp = account.encode();
    Account const decoded = Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size()));

    BOOST_CHECK_EQUAL(decoded.storageRoot, account.storageRoot);
    BOOST_CHECK_EQUAL(decoded.codeHash, account.codeHash);
}

// ---------------------------------------------------------------------------
// Test 4b: a hash with leading zero bytes must encode as a fixed 32-byte string (the
// FixedBytes<32> RLP overload), NOT be trimmed like an integer. h256 has an implicit
// operator u256(), so this guards against encode() accidentally selecting the leading-zero-
// trimming integer path — which would shorten the field and corrupt Ethereum compatibility.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(LeadingZeroHashStaysFixed32Bytes)
{
    Account account;
    account.storageRoot =
        bcos::h256("0x00000000000000000000000000000000000000000000000000000000000000ab");
    account.codeHash =
        bcos::h256("0x0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    bcos::bytes const rlp = account.encode();
    Account const decoded = Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size()));

    BOOST_CHECK_EQUAL(decoded.storageRoot, account.storageRoot);
    BOOST_CHECK_EQUAL(decoded.codeHash, account.codeHash);
}

// ---------------------------------------------------------------------------
// Test 5: malformed inputs throw MPTDecodeError.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(MalformedInputThrows)
{
    // Empty input.
    {
        bcos::bytes const empty;
        BOOST_CHECK_EXCEPTION(
            Account::decode(bcos::bytesConstRef(empty.data(), empty.size())), MPTDecodeError,
            [](auto const& e) { return errinfoContains(e, "bad list header"); });
    }
    // A single byte that decodes as a string item, not a list.
    {
        bcos::bytes const notAList{0x05};
        BOOST_CHECK_EXCEPTION(
            Account::decode(bcos::bytesConstRef(notAList.data(), notAList.size())),
            MPTDecodeError, [](auto const& e) { return errinfoContains(e, "expected a list"); });
    }
    // A well-formed list but with too few fields (list of one element).
    {
        bcos::bytes const shortList{0xc1, 0x05};  // list(payload=1) holding integer 5
        BOOST_CHECK_EXCEPTION(
            Account::decode(bcos::bytesConstRef(shortList.data(), shortList.size())),
            MPTDecodeError, [](auto const& e) { return errinfoContains(e, "bad balance"); });
    }
    // A valid account with trailing junk after the list.
    {
        Account account;
        bcos::bytes rlp = account.encode();
        rlp.push_back(0xff);  // trailing byte beyond the declared list
        BOOST_CHECK_EXCEPTION(
            Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size())), MPTDecodeError,
            [](auto const& e) { return errinfoContains(e, "payload length does not match"); });
    }
}

// ---------------------------------------------------------------------------
// Test 6: a hash field whose RLP payload is not exactly 32 bytes is rejected (the generic
// FixedBytes<32> decoder would silently zero-pad a short payload — decodeHash32 must not).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ShortHashFieldRejected)
{
    // Build a 4-field list whose storageRoot is a 31-byte string (one byte short).
    bcos::bytes payload;
    payload.push_back(0x80);                                // nonce = 0 (empty string)
    payload.push_back(0x80);                                // balance = 0 (empty string)
    payload.push_back(static_cast<bcos::byte>(0x80 + 31));  // storageRoot: 31-byte string header
    payload.insert(payload.end(), 31, 0x11);                // 31 payload bytes (should be 32)
    payload.push_back(static_cast<bcos::byte>(0x80 + 32));  // codeHash: 32-byte string header
    payload.insert(payload.end(), 32, 0x22);                // 32 payload bytes

    bcos::bytes rlp;
    rlp.push_back(static_cast<bcos::byte>(0xf7 + 1));  // long-list header, 1 length byte
    rlp.push_back(static_cast<bcos::byte>(payload.size()));
    rlp.insert(rlp.end(), payload.begin(), payload.end());

    BOOST_CHECK_EXCEPTION(Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size())),
        MPTDecodeError, [](auto const& e) { return errinfoContains(e, "expected 32"); });
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
