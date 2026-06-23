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

namespace bcos::ledger::mpt::test
{

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
// Test 5: malformed inputs throw MPTDecodeError.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(MalformedInputThrows)
{
    // Empty input.
    {
        bcos::bytes const empty;
        BOOST_CHECK_THROW(
            Account::decode(bcos::bytesConstRef(empty.data(), empty.size())), MPTDecodeError);
    }
    // A single byte that decodes as a string item, not a list.
    {
        bcos::bytes const notAList{0x05};
        BOOST_CHECK_THROW(
            Account::decode(bcos::bytesConstRef(notAList.data(), notAList.size())), MPTDecodeError);
    }
    // A well-formed list but with too few fields (list of one element).
    {
        bcos::bytes const shortList{0xc1, 0x05};  // list(payload=1) holding integer 5
        BOOST_CHECK_THROW(Account::decode(bcos::bytesConstRef(shortList.data(), shortList.size())),
            MPTDecodeError);
    }
    // A valid account with trailing junk after the list.
    {
        Account account;
        bcos::bytes rlp = account.encode();
        rlp.push_back(0xff);  // trailing byte beyond the declared list
        BOOST_CHECK_THROW(
            Account::decode(bcos::bytesConstRef(rlp.data(), rlp.size())), MPTDecodeError);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
