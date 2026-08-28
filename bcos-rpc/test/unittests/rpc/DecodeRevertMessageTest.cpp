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
 * @brief Unit tests for decodeRevertMessage: pins which eth_call revert payload shapes decode
 *        a solidity Error(string) reason and which degrade to the bare op-geth "execution
 *        reverted" message. A reverted call must never surface a fabricated reason.
 * @file DecodeRevertMessageTest.cpp
 */

#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
/// Build the canonical Error(string) ABI payload: selector || offset(32) || length || reason,
/// word-aligned. Used to construct both valid revert outputs and near-valid ones for the
/// malformed cases (by post-editing a byte).
std::string errorStringHex(std::string const& reason)
{
    bcos::bytes payload{0x08, 0xc3, 0x79, 0xa0};
    auto pushWord = [&payload](std::size_t value) {
        payload.insert(payload.end(), 24, 0);  // high bytes of the 32-byte word
        for (int i = 7; i >= 0; --i)           // low 8 bytes, big-endian (value is size_t)
        {
            payload.push_back(static_cast<bcos::byte>((value >> (i * 8)) & 0xff));
        }
    };
    pushWord(32);             // data offset
    pushWord(reason.size());  // length
    payload.insert(payload.end(), reason.begin(), reason.end());
    payload.resize((payload.size() + 31U) / 32U * 32U, 0);  // word-align
    return bcos::toHex(payload, "0x");                      // "0x"-prefixed
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DecodeRevertMessageTest)

// Outputs that carry no Error(string) at all must yield the bare message, never a reason
// fabricated from arbitrary revert bytes.
BOOST_AUTO_TEST_CASE(NonErrorStringOutputYieldsBareMessage)
{
    BOOST_TEST(bcos::rpc::decodeRevertMessage("") == "execution reverted");
    BOOST_TEST(bcos::rpc::decodeRevertMessage("0x") == "execution reverted");
    BOOST_TEST(bcos::rpc::decodeRevertMessage("0xdeadbeef") == "execution reverted");
    // 0x4e487b71: solidity Panic(uint256) — not Error(string), must stay bare.
    std::string const panic = "0x4e487b71" + std::string(62, '0') + "11" + std::string(64, '0');
    BOOST_TEST(bcos::rpc::decodeRevertMessage(panic) == "execution reverted");
}

// A valid Error(string) appends the decoded reason in op-geth style.
BOOST_AUTO_TEST_CASE(ErrorStringReasonIsAppended)
{
    BOOST_TEST(bcos::rpc::decodeRevertMessage(errorStringHex("Insufficient balance")) ==
               "execution reverted: Insufficient balance");
    BOOST_TEST(bcos::rpc::decodeRevertMessage(errorStringHex("revert with a longer reason")) ==
               "execution reverted: revert with a longer reason");
    // Empty string is a valid Error("") — the reason is empty but the shape is genuine, so
    // the message keeps the ": " (exactly what op-geth's fmt.Errorf("%s: %v") produces).
    BOOST_TEST(bcos::rpc::decodeRevertMessage(errorStringHex("")) == "execution reverted: ");
}

// Near-valid Error(string) shapes that must fail closed to the bare message: wrong offset,
// a length that runs past the end of the payload, and an offset+length pair that cannot be
// trusted as a reason.
BOOST_AUTO_TEST_CASE(MalformedErrorStringYieldsBareMessage)
{
    // offset word says 0x21 instead of 0x20 (some nonstandard ABI layout): reject.
    auto wrongOffset = errorStringHex("Insufficient balance");
    wrongOffset[73] = '1';  // last hex char of the offset word (chars 2..9 selector, 10..73 offset)
    BOOST_TEST(bcos::rpc::decodeRevertMessage(wrongOffset) == "execution reverted");

    // length word claims 0xf4 (244) while only 20 payload bytes remain (the padded payload
    // leaves 32 bytes after the fixed header): reject.
    auto tooLong = errorStringHex("Insufficient balance");
    tooLong[136] = 'f';  // length word chars 136,137 hold 0x14: "14" -> "f4"
    BOOST_TEST(bcos::rpc::decodeRevertMessage(tooLong) == "execution reverted");

    // Valid offset but a length word claiming max-size_t: the overflow-guarded read clamps
    // to size_t max, which can never fit the remaining payload: reject.
    BOOST_TEST(bcos::rpc::decodeRevertMessage("0x08c379a0" + std::string(62, '0') + "20" +
                                              std::string(64, 'f') + std::string(64, '0')) ==
               "execution reverted");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
