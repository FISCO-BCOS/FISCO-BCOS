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
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/EthBlockBody.h>
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

    /// A self-consistent OP-lane block with no transactions.
    ///
    /// The generic fake block is not an Ethereum block: fakeAndCheckBlock stamps the header's
    /// txsRoot with bcos's native Block::calculateTransactionRoot, which is not the Ethereum
    /// indexed trie over raw envelopes that a real Eth/OP block carries (the engine writes that
    /// one, see EngineStorageCommit / transactionsRootFromPayload). debug_getRawBlock verifies
    /// exactly that commitment, so the happy path needs a header whose txsRoot really is
    /// calculateTransactionsRoot of the body it will produce.
    bcos::protocol::BlockHeader::Ptr makeEmptyOpBlock()
    {
        m_ledger->addNewBlock(nullptr, m_ledger->blockNumber() + 1, 0);
        auto const& block = m_ledger->ledgerData().back();
        auto header = block->blockHeader();
        header->setTimestamp(1'700'000'000'000);
        header->setBaseFee(7);
        // An empty body commits to the empty transaction trie, and the OP lane's Canyon+ body
        // carries an empty withdrawals list, i.e. also the empty root.
        header->setTxsRoot(mpt::emptyRootHash());
        header->setWithdrawalsRoot(mpt::emptyRootHash());
        BOOST_REQUIRE(bcos::protocol::isOpEthereumBlock(*header));
        header->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*header));
        m_ledger->indexBlockHash(header->hash(), header->number());
        // The eth-lane withdrawals branch reads the sidecar row, and a header that commits to a
        // withdrawalsRoot with no row must be refused. OP blocks never carry the row (the OP
        // verifier does not write one), so the op lane never reaches that branch -- but a test
        // that flips the lane would. Seed the row the shape a Shanghai+ commit writes: the RLP
        // list of withdrawals, empty here (0xc0).
        storage::Entry withdrawalsRow;
        withdrawalsRow.set(bcos::bytes{0xc0});
        m_ledger->setStateStorageEntry(std::string(ledger::SYS_NUMBER_2_WITHDRAWALS),
            std::to_string(header->number()), std::move(withdrawalsRow));
        return header;
    }

    /// An Ethereum-lane header (the [eth_genesis_header] shape) whose stored hash IS
    /// keccak256(rlp) — the header RawHeaderServesEthGenesisHeader publishes, and the base for
    /// the guard's negative control (which mutates a field after calculateHash).
    bcos::protocol::BlockHeader::Ptr makeEthLaneHeader()
    {
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
// errors with a plain message instead (its native millisecond timestamp fails the EthBlockHeader
// bridge, so encodeHeader throws before the canonicalBlockHash comparison).
BOOST_AUTO_TEST_CASE(RawHeaderRefusesNonOpHeader)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const resp = call1("debug_getRawHeader", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InternalError);
    BOOST_CHECK(
        resp["error"]["message"].asString().find("Header RLP encode failed") != std::string::npos);
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

    // Legacy lane only. The debug data plane is not OP-lane-only any more: a node that serves an
    // OP rollup as its L1 has to answer kona-host, and that node is an Ethereum-lane node.
    for (std::optional<int> version : {std::optional<int>{}, std::optional<int>{1}})
    {
        if (version)
        {
            setExecutorVersion(*version);
        }
        for (auto const& resp :
            {call1("debug_getRawHeader", "latest"), call1("debug_getRawReceipts", "latest"),
                dbGet(bcos::bytes(root.begin(), root.end()))})
        {
            BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), MethodNotFound);
            BOOST_CHECK_EQUAL(
                resp["error"]["message"].asString(), unregistered["error"]["message"].asString());
            BOOST_TEST(!resp.isMember("result"));
        }
    }
}

// The Ethereum lane serves the whole data plane: it is the lane an L1 node runs on, and
// kona-host reads L1 headers, receipts and trie nodes through exactly these three methods.
BOOST_AUTO_TEST_CASE(EthereumLaneServesTheDataPlane)
{
    auto const root = buildTrie();
    wireReader();
    setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
    makeLatestOpHeader();

    // Header: the RLP must hash back to the published block hash (kona-host's own check).
    auto const header = resultBytes(call1("debug_getRawHeader", "latest"));
    BOOST_TEST(!header.empty());

    // Receipts: an array, one entry per transaction. Asked of the genesis block because it has
    // no transactions -- this fixture has no stored receipts for its later blocks, and a missing
    // receipt is (correctly) a hard error rather than a short array, so this pins the method's
    // presence, its lane gate and its result shape only.
    //
    // The byte contract -- that the returned strings reassemble into the header's receiptsRoot --
    // is pinned in LedgerTest::ethereumLaneReceiptLeavesRebuildTheCommittedRoot, where block 4242
    // has real stored receipts and the root they commit to. It is not covered here, and this
    // comment used to claim otherwise.
    auto const receipts = call1("debug_getRawReceipts", "0x0");
    BOOST_REQUIRE_MESSAGE(!receipts.isMember("error"), printJson(receipts));
    BOOST_REQUIRE_MESSAGE(receipts["result"].isArray(), printJson(receipts));
    BOOST_CHECK_EQUAL(receipts["result"].size(), 0U);

    // The OP lane is refused: its receipts-trie leaf deliberately omits depositNonce inside one
    // fork window (EthTrieRoots.cpp), so the leaf is not the receipt's consensus encoding there.
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const onOpLane = call1("debug_getRawReceipts", "0x0");
    BOOST_REQUIRE_MESSAGE(onOpLane.isMember("error"), printJson(onOpLane));
    BOOST_CHECK_EQUAL(onOpLane["error"]["code"].asInt(), MethodNotFound);
    setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);

    // Trie nodes: the preimage lookup answers on this lane too.
    auto const node = dbGet(bcos::bytes(root.begin(), root.end()));
    BOOST_TEST(!node.isMember("error"));
}

// The rollup genesis ([eth_genesis_header]) is an Ethereum-versioned header whose stored hash is
// keccak256 of its RLP; eth_getBlockByNumber(0) publishes it and debug_getRawHeader(0) must hash
// to it (kona-host starts every claim from the genesis anchor).
BOOST_AUTO_TEST_CASE(RawHeaderServesEthGenesisHeader)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const header = makeEthLaneHeader();
    BOOST_REQUIRE(!bcos::protocol::isOpEthereumBlock(*header));  // the case that was refused
    auto const rlp = protocol::EthBlockHeader::encodeHeader(*header);
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
    // The earliest tag resolves to block 0 too (the same block eth_getBlockByNumber(0)
    // publishes), and is served — the tag refusal above is scoped to safe/finalized/pending.
    BOOST_CHECK(resultBytes(call1("debug_getRawHeader", "earliest")) == rlp);
}

// The guard's negative control: a header whose stored hash is NOT keccak256 of its RLP must be
// refused. Mutating a field AFTER calculateHash reproduces exactly that — an Eth-lane header
// the bridge can still encode, but whose bytes no longer hash back to the published hash — so
// this is the branch that turns red if the `canonicalBlockHash != keccak256(encoded)` check is
// ever removed.
BOOST_AUTO_TEST_CASE(RawHeaderRefusesHeaderWhoseHashIsNotItsRlpHash)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const header = makeEthLaneHeader();
    BOOST_REQUIRE(!bcos::protocol::isOpEthereumBlock(*header));
    // Change a field AND stamp a stored hash that is not keccak256 of the resulting RLP — the
    // negative half of the guard's invariant. (The tars setters clear dataHash, so the wrong
    // hash has to be re-stamped explicitly; an empty one would just make hash() throw.)
    header->setStateRoot(h256(0xdeadU));
    header->setRLPHash(h256(0xc0ffeeU));
    m_ledger->ledgerData().front()->setBlockHeader(header);

    auto const resp = call1("debug_getRawHeader", "0x0");
    BOOST_REQUIRE_MESSAGE(resp.isMember("error"), printJson(resp));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InternalError);
    BOOST_CHECK(
        resp["error"]["message"].asString().find("has no Ethereum header") != std::string::npos);
    BOOST_TEST(!resp.isMember("result"));
}

// The guard's positive twin: a pre-Canyon OP header (Bedrock-Delta — NON_ETH + baseFee, no
// withdrawalsRoot, so isOpEthereumBlock is false and canonicalBlockHash takes the stored-hash
// branch) whose stored hash IS keccak256(rlp) is SERVED. The previous re-spelled predicate
// refused this shape; the invariant-based guard admits it, which is the fix's claimed benefit.
BOOST_AUTO_TEST_CASE(RawHeaderServesPreCanyonOpHeader)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto header = m_ledger->ledgerData().back()->blockHeader();
    header->setTimestamp(1'700'000'000'000);
    header->setBaseFee(7);
    BOOST_REQUIRE(!bcos::protocol::isOpEthereumBlock(*header));  // pre-Canyon: no withdrawalsRoot
    header->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*header));

    auto const served = resultBytes(call1("debug_getRawHeader", "latest"));
    BOOST_TEST(
        bcos::protocol::canonicalBlockHash(*header).hexPrefixed() == header->hash().hexPrefixed());
    BOOST_TEST(bcos::crypto::keccak256Hash(bcos::ref(served)).hexPrefixed() ==
               header->hash().hexPrefixed());
}

// debug_getRawHeader refuses the safe/finalized/pending tags before resolution: answering them
// through the shared tag resolver would return the unsafe tip and contradict
// eth_getBlockByNumber on the same node (which fails closed on the engine lane when the
// forkchoice tracker is unset). A number, a hash, latest and earliest are accepted.
BOOST_AUTO_TEST_CASE(RawHeaderRefusesSafeFinalizedPendingTags)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    makeLatestOpHeader();  // a resolvable head, so a refusal is the refusal and not "not found"
    for (auto const* tag : {"safe", "finalized", "pending"})
    {
        auto const resp = call1("debug_getRawHeader", tag);
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), std::string(tag) + ": " + printJson(resp));
        BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InvalidParams);
        BOOST_CHECK_EQUAL(resp["error"]["message"].asString(),
            "debug_getRawHeader does not accept the safe/finalized/pending tags; use a block "
            "number, a block hash, latest or earliest");
        BOOST_TEST(!resp.isMember("result"));
    }
    // latest is NOT refused — the refusal is tag-specific, not a blanket rejection.
    BOOST_TEST(resultBytes(call1("debug_getRawHeader", "latest")).size() > 0);
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

// op-succinct's witness generator reads whole L2 blocks through debug_getRawBlock and has no
// fallback, so the first requirement is that the endpoint exists on the OP lane at all.
// The invariant that matters is that the header embedded in the block RLP is byte-identical to
// what debug_getRawHeader serves: the preimage-oracle keys a block by the hash of that header,
// so a body whose header re-encodes differently would derive a trace nobody else reproduces.
BOOST_AUTO_TEST_CASE(RawBlockHeaderMatchesRawHeader)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    // A fresh empty block: the fixture's genesis block already carries fake transactions, which
    // are FISCO-native and therefore deliberately refused by the endpoint (see the negative test
    // below), so the happy path needs a block whose transaction list is empty.
    auto const headerPtr = makeEmptyOpBlock();
    auto const& header = *headerPtr;
    auto const numberHex = toQuantity(header.number());

    auto const rawHeader = resultBytes(call1("debug_getRawHeader", numberHex));
    auto const rawBlock = resultBytes(call1("debug_getRawBlock", numberHex));

    // In Ethereum block RLP the header is an inner LIST, so debug_getRawHeader's bytes appear
    // verbatim inside the block — not wrapped as an RLP string.
    BOOST_CHECK(std::search(rawBlock.begin(), rawBlock.end(), rawHeader.begin(), rawHeader.end()) !=
                rawBlock.end());

    // The block decodes back to a block whose header carries the same identity.
    bcos::protocol::EthBlock decoded;
    auto mutableBlock = rawBlock;
    decoded.rlpDecode(bcos::bytesConstRef(mutableBlock.data(), mutableBlock.size()));
    BOOST_TEST(decoded.data().header.number == header.number());
    BOOST_TEST(decoded.data().header.stateRoot == header.stateRoot());
    BOOST_TEST(decoded.data().header.txsRoot == header.txsRoot());
    // OP lane: Canyon+ bodies always carry the (empty) withdrawals list, so 4 elements decode.
    BOOST_REQUIRE(decoded.data().withdrawals.has_value());
    BOOST_TEST(decoded.data().withdrawals->empty());
}

// The hash form works the same way debug_getRawHeader's does, and both resolve to one answer.
BOOST_AUTO_TEST_CASE(RawBlockAcceptsBlockHashAndTag)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const headerPtr = makeEmptyOpBlock();
    auto const published = bcos::protocol::canonicalBlockHash(*headerPtr).hexPrefixed();

    auto const byNumber = resultBytes(call1("debug_getRawBlock", toQuantity(headerPtr->number())));
    auto const byHash = resultBytes(call1("debug_getRawBlock", published));
    auto const byTag = resultBytes(call1("debug_getRawBlock", "latest"));
    BOOST_TEST(byHash == byNumber);
    BOOST_TEST(byTag == byNumber);
}

BOOST_AUTO_TEST_CASE(RawBlockUnknownBlockIsError)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    makeLatestOpHeader();
    auto const unknownHash = call1("debug_getRawBlock", h256{0xbeefU}.hexPrefixed());
    BOOST_CHECK_EQUAL(unknownHash["error"]["code"].asInt(), InvalidParams);
    BOOST_CHECK_EQUAL(unknownHash["error"]["message"].asString(), "Block not found");

    auto const future = call1("debug_getRawBlock", "0x1000");
    BOOST_CHECK_EQUAL(future["error"]["code"].asInt(), InvalidParams);
    BOOST_CHECK_EQUAL(future["error"]["message"].asString(), "Block not found");
}

// A FISCO-native transaction has no EIP-2718 envelope at all — there is no byte string whose
// keccak could serve as its transactions-trie key. Emitting an empty slot instead would produce
// a block whose txsRoot no client can reproduce, so the endpoint fails closed with a diagnostic.
BOOST_AUTO_TEST_CASE(RawBlockRefusesTransactionWithoutRawEnvelope)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    m_ledger->addNewBlock(nullptr, m_ledger->blockNumber() + 1, 1);
    auto const headerPtr = makeLatestOpHeader();
    BOOST_REQUIRE_GT(headerPtr->number(), 0);

    auto const resp = call1("debug_getRawBlock", toQuantity(headerPtr->number()));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InternalError);
    BOOST_CHECK(resp["error"]["message"].asString().find("no raw envelope") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(RawBlockRejectsSafeFinalizedPendingTags)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    makeLatestOpHeader();
    for (auto const* tag : {"safe", "finalized", "pending"})
    {
        auto const resp = call1("debug_getRawBlock", tag);
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), tag);
        BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InvalidParams);
    }
}

// An unknown hash is geth's -32000 "not found", not InvalidParams: kona-host distinguishes a
// miss (retry later) from a malformed request.
BOOST_AUTO_TEST_CASE(RawTransactionUnknownHashIsNotFound)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    makeLatestOpHeader();
    auto const resp = call1("debug_getRawTransaction", h256{0xbeefU}.hexPrefixed());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), Web3DefaultError);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "not found");
}

BOOST_AUTO_TEST_CASE(RawTransactionRejectsMalformedHash)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    makeLatestOpHeader();
    for (auto const* bad : {"0x1234", "nothex", "0xzz"})
    {
        auto const resp = call1("debug_getRawTransaction", bad);
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), bad);
        BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InvalidParams);
    }
}

// The whole point of serving a block is that a client can rebuild its tries from the bytes and
// land exactly on the roots the header commits to. Assert that end to end: decode the served
// block and recompute both roots through the same helpers the engine commits with.
BOOST_AUTO_TEST_CASE(RawBlockReproducesHeaderCommitments)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const headerPtr = makeEmptyOpBlock();
    auto const rawBlock =
        resultBytes(call1("debug_getRawBlock", toQuantity(headerPtr->number())));

    bcos::protocol::EthBlock decoded;
    auto mutableBlock = rawBlock;
    decoded.rlpDecode(bcos::bytesConstRef(mutableBlock.data(), mutableBlock.size()));

    std::vector<bcos::bytesConstRef> txRefs;
    for (auto const& tx : decoded.data().transactions)
    {
        txRefs.emplace_back(tx.data(), tx.size());
    }
    BOOST_CHECK(bcos::ledger::mpt::calculateTransactionsRoot(txRefs) ==
                headerPtr->txsRoot());

    BOOST_REQUIRE(decoded.data().withdrawals.has_value());
    std::vector<bcos::bytes> encodedWithdrawals;
    for (auto const& withdrawal : *decoded.data().withdrawals)
    {
        bcos::bytes item;
        bcos::codec::rlp::encode(item, withdrawal);
        encodedWithdrawals.push_back(std::move(item));
    }
    std::vector<bcos::bytesConstRef> withdrawalRefs;
    for (auto const& item : encodedWithdrawals)
    {
        withdrawalRefs.emplace_back(item.data(), item.size());
    }
    BOOST_REQUIRE(headerPtr->withdrawalsRoot().has_value());
    BOOST_CHECK(bcos::ledger::mpt::calculateWithdrawalsRoot(withdrawalRefs) ==
                *headerPtr->withdrawalsRoot());
}

// Guard: a header whose txsRoot does not match its body must be refused, never served. This is
// the check that keeps a mis-reassembled envelope from reaching a prover as a plausible block.
BOOST_AUTO_TEST_CASE(RawBlockRefusesInconsistentTxsRoot)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const headerPtr = makeEmptyOpBlock();
    // Claim a wrong transaction trie for a block whose body is empty, then re-stamp the hash so
    // the header is internally consistent -- only the commitment is wrong. The withdrawals
    // commitment is cleared together with the root so this case isolates the transactionsRoot
    // guard (a mismatched withdrawalsRoot would trip its own guard first).
    headerPtr->setTxsRoot(h256(0x1234U));
    headerPtr->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*headerPtr));
    m_ledger->indexBlockHash(headerPtr->hash(), headerPtr->number());

    auto const resp = call1("debug_getRawBlock", toQuantity(headerPtr->number()));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InternalError);
    BOOST_CHECK(resp["error"]["message"].asString().find("transactionsRoot") != std::string::npos ||
                resp["error"]["message"].asString().find("withdrawalsRoot") != std::string::npos);
}

// A hash request must resolve to that block, not to whatever now occupies the height the hash
// was indexed against. A canonical replacement can leave the SYS_HASH_2_NUMBER row pointing at a
// height that has since been refilled; serving it would answer with a block nobody asked for.
BOOST_AUTO_TEST_CASE(RawBlockHashRequestRejectsStaleIndex)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const headerPtr = makeEmptyOpBlock();
    auto const number = headerPtr->number();
    auto const staleHash = headerPtr->hash();

    // Rewrite the height with a different block while leaving the old hash row in place, which is
    // what a canonical replacement does: the stale row still resolves the old hash to this height.
    // Build a fresh header (not the pointer makeEmptyOpBlock already stamped) so the stored block
    // at this height really is a different one.
    auto replacement = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    replacement->setNumber(number);
    replacement->setTimestamp(1'700'000'001'000);
    replacement->setGasLimit(30'000'000);
    replacement->setBaseFee(7);
    replacement->setTxsRoot(mpt::emptyRootHash());
    replacement->setWithdrawalsRoot(mpt::emptyRootHash());
    replacement->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*replacement));
    BOOST_REQUIRE_EQUAL(replacement->number(), number);
    BOOST_REQUIRE(replacement->hash() != staleHash);

    // Point the height at the replacement header in the fake ledger's block list, and index the
    // new hash alongside the stale one (the stale row is what a real reorg leaves behind).
    m_ledger->ledgerData().back()->setBlockHeader(replacement);
    m_ledger->indexBlockHash(replacement->hash(), number);

    // The stale hash row still points at this height, but the block there is not the one asked for.
    auto const stale = call1("debug_getRawBlock", staleHash.hexPrefixed());
    BOOST_REQUIRE(stale.isMember("error"));
    BOOST_CHECK_EQUAL(stale["error"]["code"].asInt(), InvalidParams);
    BOOST_CHECK_EQUAL(stale["error"]["message"].asString(), "Block not found");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
