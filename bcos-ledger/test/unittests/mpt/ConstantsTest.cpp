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
 * @file ConstantsTest.cpp
 * @brief Pin Ethereum well-known constants to detect accidental hex typos
 */

#include "bcos-ledger/mpt/Constants.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MptConstantsSuite)

// emptyRootHash() is already covered indirectly by NodeEncoderTest::EmptyTrieRootMatchesEthereum-
// Constant (it verifies keccak256({0x80}) == emptyRootHash()). This direct test pins the literal
// hex value so a typo in EMPTY_ROOT_HASH_HEX is caught even if NodeEncoder regresses.
BOOST_AUTO_TEST_CASE(EmptyRootHashLiteralPinned)
{
    BOOST_CHECK_EQUAL(
        emptyRootHash().hex(), "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421");
}

// emptyCodeHash() = keccak256("") — the only direct test in the module. EOA accounts and
// uninitialised contract slots reference this value; a hex typo would silently corrupt account
// state hashing downstream.
BOOST_AUTO_TEST_CASE(EmptyCodeHashLiteralPinned)
{
    BOOST_CHECK_EQUAL(
        emptyCodeHash().hex(), "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

// The constants are computed on first use from the hasher's definition (HasherT({0x80}) and
// HasherT("")). The two literal-pinning cases above prove the keccak instantiation; this case
// covers the parameterization itself: a non-default hasher (SM3) yields its own distinct,
// stable values rather than the keccak literals.
BOOST_AUTO_TEST_CASE(NonDefaultHasherYieldsOwnConstants)
{
    using SM3 = bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher;
    BOOST_CHECK(emptyRootHash<SM3>() != emptyRootHash());
    BOOST_CHECK(emptyCodeHash<SM3>() != emptyCodeHash());
    BOOST_CHECK(emptyRootHash<SM3>() != emptyCodeHash<SM3>());
    // Meyers-singleton stability: repeated calls return the same cached value.
    BOOST_CHECK(emptyRootHash<SM3>() == emptyRootHash<SM3>());
    BOOST_CHECK(emptyCodeHash<SM3>() == emptyCodeHash<SM3>());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
