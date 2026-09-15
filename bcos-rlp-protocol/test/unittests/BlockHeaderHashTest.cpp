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
 * @file BlockHeaderHashTest.cpp
 * @brief Unit tests for the canonical block identity hash across the three lanes.
 * @date 2026/9/14
 */

#include "bcos-rlp-protocol/BlockHeaderHash.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string_view>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BlockHeaderHashTest)

namespace
{
/// A native FISCO header: NON_ETH and no fork fields, i.e. neither OP nor an Eth header.
bcostars::protocol::BlockHeaderImpl::Ptr makeNativeHeader()
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    auto& data = tars->data;
    data.blockNumber = 7;
    data.timestamp = 1700000000 * 1000LL;  // internal milliseconds
    data.gasLimit = "30000000";
    data.gasUsed = "21000";
    data.coinbase.assign(20, static_cast<char>(0xab));
    data.uncleHash.assign(32, static_cast<char>(0xee));
    data.stateRoot.assign(32, static_cast<char>(0x11));
    data.txsRoot.assign(32, static_cast<char>(0x22));
    data.receiptRoot.assign(32, static_cast<char>(0x33));
    data.prevRandao.assign(32, static_cast<char>(0x44));
    data.logsBloom.assign(256, static_cast<char>(0xcd));
    data.difficulty = "0";
    data.nonce.assign(8, static_cast<char>(0x00));
    bcostars::ParentInfo parentInfo;
    parentInfo.blockNumber = 6;
    parentInfo.blockHash.assign(32, static_cast<char>(0x55));
    data.parentInfo.push_back(parentInfo);
    return std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
}
}  // namespace

/// Native lane: the stored header hash IS the identity.
BOOST_AUTO_TEST_CASE(nativeLaneUsesTheStoredHash)
{
    auto header = makeNativeHeader();
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    header->calculateHash(*hashImpl);

    BOOST_CHECK(!isOpEthereumBlock(*header));
    BOOST_CHECK_EQUAL(canonicalBlockHash(*header).hex(), header->hash().hex());
}

/// Eth lane: a real fork version is never the OP lane; the header's own hash (the RLP hash,
/// injected by calculateRLPHash on that path) is the identity.
BOOST_AUTO_TEST_CASE(ethLaneUsesTheStoredHash)
{
    auto header = makeNativeHeader();
    header->setEthBlockVersion(EthBlockVersion::LONDON);
    header->setBaseFee(u256(1));
    header->setRLPHash(h256(0x61));

    BOOST_CHECK(!isOpEthereumBlock(*header));
    BOOST_CHECK_EQUAL(canonicalBlockHash(*header).hex(), header->hash().hex());
}

/// OP lane: NON_ETH (the OP build path stamps no fork version) plus the fork fields OP's
/// ExecutionPayload always carries. The identity is the Ethereum block hash, which differs
/// from the stored one — exactly why the rule has a single home.
BOOST_AUTO_TEST_CASE(opLaneUsesTheRlpIdentityHash)
{
    auto header = makeNativeHeader();
    header->setWithdrawalsRoot(h256(0x61));
    header->setBaseFee(u256(1000000000));
    BOOST_REQUIRE(isOpEthereumBlock(*header));

    // Golden pinned externally: keccak256 over the RLP encoding of exactly this header's
    // field set (captured by an independent harness, not via canonicalBlockHash), so a
    // broken hash implementation cannot pass just because both sides of the comparison
    // share code.
    constexpr std::string_view c_opLaneGoldenHash =
        "ce126e95af4dacf1efa887b5aaa32a6f65b42b9c39e80d7494cd13d8f144760b";
    BOOST_CHECK_EQUAL(EthBlockHeader::computeHash(*header).hex(), std::string(c_opLaneGoldenHash));
    BOOST_CHECK_EQUAL(canonicalBlockHash(*header).hex(), std::string(c_opLaneGoldenHash));

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    header->calculateHash(*hashImpl);  // native form: fills the TARS hash
    BOOST_CHECK_NE(canonicalBlockHash(*header).hex(), header->hash().hex());
}

/// The predicate keys on field presence, not on the value: OP always engages both fields,
/// so the classification does not depend on which fee/root a given block happens to carry.
BOOST_AUTO_TEST_CASE(opLanePredicateKeysOnPresenceNotValue)
{
    auto header = makeNativeHeader();
    header->setWithdrawalsRoot(h256{});
    header->setBaseFee(u256(0));
    BOOST_CHECK(isOpEthereumBlock(*header));

    // Either field alone is not the OP shape.
    auto withdrawalsOnly = makeNativeHeader();
    withdrawalsOnly->setWithdrawalsRoot(h256(0x61));
    BOOST_CHECK(!isOpEthereumBlock(*withdrawalsOnly));

    auto baseFeeOnly = makeNativeHeader();
    baseFeeOnly->setBaseFee(u256(1));
    BOOST_CHECK(!isOpEthereumBlock(*baseFeeOnly));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
