/*
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
 * @brief The EIP-55 helper's degradation contract: only a canonical 40-hex-digit address is
 *        checksummed, every other form (BFS link path, raw-address chain text, an over-long
 *        or short input) comes back untouched. bcos::toChecksumAddress indexes its 64-character
 *        keccak argument with the address's own length, so an unguarded call reads past that
 *        buffer for inputs longer than 64 and takes the casing from the wrong nibble positions
 *        for shorter ones.
 */
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(ChecksummedHexAddressTest)

// One official EIP-55 vector: the 40-hex-digit form is checksummed.
BOOST_AUTO_TEST_CASE(fortyHexDigitsIsChecksummed)
{
    BOOST_CHECK_EQUAL(checksummedHexAddress("52908400098527886e0f7030069857d2e4169ee7"),
        "52908400098527886E0F7030069857D2E4169EE7");
    BOOST_CHECK_EQUAL(checksummedHexAddressFromHex("0x52908400098527886e0f7030069857d2e4169ee7"),
        "52908400098527886E0F7030069857D2E4169EE7");
}

// Shorter than an address: the casing used to come from the hash's first nibbles. A BFS link
// path must survive verbatim.
BOOST_AUTO_TEST_CASE(shorterThanAnAddressIsReturnedUntouched)
{
    auto const bfsPath = std::string("/tables/t_test");
    BOOST_CHECK_EQUAL(checksummedHexAddress(bfsPath), bfsPath);
    auto const shortHex = std::string("0123456789abcdef0123");
    BOOST_CHECK_EQUAL(checksummedHexAddress(shortHex), shortHex);
}

// Longer than the 64-character hash the callee indexes: the pre-fix call read past it.
BOOST_AUTO_TEST_CASE(longerThanTheHashBufferIsReturnedUntouched)
{
    auto const longHex = std::string(100, 'a');
    BOOST_CHECK_EQUAL(checksummedHexAddress(longHex), longHex);
    auto const longPath = std::string("0x") + std::string(80, 'b');
    BOOST_CHECK_EQUAL(checksummedHexAddressFromHex(longPath), std::string(80, 'b'));
}

// 40 characters but not hex digits: not an address, so not checksummable.
BOOST_AUTO_TEST_CASE(nonHexOfAddressLengthIsReturnedUntouched)
{
    auto const nonHex = std::string(40, 'z');
    BOOST_CHECK_EQUAL(checksummedHexAddress(nonHex), nonHex);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
