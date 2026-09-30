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
 * @file EngineAuxOpLaneTest.cpp
 * @brief engine_getPayloadBodiesBy{Hash,Range}V1 / engine_getClientVersionV1 on the OP lane
 *
 * The three aux methods are one implementation shared by both lanes (EngineEndpoint). On the
 * OP lane a committed block differs from an L1 one in two ways the body builder must honour:
 * the L1-info deposit (0x7e) is unsigned and stored as the full envelope, and the header always
 * carries a withdrawalsRoot although the OP lane writes no SYS_NUMBER_2_WITHDRAWALS row
 * (every OP payload has an empty withdrawals list).
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/endpoints/Endpoints.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/ClientIdentity.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <regex>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
/// A signed legacy EIP-155 transaction in the shape a committed engine-lane block stores it:
/// extraTransactionBytes is the FULL wire envelope (decodedTransactionFromEnvelope /
/// wrapOpP2pEnvelope overwrite the signing preimage with it) plus the 65-byte tars signature.
std::shared_ptr<bcostars::protocol::TransactionImpl> makeSealedWeb3Tx(bcos::bytes& wireOut)
{
    namespace rlp = bcos::codec::rlp;
    bcos::bytes items;
    rlp::encode(items, uint64_t{0});
    rlp::encode(items, uint64_t{1});
    rlp::encode(items, uint64_t{21000});
    rlp::encode(items, bcos::Address("0xdead000000000000000000000000000000000011"));
    rlp::encode(items, uint64_t{0});
    rlp::encode(items, bcos::bytes{});
    rlp::encode(items, uint64_t{38});  // v = chainId(1) * 2 + 35 + yParity(1)
    rlp::encode(items, bcos::bytes(32, 0x11));
    rlp::encode(items, bcos::bytes(32, 0x22));
    wireOut.clear();
    rlp::encodeHeader(wireOut, rlp::Header{true, items.size()});
    wireOut.insert(wireOut.end(), items.begin(), items.end());

    bcos::bytes signature(65, 0x00);
    std::fill(signature.begin(), signature.begin() + 32, 0x11);
    std::fill(signature.begin() + 32, signature.begin() + 64, 0x22);
    signature[64] = 1;

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    inner.extraTransactionBytes.assign(wireOut.begin(), wireOut.end());
    inner.signature.assign(signature.begin(), signature.end());
    return tx;
}

/// An OP deposit (0x7e) exactly as the ledger stores it: Web3Transaction::takeToTarsTransaction
/// puts the full envelope in extraTransactionBytes and leaves the signature empty.
std::shared_ptr<bcostars::protocol::TransactionImpl> makeDepositTx(bcos::bytes& envelopeOut)
{
    bcos::rpc::Web3Transaction deposit;
    deposit.type = bcos::rpc::TransactionType::Deposit;
    deposit.sourceHash =
        bcos::h256("0x1111111111111111111111111111111111111111111111111111111111111111");
    deposit.from = bcos::Address("0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001");
    deposit.to = bcos::Address("0x4200000000000000000000000000000000000015");
    deposit.mint = 0;
    deposit.value = 0;
    deposit.gasLimit = 1000000;
    deposit.isSystemTx = false;
    deposit.data = bcos::bytes{0x44, 0x0a, 0x5e, 0x20};
    envelopeOut = deposit.encode();
    auto tars = deposit.takeToTarsTransaction();
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
}

void replaceBlockTransactions(bcos::protocol::Block::Ptr const& block,
    std::vector<std::shared_ptr<bcostars::protocol::TransactionImpl>> const& txs)
{
    auto& inner = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(block)->inner();
    inner.transactions.clear();
    for (auto const& tx : txs)
    {
        inner.transactions.emplace_back(tx->inner());
    }
}

/// Give a fake header the Canyon+/Shanghai+ shape (withdrawalsRoot present) and rehash it.
void setWithdrawalsRoot(FakeLedger* ledger, size_t index, bcos::crypto::Hash& hashImpl)
{
    auto const& header = ledger->ledgerData()[index]->blockHeader();
    header->setWithdrawalsRoot(
        bcos::h256("0x4242424242424242424242424242424242424242424242424242424242424242"));
    header->calculateHash(hashImpl);
}
}  // namespace

class EngineAuxOpLaneFixture : public RPCFixture
{
public:
    EngineAuxOpLaneFixture()
    {
        endpoints = std::make_unique<Endpoints>(nodeService, nullptr, false);
        hashImpl = m_blockFactory->cryptoSuite()->hashImpl();
    }

    // The lane is genesis-fixed; the endpoint reads the boot-time NodeService value.
    void setLane(int executorVersion) { nodeService->setExecutorVersion(executorVersion); }

    Json::Value bodiesByRange(std::string const& start, std::string const& count)
    {
        Json::Value params(Json::arrayValue);
        params.append(start);
        params.append(count);
        Json::Value response;
        // syncWait: FakeLedger callbacks run synchronously; see EngineRpcTest CALL_ENGINE_SYNC.
        task::syncWait([&](Endpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->getPayloadBodiesByRangeV1(p, r);
        }(endpoints.get(), params, response));
        return response["result"];
    }

    Json::Value bodiesByHash(std::vector<bcos::h256> const& hashes)
    {
        Json::Value params(Json::arrayValue);
        Json::Value list(Json::arrayValue);
        for (auto const& hash : hashes)
        {
            list.append(hash.hexPrefixed());
        }
        params.append(list);
        Json::Value response;
        task::syncWait([&](Endpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->getPayloadBodiesByHashV1(p, r);
        }(endpoints.get(), params, response));
        return response["result"];
    }

    std::unique_ptr<Endpoints> endpoints;
    bcos::crypto::Hash::Ptr hashImpl;
};

BOOST_FIXTURE_TEST_SUITE(EngineAuxOpLaneTest, EngineAuxOpLaneFixture)

BOOST_AUTO_TEST_CASE(opLaneBodyEmitsDepositEnvelopeAndEmptyWithdrawals)
{
    setLane(ledger::OPSTACK_EXECUTOR_VERSION);
    bcos::bytes depositEnvelope;
    bcos::bytes wire;
    auto deposit = makeDepositTx(depositEnvelope);
    auto sealed = makeSealedWeb3Tx(wire);
    BOOST_REQUIRE(deposit->isDepositTx());
    BOOST_REQUIRE(!sealed->isDepositTx());
    // The bytes the body must emit are the bytes the ledger indexes the deposit by.
    deposit->calculateHash(*hashImpl);
    BOOST_CHECK_EQUAL(deposit->hash(), bcos::crypto::keccak256Hash(bcos::ref(depositEnvelope)));

    // Block 5: an OP block — L1-info deposit first, one user tx, Canyon+ header, and no
    // SYS_NUMBER_2_WITHDRAWALS row (the OP lane never writes one).
    replaceBlockTransactions(m_ledger->ledgerData()[5], {deposit, sealed});
    setWithdrawalsRoot(m_ledger.get(), 5, *hashImpl);

    auto const result = bodiesByRange("0x5", "0x1");
    BOOST_REQUIRE(result.isArray());
    BOOST_REQUIRE_EQUAL(result.size(), 1);
    auto const& body = result[0u];
    BOOST_REQUIRE(body.isObject());
    BOOST_REQUIRE_EQUAL(body["transactions"].size(), 2);
    BOOST_CHECK_EQUAL(body["transactions"][0u].asString(), toHexStringWithPrefix(depositEnvelope));
    BOOST_CHECK_EQUAL(body["transactions"][1u].asString(), toHexStringWithPrefix(wire));
    BOOST_REQUIRE(body["withdrawals"].isArray());
    BOOST_CHECK_EQUAL(body["withdrawals"].size(), 0);

    // ByHash answers the same body for the same block.
    auto const byHash = bodiesByHash({m_ledger->ledgerData()[5]->blockHeader()->hash()});
    BOOST_REQUIRE_EQUAL(byHash.size(), 1);
    BOOST_CHECK(byHash[0u] == body);
}

BOOST_AUTO_TEST_CASE(opLanePreCanyonBodyKeepsNullWithdrawals)
{
    setLane(ledger::OPSTACK_EXECUTOR_VERSION);
    // No withdrawalsRoot on the header (pre-Canyon OP block): the spec's null, not [].
    // This lane starts at Karst and never produces such a block; the case pins spec
    // conformance of the shared builder only.
    replaceBlockTransactions(m_ledger->ledgerData()[6], {});
    auto const result = bodiesByRange("0x6", "0x1");
    BOOST_REQUIRE_EQUAL(result.size(), 1);
    BOOST_REQUIRE(result[0u].isObject());
    BOOST_CHECK_EQUAL(result[0u]["transactions"].size(), 0);
    BOOST_CHECK(result[0u].isMember("withdrawals"));
    BOOST_CHECK(result[0u]["withdrawals"].isNull());
}

BOOST_AUTO_TEST_CASE(l1LaneWithoutSidecarRowStaysNull)
{
    // Unchanged L1 behaviour: a Shanghai+ header whose SYS_NUMBER_2_WITHDRAWALS row is
    // missing is an unavailable body (null), never a fabricated empty list.
    setLane(ledger::ETHEREUM_EXECUTOR_VERSION);
    replaceBlockTransactions(m_ledger->ledgerData()[7], {});
    setWithdrawalsRoot(m_ledger.get(), 7, *hashImpl);
    auto const result = bodiesByRange("0x7", "0x1");
    BOOST_REQUIRE_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0u].isNull());
}

BOOST_AUTO_TEST_CASE(opLaneRangeBeyondHeadIsEmptyArray)
{
    setLane(ledger::OPSTACK_EXECUTOR_VERSION);
    // Fixture chain: blocks 0..19. Entirely beyond the head -> [] (no trailing nulls, no
    // error); straddling the head -> clamped to the blocks that exist.
    auto const beyond = bodiesByRange("0x100", "0x1");
    BOOST_REQUIRE(beyond.isArray());
    BOOST_CHECK_EQUAL(beyond.size(), 0);

    replaceBlockTransactions(m_ledger->ledgerData()[18], {});
    replaceBlockTransactions(m_ledger->ledgerData()[19], {});
    setWithdrawalsRoot(m_ledger.get(), 18, *hashImpl);
    setWithdrawalsRoot(m_ledger.get(), 19, *hashImpl);
    auto const clamped = bodiesByRange("0x12", "0x20");
    BOOST_REQUIRE_EQUAL(clamped.size(), 2);
    for (auto const& body : clamped)
    {
        BOOST_REQUIRE(body.isObject());
        BOOST_REQUIRE(body["withdrawals"].isArray());
        BOOST_CHECK_EQUAL(body["withdrawals"].size(), 0);
    }

    // An unknown hash is a null element, the OP lane included.
    auto const byHash = bodiesByHash(
        {bcos::h256("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")});
    BOOST_REQUIRE_EQUAL(byHash.size(), 1);
    BOOST_CHECK(byHash[0u].isNull());
}

BOOST_AUTO_TEST_CASE(clientVersionIsTheSharedClientIdentity)
{
    setLane(ledger::OPSTACK_EXECUTOR_VERSION);
    Json::Value params(Json::arrayValue);
    Json::Value cl(Json::objectValue);
    cl["code"] = "OP";
    cl["name"] = "op-node";
    cl["version"] = "v1.19.3";
    cl["commit"] = "0x00000000";
    params.append(cl);
    Json::Value response;
    task::syncWait([&](Endpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->getClientVersionV1(p, r);
    }(endpoints.get(), params, response));

    auto const& result = response["result"];
    BOOST_REQUIRE(result.isArray());
    BOOST_REQUIRE_EQUAL(result.size(), 1);
    auto const& self = result[0u];
    BOOST_CHECK_EQUAL(self["code"].asString(), "FB");
    BOOST_CHECK_EQUAL(self["name"].asString(), "FISCO-BCOS");
    // One source for the node's identity: the Engine self-report and the devp2p Hello
    // clientId (ADR 0003) both read bcos::clientIdentity().
    auto const version = self["version"].asString();
    BOOST_CHECK_EQUAL(version, bcos::clientIdentity());
    std::regex const shape(R"(^fisco-bcos/v[0-9]+\.[0-9]+\.[0-9]+.*/[^/]+/[^/]+$)");
    BOOST_CHECK_MESSAGE(std::regex_match(version, shape), "unexpected version: " << version);
    auto const commit = self["commit"].asString();
    BOOST_CHECK_EQUAL(commit.size(), 10);
    BOOST_CHECK_EQUAL(commit.substr(0, 2), "0x");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
