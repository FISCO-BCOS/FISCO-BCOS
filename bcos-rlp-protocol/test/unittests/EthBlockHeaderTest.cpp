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
 * @file EthBlockHeaderTest.cpp
 * @brief Unit tests for EthBlockHeader RLP encode/decode/hash
 * @date 2026/8/5
 */

#include "bcos-rlp-protocol/EthBlockHeader.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EthBlockHeaderTest)

// A helper that builds a Tars header with all Eth-required fields populated
static std::shared_ptr<bcostars::BlockHeader> makeEthTarsHeader()
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    auto& data = tars->data;
    data.blockNumber = 77;
    data.timestamp = 1700000000;
    data.gasLimit = "30000000";
    data.gasUsed = "21000";
    data.baseFee = "1000000000";
    data.coinbase.assign(20, 0xab);
    data.stateRoot.assign(32, 0x11);
    data.txsRoot.assign(32, 0x22);
    data.receiptRoot.assign(32, 0x33);
    data.prevRandao.assign(32, 0x44);
    data.logsBloom.assign(256, 0xcd);
    bcostars::ParentInfo parentInfo;
    parentInfo.blockNumber = 76;
    parentInfo.blockHash.assign(32, 0x55);
    data.parentInfo.push_back(parentInfo);
    return tars;
}

// Tars header -> EthBlockHeader -> RLP -> toTarsHeader (base-class Ptr)
BOOST_AUTO_TEST_CASE(rlpEncodeDecodeRoundTrip)
{
    auto tars = makeEthTarsHeader();

    // Tars -> Eth
    EthBlockHeader ethHeader(*tars);
    BOOST_CHECK_EQUAL(ethHeader.data().number, 77);
    BOOST_CHECK_EQUAL(ethHeader.data().timestamp, 1700000000);
    BOOST_CHECK_EQUAL(ethHeader.data().gasLimit, u256(30000000));
    BOOST_CHECK_EQUAL(ethHeader.data().gasUsed, u256(21000));
    BOOST_CHECK(ethHeader.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*ethHeader.data().baseFee, u256(1000000000));

    // RLP encode
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    BOOST_CHECK(!rlp.empty());

    // Static: decode RLP -> EthBlockHeader
    bcos::Error::UniquePtr ethError;
    auto decodedEth = EthBlockHeader::toEthBlockHeader(bcos::ref(rlp), ethError);
    BOOST_CHECK(!ethError);
    BOOST_CHECK_EQUAL(decodedEth.data().number, 77);
    BOOST_CHECK_EQUAL(decodedEth.data().timestamp, 1700000000);
    BOOST_CHECK_EQUAL(decodedEth.data().gasLimit, u256(30000000));
    BOOST_CHECK_EQUAL(decodedEth.data().gasUsed, u256(21000));
    BOOST_CHECK(decodedEth.data().baseFee.has_value());
    BOOST_CHECK_EQUAL(*decodedEth.data().baseFee, u256(1000000000));
    BOOST_CHECK(decodedEth.data().coinbase == ethHeader.data().coinbase);
    BOOST_CHECK(decodedEth.data().stateRoot == ethHeader.data().stateRoot);
    BOOST_CHECK(decodedEth.data().txsRoot == ethHeader.data().txsRoot);
    BOOST_CHECK(decodedEth.data().receiptsRoot == ethHeader.data().receiptsRoot);

    // Static: decode RLP -> Tars header (base-class Ptr)
    auto tarsPtr = EthBlockHeader::toTarsHeader(bcos::ref(rlp), ethError);
    BOOST_CHECK(tarsPtr != nullptr);
    BOOST_CHECK(!ethError);
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(tarsPtr);
    BOOST_CHECK(impl != nullptr);
    BOOST_CHECK_EQUAL(impl->number(), 77);
    BOOST_CHECK_EQUAL(impl->timestamp(), 1700000000);
    BOOST_CHECK_EQUAL(impl->gasLimit(), u256(30000000));
    BOOST_CHECK_EQUAL(impl->gasUsed(), u256(21000));
    // The converted header must be marked as an Eth header
    BOOST_CHECK_EQUAL(impl->version(), bcos::protocol::ETH_BLOCK_HEADER_VERSION);
}

// calculateRLPHash injects the RLP hash into a BlockHeaderImpl
BOOST_AUTO_TEST_CASE(calculateRLPHashInjectsHash)
{
    auto tars = makeEthTarsHeader();
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);

    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(!error);

    // Expected: keccak256 of the RLP encoding
    bcos::protocol::EthBlockHeader ethHeader(*tars);
    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = bcos::crypto::keccak256Hash(bcos::ref(rlp));

    BOOST_CHECK(header->hash() == expected);
}

// RLP encoding equals keccak256 of the RLP encoding (the hash formula)
BOOST_AUTO_TEST_CASE(rlpHashFormula)
{
    auto tars = makeEthTarsHeader();
    tars->data.blockNumber = 5;
    tars->data.timestamp = 1000;

    EthBlockHeader ethHeader(*tars);

    bytes rlp;
    ethHeader.rlpEncode(rlp);
    auto expected = crypto::keccak256Hash(bcos::ref(rlp));

    // calculateRLPHash injects exactly keccak256(rlp(header))
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(!error);
    BOOST_CHECK(header->hash() == expected);
}

// An incomplete Tars header: constructor no longer throws (validation moved to
// calculateRLPHash), but calculateRLPHash must report an InvalidTarsHeader error.
BOOST_AUTO_TEST_CASE(incompleteTarsHeaderThrows)
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    // Empty header: missing all required Eth fields
    // Constructor no longer validates — it converts directly.
    EthBlockHeader ethHeader(*tars);  // should not throw
    BOOST_CHECK_EQUAL(ethHeader.data().number, 0);

    // Partially filled: missing coinbase
    tars = makeEthTarsHeader();
    tars->data.coinbase.clear();

    // calculateRLPHash on an incomplete header must report an error
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    bcos::Error::UniquePtr error;
    bcos::protocol::EthBlockHeader::calculateRLPHash(header, error);
    BOOST_CHECK(error != nullptr);
    BOOST_CHECK_EQUAL(error->errorCode(),
        static_cast<int32_t>(EthBlockHeaderError::InvalidTarsHeader));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
