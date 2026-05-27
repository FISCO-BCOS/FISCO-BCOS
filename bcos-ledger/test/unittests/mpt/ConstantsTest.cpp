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

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
