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
 * @file DebugRawReceiptsTest.cpp
 * @brief debug_getRawReceipts against a REAL block with stored receipts, on the real stack
 *        (FullChainFixture: real Ledger over RocksDB, real BaselineScheduler commit).
 *
 * Two things were previously untested and one of them shipped a blocker:
 *
 * 1. The endpoint used to read the block with `TRANSACTIONS` only. RECEIPTS and TRANSACTIONS are
 *    disjoint flags and only RECEIPTS populates Block::receipts(), so receiptsSize() was 0, the
 *    count guard rejected every block that had a transaction, and the method threw InternalError
 *    for every real block. The only other test asked for block 0x0 deliberately, so the encoding
 *    loop never ran in CI. The fixture here has real stored receipts, so the guard and the loop
 *    both have to work.
 * 2. The EIP-2718 prefix is taken from the signed envelope rather than the tars web3TypedTxKind
 *    mirror, for the same bytes the block's receiptsRoot commits to. The filler transactions in
 *    FullChainFixture carry a real 0x02 envelope, so the envelope branch is the one exercised here
 *    (the ledger-side cases all build FISCO-native transactions and only reach the mirror
 * fallback).
 *
 * The assertion that ties it together is the one kona-host relies on: reassembling a trie from the
 * returned byte strings must reproduce the header's receiptsRoot.
 */
#include "transaction-scheduler/tests/FullChainFixture.h"
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/DebugEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>  // printJson
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

namespace bcos
{
namespace test
{
namespace
{
using namespace bcos::test::fullchain;

/// An Ethereum-lane chain (executor_version 2) with the compatibility version that persists the
/// executor_version row: DebugEndpoint::requireEthereumExecutorLane resolves the lane from exactly
/// that row.
ledger::GenesisConfig ethLaneGenesis()
{
    auto genesis = FullChainFixture::baseGenesis();
    genesis.m_compatibilityVersion =
        static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_15_0_VERSION);
    genesis.m_web3ChainID = genesis.m_chainID;
    genesis.m_executorVersion = ledger::ETHEREUM_EXECUTOR_VERSION;
    return genesis;
}

/// Call debug_getRawReceipts directly on the endpoint and return the decoded leaves, in block
/// order.
std::vector<bytes> rawReceipts(rpc::DebugEndpoint& endpoint, std::string const& blockTag)
{
    Json::Value request(Json::arrayValue);
    request.append(blockTag);
    Json::Value response;
    task::syncWait(endpoint.getRawReceipts(request, response));
    BOOST_REQUIRE_MESSAGE(!response.isMember("error"), rpc::printJson(response));
    BOOST_REQUIRE(response.isMember("result"));
    BOOST_REQUIRE(response["result"].isArray());

    std::vector<bytes> leaves;
    leaves.reserve(response["result"].size());
    for (auto const& item : response["result"])
    {
        leaves.push_back(fromHexWithPrefix(item.asString()));
    }
    return leaves;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DebugRawReceiptsTest)

/// The blocker: a block WITH transactions and receipts must be served, one leaf per transaction.
BOOST_AUTO_TEST_CASE(NonEmptyBlockServesOneLeafPerTransaction)
{
    FullChainFixture fixture{"raw_receipts_nonempty"};
    fixture.buildGenesis(ethLaneGenesis());
    // Two transactions, so a response that returns only the first (or zips out of order) fails the
    // root comparison below as well as the count.
    fixture.runBlock(1, /*txCount=*/2);

    auto nodeService = std::make_shared<rpc::NodeService>(fixture.m_ledger, nullptr,
        fixture.m_txpool, nullptr, nullptr, fixture.m_blockFactory, nullptr);
    rpc::DebugEndpoint endpoint(nodeService);

    auto const leaves = rawReceipts(endpoint, "0x1");
    BOOST_REQUIRE_EQUAL(leaves.size(), 2U);

    // The property kona-host depends on: these bytes reassemble into the root the header committed.
    std::vector<bytesConstRef> leafRefs;
    leafRefs.reserve(leaves.size());
    for (auto const& leaf : leaves)
    {
        leafRefs.emplace_back(ref(leaf));
    }
    auto const header = fixture.headerOnChain(1);
    BOOST_REQUIRE(header);
    BOOST_CHECK_EQUAL(ledger::mpt::calculateReceiptsRoot(leafRefs), header->receiptsRoot());
    // Guard against a vacuous pass: an empty trie would be the empty root, not the header's.
    BOOST_CHECK(header->receiptsRoot() != ledger::mpt::emptyRootHash());
}

/// The type-byte branch this PR changed: the leaf prefix comes from the transaction's signed
/// envelope (0x02 here), not from the tars mirror. Both quantities are asserted literally, because
/// a regression in the envelope path leaves every other test green while the node commits a
/// different root than a standard client computes.
BOOST_AUTO_TEST_CASE(LeafPrefixComesFromTheSignedEnvelope)
{
    FullChainFixture fixture{"raw_receipts_type_byte"};
    fixture.buildGenesis(ethLaneGenesis());
    fixture.runBlock(1, /*txCount=*/2);

    auto nodeService = std::make_shared<rpc::NodeService>(fixture.m_ledger, nullptr,
        fixture.m_txpool, nullptr, nullptr, fixture.m_blockFactory, nullptr);
    rpc::DebugEndpoint endpoint(nodeService);

    auto const leaves = rawReceipts(endpoint, "0x1");
    BOOST_REQUIRE_EQUAL(leaves.size(), 2U);
    for (auto const& leaf : leaves)
    {
        BOOST_REQUIRE(!leaf.empty());
        // 0x02 = EIP-1559, the kind makeWeb3FillerTx signs into extraTransactionBytes.
        BOOST_CHECK_EQUAL(static_cast<int>(leaf.front()), 0x02);
    }
}

/// A block this node does not have is a parameter error, not an InternalError: kona-host
/// distinguishes them, and the lane gate must not turn a missing block into a crash.
BOOST_AUTO_TEST_CASE(MissingBlockIsAParameterError)
{
    FullChainFixture fixture{"raw_receipts_missing"};
    fixture.buildGenesis(ethLaneGenesis());
    fixture.runBlock(1);

    auto nodeService = std::make_shared<rpc::NodeService>(fixture.m_ledger, nullptr,
        fixture.m_txpool, nullptr, nullptr, fixture.m_blockFactory, nullptr);
    rpc::DebugEndpoint endpoint(nodeService);

    Json::Value request(Json::arrayValue);
    request.append("0x40");  // far beyond the tip
    Json::Value response;
    BOOST_CHECK_THROW(
        task::syncWait(endpoint.getRawReceipts(request, response)), rpc::JsonRpcException);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
