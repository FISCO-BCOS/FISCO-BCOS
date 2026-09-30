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
 * @file DebugChallengerDataPlaneTest.cpp
 * @brief debug_getRawHeader / debug_dbGet: the geth-route preimage reads kona-host issues
 *        against an OP-lane node (ADR 0007), and their MethodNotFound answer off the OP lane.
 */

#include "../common/RPCFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <string>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

class DebugChallengerFixture : public RPCFixture
{
public:
    using MPTNodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

    DebugChallengerFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
    }

    void setExecutorVersion(int version)
    {
        m_ledger->setSystemConfig(
            magic_enum::enum_name(ledger::SystemConfig::executor_version), std::to_string(version));
    }

    /// Turn the latest fake block into an OP-shaped header (NON_ETH + withdrawalsRoot +
    /// baseFee, isOpEthereumBlock) and give it the identity the OP commit path gives it:
    /// header->hash() = keccak256(rlp(header)), indexed for lookup by that hash.
    bcos::protocol::BlockHeader::Ptr makeLatestOpHeader()
    {
        auto const& block = m_ledger->ledgerData().back();
        auto header = block->blockHeader();
        header->setTimestamp(1'700'000'000'000);
        header->setBaseFee(7);
        header->setWithdrawalsRoot(mpt::emptyRootHash());
        BOOST_REQUIRE(bcos::protocol::isOpEthereumBlock(*header));
        header->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*header));
        m_ledger->indexBlockHash(header->hash(), header->number());
        return header;
    }

    /// A one-account trie in m_mptNodes; returns its root (which is also a stored node key).
    bcos::h256 buildTrie()
    {
        mpt::Account account;
        account.nonce = 1;
        account.balance = 42;
        std::map<bcos::h256, std::optional<bcos::bytes>> changes{
            {mpt::accountKeyHash(
                 bcos::Address{std::string("0x00000000000000000000000000000000000000ab")}),
                account.encode()}};
        auto result = task::syncWait(mpt::commitTrie(m_mptNodes, mpt::emptyRootHash(), changes));
        task::syncWait(mpt::flushTrieNodes(m_mptNodes, result.newNodes));
        return result.root;
    }

    void wireReader()
    {
        nodeService->setMPTNodeReader(
            std::make_shared<bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>>(m_mptNodes));
    }

    /// Store @p code under its keccak hash in s_code_binary, as EVMAccount::setCode does.
    bcos::h256 storeCode(bcos::bytes const& code)
    {
        auto const codeHash = bcos::crypto::keccak256Hash(bcos::ref(code));
        storage::Entry entry;
        entry.set(code);
        std::promise<void> done;
        m_ledger->getStateStorage()->asyncSetRow(bcos::ledger::SYS_CODE_BINARY,
            codeHash.toRawString(), std::move(entry), [&done](auto&&) { done.set_value(); });
        done.get_future().get();
        return codeHash;
    }

    Json::Value call(std::string const& method, Json::Value params)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = method;
        req["params"] = std::move(params);
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            printJson(req), [&promise](bcos::bytes resp, boost::beast::http::status) {
                promise.set_value(std::move(resp));
            });
        auto const bytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char const*)bytes.data(), bytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Json::Value call1(std::string const& method, std::string const& arg)
    {
        Json::Value params(Json::arrayValue);
        params.append(arg);
        return call(method, std::move(params));
    }

    Json::Value dbGet(bcos::bytes const& key)
    {
        return call1("debug_dbGet", toHexStringWithPrefix(key));
    }

    static bcos::bytes codeKey(bcos::h256 const& hash)
    {
        bcos::bytes key{'c'};
        key.insert(key.end(), hash.begin(), hash.end());
        return key;
    }

    static bcos::bytes resultBytes(Json::Value const& resp)
    {
        BOOST_REQUIRE_MESSAGE(!resp.isMember("error"), printJson(resp));
        return fromHexWithPrefix(resp["result"].asString());
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
    MPTNodeStorage m_mptNodes;
};

BOOST_FIXTURE_TEST_SUITE(DebugChallengerDataPlaneTest, DebugChallengerFixture)

BOOST_AUTO_TEST_CASE(LanePredicate)
{
    BOOST_TEST(!isOpStackLane(0));
    BOOST_TEST(!isOpStackLane(1));
    BOOST_TEST(!isOpStackLane(ledger::ETHEREUM_EXECUTOR_VERSION));
    BOOST_TEST(isOpStackLane(ledger::OPSTACK_EXECUTOR_VERSION));
}

// keccak256(debug_getRawHeader) must equal the hash eth_getBlockByNumber / ByHash report — that
// equality is the check kona-host applies to every header it pulls.
BOOST_AUTO_TEST_CASE(RawHeaderHashesToPublishedBlockHash)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const headerPtr = makeLatestOpHeader();
    auto const& header = *headerPtr;
    auto const numberHex = toQuantity(header.number());

    Json::Value blockParams(Json::arrayValue);
    blockParams.append(numberHex);
    blockParams.append(false);
    auto const block = call("eth_getBlockByNumber", blockParams);
    BOOST_REQUIRE(block["result"].isObject());
    auto const published = block["result"]["hash"].asString();
    BOOST_TEST(published == bcos::protocol::canonicalBlockHash(header).hexPrefixed());

    auto const byNumber = resultBytes(call1("debug_getRawHeader", numberHex));
    BOOST_TEST(bcos::crypto::keccak256Hash(bcos::ref(byNumber)).hexPrefixed() == published);

    // The hash form (what kona-host sends) serves the same bytes.
    auto const byHash = resultBytes(call1("debug_getRawHeader", published));
    BOOST_TEST(byHash == byNumber);
    auto const byTag = resultBytes(call1("debug_getRawHeader", "latest"));
    BOOST_TEST(byTag == byNumber);

    // And the bytes decode back to the header's own fields.
    bcos::protocol::EthBlockHeader decoded;
    bcos::protocol::EthBlockHeader::toEthBlockHeader(decoded, bcos::ref(byNumber));
    BOOST_TEST(decoded.data().number == header.number());
    BOOST_TEST(decoded.data().stateRoot == header.stateRoot());
}

BOOST_AUTO_TEST_CASE(RawHeaderUnknownBlockIsError)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    makeLatestOpHeader();
    auto const unknownHash = call1("debug_getRawHeader", h256{0xbeefU}.hexPrefixed());
    BOOST_CHECK_EQUAL(unknownHash["error"]["code"].asInt(), InvalidParams);
    BOOST_CHECK_EQUAL(unknownHash["error"]["message"].asString(), "Block not found");

    auto const future = call1("debug_getRawHeader", "0x1000");
    BOOST_CHECK_EQUAL(future["error"]["code"].asInt(), InvalidParams);
    BOOST_CHECK_EQUAL(future["error"]["message"].asString(), "Block not found");
}

// A non-OP header on the OP lane would serve bytes that do not hash to the published hash; it
// errors with a plain message instead.
BOOST_AUTO_TEST_CASE(RawHeaderRefusesNonOpHeader)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const resp = call1("debug_getRawHeader", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InternalError);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "Block 1 has no Ethereum header");
}

// A trie node answers under both key shapes kona-host sends: bare hash and 'c' || hash.
BOOST_AUTO_TEST_CASE(DbGetTrieNodeBothKeyForms)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const root = buildTrie();
    wireReader();
    auto const stored = task::syncWait(bcos::storage2::readOne(m_mptNodes, root));
    BOOST_REQUIRE(stored.has_value());

    auto const bare = resultBytes(dbGet(bcos::bytes(root.begin(), root.end())));
    BOOST_TEST(bare == *stored);
    BOOST_TEST(bcos::crypto::keccak256Hash(bcos::ref(bare)) == root);
    BOOST_TEST(resultBytes(dbGet(codeKey(root))) == *stored);
}

// Bytecode answers under both key shapes as well.
BOOST_AUTO_TEST_CASE(DbGetCodeBothKeyForms)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    wireReader();
    bcos::bytes const code{0x60, 0x2a, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};
    auto const codeHash = storeCode(code);

    BOOST_TEST(resultBytes(dbGet(codeKey(codeHash))) == code);
    BOOST_TEST(resultBytes(dbGet(bcos::bytes(codeHash.begin(), codeHash.end()))) == code);
}

// A miss is a -32000 "not found" error (kona-host retries its code hint with the bare hash),
// never a 200 with empty data and never -32603.
BOOST_AUTO_TEST_CASE(DbGetUnknownKeyIsNotFoundError)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const root = buildTrie();
    wireReader();
    h256 const unknown{0x1234U};
    // A 33-byte key whose prefix is not 'c' is not a code key, even when its tail is a stored
    // trie node's hash.
    bcos::bytes wrongPrefix{'d'};
    wrongPrefix.insert(wrongPrefix.end(), root.begin(), root.end());
    for (auto const& key : {bcos::bytes(unknown.begin(), unknown.end()), codeKey(unknown),
             bcos::bytes{0x01, 0x02}, wrongPrefix})
    {
        auto const resp = dbGet(key);
        BOOST_REQUIRE(resp.isMember("error"));
        BOOST_TEST(!resp.isMember("result"));
        BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
        BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "not found");
    }

    auto const notHex = call1("debug_dbGet", "0xzz");
    BOOST_CHECK_EQUAL(notHex["error"]["code"].asInt(), InvalidParams);
}

BOOST_AUTO_TEST_CASE(DbGetWithoutReaderIsInternalError)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const resp = dbGet(bcos::bytes(32, 0x11));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InternalError);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "MPT not enabled on this node");
}

// Off the OP lane both methods answer exactly what an unregistered method answers.
BOOST_AUTO_TEST_CASE(NonOpLaneIsMethodNotFound)
{
    auto const root = buildTrie();
    wireReader();
    auto const unregistered = call1("debug_noSuchMethod", "0x1");
    BOOST_REQUIRE_EQUAL(unregistered["error"]["code"].asInt(), MethodNotFound);

    for (std::optional<int> version : {std::optional<int>{}, std::optional<int>{1},
             std::optional<int>{ledger::ETHEREUM_EXECUTOR_VERSION}})
    {
        if (version)
        {
            setExecutorVersion(*version);
        }
        for (auto const& resp :
            {call1("debug_getRawHeader", "latest"), dbGet(bcos::bytes(root.begin(), root.end()))})
        {
            BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), MethodNotFound);
            BOOST_CHECK_EQUAL(
                resp["error"]["message"].asString(), unregistered["error"]["message"].asString());
            BOOST_TEST(!resp.isMember("result"));
        }
    }
}

// The rollup genesis ([eth_genesis_header]) is an Ethereum-versioned header whose stored hash is
// keccak256 of its RLP; eth_getBlockByNumber(0) publishes it and debug_getRawHeader(0) must hash
// to it (kona-host starts every claim from the genesis anchor).
BOOST_AUTO_TEST_CASE(RawHeaderServesEthGenesisHeader)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    protocol::EthBlockHeaderData data;
    data.uncleHash = protocol::c_emptyOmmersHash;
    data.stateRoot = h256(1U);
    data.txsRoot = mpt::emptyRootHash();
    data.receiptsRoot = mpt::emptyRootHash();
    data.gasLimit = 30'000'000;
    data.timestamp = 1'700'000'000;
    data.extraData = bytes{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};
    data.baseFee = 1'000'000'000;
    data.withdrawalsHash = mpt::emptyRootHash();
    data.blobGasUsed = 0;
    data.excessBlobGas = 0;
    data.parentBeaconRoot = h256{};
    bytes rlp;
    codec::rlp::encode(rlp, data);
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    protocol::EthBlockHeader::toTarsHeader(header, bcos::ref(rlp));
    header->calculateHash(*cryptoSuite->hashImpl());
    BOOST_REQUIRE(!bcos::protocol::isOpEthereumBlock(*header));  // the case that was refused
    m_ledger->ledgerData().front()->setBlockHeader(header);
    auto const genesisHash = m_ledger->ledgerData().front()->blockHeader()->hash();
    m_ledger->indexBlockHash(genesisHash, 0);

    Json::Value blockParams(Json::arrayValue);
    blockParams.append("0x0");
    blockParams.append(false);
    auto const published = call("eth_getBlockByNumber", blockParams)["result"]["hash"].asString();
    BOOST_CHECK_EQUAL(published, genesisHash.hexPrefixed());

    auto const raw = resultBytes(call1("debug_getRawHeader", "0x0"));
    BOOST_CHECK(raw == rlp);
    BOOST_CHECK_EQUAL(bcos::crypto::keccak256Hash(bcos::ref(raw)).hexPrefixed(), published);
    BOOST_CHECK(resultBytes(call1("debug_getRawHeader", published)) == rlp);
}

// kona-host asks eth_getProof for accounts a block creates: under a complete trie the absent
// account answers an EIP-1186 exclusion proof (empty account), never -32004.
BOOST_AUTO_TEST_CASE(GetProofAbsentAccountIsExclusionProof)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const root = buildTrie();
    wireReader();
    m_ledger->ledgerData().back()->blockHeader()->setStateRoot(root);

    bcos::Address const dead(std::string("0x000000000000000000000000000000000000dEaD"));
    Json::Value params(Json::arrayValue);
    params.append(dead.hexPrefixed());
    params.append(Json::Value(Json::arrayValue));
    params.append("latest");
    auto const resp = call("eth_getProof", params);
    BOOST_REQUIRE_MESSAGE(resp.isMember("result"), printJson(resp));
    auto const& result = resp["result"];
    BOOST_CHECK_EQUAL(result["balance"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["codeHash"].asString(), mpt::emptyCodeHash().hexPrefixed());
    BOOST_CHECK_EQUAL(result["storageHash"].asString(), mpt::emptyRootHash().hexPrefixed());

    mpt::EIP1186Proof proof;
    proof.address = dead;
    proof.codeHash = mpt::emptyCodeHash();
    proof.storageHash = mpt::emptyRootHash();
    for (auto const& node : result["accountProof"])
    {
        proof.accountProof.push_back(fromHexWithPrefix(node.asString()));
    }
    BOOST_CHECK(!proof.accountProof.empty());
    BOOST_CHECK(mpt::verifyProof(root, proof).accountValid);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
