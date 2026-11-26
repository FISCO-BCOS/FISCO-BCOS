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
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <string>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

/// A ledger whose transaction read can be made to fail in one of the two ways the production
/// ledger distinguishes, so the endpoint's translation can be told apart from a passthrough
/// (review F1):
///  * Missing -- the shape a pruned/absent SYS_HASH_2_TX row takes: batchGetTransactions fails with
///    a count mismatch, which must become geth's -32000 "not found";
///  * Failing -- a genuine storage failure, which must keep its diagnostics and surface as an
///    internal error instead of being flattened into the same "not found".
///
/// Both inject the PRODUCTION nesting, not a bare inner error: Ledger::getBatchTxsByHashList wraps
/// whatever batchGetTransactions raised in GetStorageError("GetBatchTxsByHashList error"). The two
/// outcomes therefore have an identical outer code and message and differ only in the cause chain,
/// which is exactly what isMissingTransactionRow() has to traverse -- injecting a direct inner
/// error would test a shape that never occurs on a node.
class FaultyReadLedger : public FakeLedger
{
public:
    using FakeLedger::FakeLedger;

    enum class Mode : uint8_t
    {
        Healthy,
        Missing,
        Failing
    };
    Mode mode = Mode::Healthy;

    task::Task<BatchTransactionsResult> getBatchTxsByHashList(
        crypto::HashListPtr hashes, bool withProof) override
    {
        if (mode == Mode::Missing)
        {
            // Verbatim the pair Ledger.cpp produces for an absent row, wrapper and all.
            auto inner = BCOS_ERROR(bcos::ledger::LedgerError::CollectAsyncCallbackError,
                "Batch get transaction failed, transactions size not match hashesSize, txsSize: 0, "
                "hashesSize: 1");
            BOOST_THROW_EXCEPTION(BCOS_ERROR_WITH_PREV(
                bcos::ledger::LedgerError::GetStorageError, "GetBatchTxsByHashList error", inner));
        }
        if (mode == Mode::Failing)
        {
            // Same wrapper, different cause: a storage outage rather than an absent row.
            auto inner = BCOS_ERROR(
                bcos::ledger::LedgerError::GetStorageError, "Batch get transaction failed ");
            BOOST_THROW_EXCEPTION(BCOS_ERROR_WITH_PREV(
                bcos::ledger::LedgerError::GetStorageError, "GetBatchTxsByHashList error", inner));
        }
        co_return co_await FakeLedger::getBatchTxsByHashList(std::move(hashes), withProof);
    }
};

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
        // Both, because they answer different questions: the ledger row is what requireEthereumLane
        // reads, and NodeService::executorVersion is what isOpStackLane reads for the body shape.
        // Setting only the ledger row left every test on the eth branch, so the OP withdrawals
        // branch was never exercised.
        nodeService->setExecutorVersion(version);
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

    /// Rebuild the RPC surface over @p ledger. The fixture wires one ledger into NodeService at
    /// construction, so an endpoint under test can only be pointed at a fault-injecting ledger by
    /// building a fresh service and RPC around it.
    void useLedger(std::shared_ptr<FakeLedger> ledger)
    {
        auto service = std::make_shared<rpc::NodeService>(
            ledger, scheduler, txPool, nullptr, nullptr, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, service);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc != nullptr);
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

// A stored hash that is NOT keccak256 of the served bytes must not make the endpoint refuse the
// header: the endpoint's identity is the bytes it emits (review F4), not a hash field the ledger
// may never have written. What must still be refused is a HASH REQUEST whose requested hash is not
// the keccak of those bytes -- that binding is what survives a reorg, and it is the negative
// control for the guard as it now stands.
BOOST_AUTO_TEST_CASE(RawHeaderServesHeaderWhoseStoredHashIsNotItsRlpHash)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    auto const header = makeEthLaneHeader();
    BOOST_REQUIRE(!bcos::protocol::isOpEthereumBlock(*header));
    // Change a field AND stamp a stored identity that is not keccak256 of the resulting RLP: the
    // shape the ledger produces when a row's hash field disagrees with its own bytes.
    header->setStateRoot(h256(0xdeadU));
    auto const wrongStoredHash = h256(0xc0ffeeU);
    header->setRLPHash(wrongStoredHash);
    m_ledger->ledgerData().front()->setBlockHeader(header);
    m_ledger->indexBlockHash(wrongStoredHash, 0);

    // Serving by number works: encodeHeader is the only gate and the bytes are self-consistent.
    auto const served = resultBytes(call1("debug_getRawHeader", "0x0"));
    auto const servedHash = bcos::crypto::keccak256Hash(bcos::ref(served));
    BOOST_CHECK(servedHash != wrongStoredHash);
    m_ledger->indexBlockHash(servedHash, 0);

    // The served bytes' own hash resolves back to the same header.
    BOOST_CHECK(resultBytes(call1("debug_getRawHeader", servedHash.hexPrefixed())) == served);

    // Asking for the stored hash would have to answer with bytes that hash to it. They do not, so
    // the request is a miss rather than a wrong answer.
    auto const byStoredHash = call1("debug_getRawHeader", wrongStoredHash.hexPrefixed());
    BOOST_REQUIRE_MESSAGE(byStoredHash.isMember("error"), printJson(byStoredHash));
    BOOST_CHECK_EQUAL(byStoredHash["error"]["code"].asInt(), InvalidParams);
    BOOST_TEST(!byStoredHash.isMember("result"));
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
    auto const rawBlock = resultBytes(call1("debug_getRawBlock", toQuantity(headerPtr->number())));

    bcos::protocol::EthBlock decoded;
    auto mutableBlock = rawBlock;
    decoded.rlpDecode(bcos::bytesConstRef(mutableBlock.data(), mutableBlock.size()));

    std::vector<bcos::bytesConstRef> txRefs;
    for (auto const& tx : decoded.data().transactions)
    {
        txRefs.emplace_back(tx.data(), tx.size());
    }
    BOOST_CHECK(bcos::ledger::mpt::calculateTransactionsRoot(txRefs) == headerPtr->txsRoot());

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

// Byte-exact vectors: (signing preimage, 65-byte r||s||yParity signature, canonical tx hash).
// Copied from the bcos-tars-protocol known-answer table (New1_Web3TxHashCanonicalTest), whose
// hashes came from a geth cross-checked oracle. Signatures are real secp256k1 outputs from the
// well-known 0xdead…beef key, so these exercise the same splice the endpoint performs rather
// than a hand-assembled byte string.
struct SignedTxVector
{
    char const* name;
    char const* preimageHex;
    char const* signatureHex;
    char const* canonicalHashHex;
};

// clang-format off
constexpr std::array<SignedTxVector, 5> SIGNED_TX_VECTORS{{
    {"legacy_pre_eip155",
     "eb07847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeef",
     "a31b27f37d25064dede61b1bf9649134318f38f31a0b75376b77d0fb3e6f70d90d505feb95719789060cce3592145b9e"
     "309de267cc878ac85eae5a1fa5462beb00",
     "b2c20c0c72a91024dedbcf57a37e4eb0bbc8e30776902f577f904638e43a71b2"},
    {"legacy_eip155_chain1",
     "ee07847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeef018080",
     "94f5e0158ba372a7b451996ac591fec2055d7ca7d0f72aa6b473d7b84a69d58262a22960ade9f059d389489fff300213"
     "df3ef0e09e7b4ab89508b934530c566101",
     "c063947c632cfcf52d7a11c9e8887430bcfb673b2bfc33ddc6d961ba77ffa2f4"},
    {"legacy_eip155_chain1337",
     "f007847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeef82053980"
     "80",
     "beb0472e1fcad9f02fafae6a631e6b0c4be8a0c1f74ed54d9d2654ae02e6149a4f50a93d6ea03a0c1b13e26245882421"
     "8a80d586ba0b10df8113c2918ffa1dc801",
     "15f417e7e9a83754c74742402e46695d7fb3e953caf214d6cdf31a9541a3c2c1"},
    {"eip1559_chain5",
     "02f20507843b9aca00847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084de"
     "adbeefc0",
     "3dc2e61019aa7824a7e290f2b097d10a405e989fe4d3348268df2afcf647b4086e02084295955b070394ea8e604d86b4"
     "942e4a1638358dca984e9f6adff46d4a01",
     "220e457c18458cbed1fe2345901fdce4374c2c088416e3c8decf3859f2183ef2"},
    {"eip2930_chain5",
     "01ed0507847735940082520894727fc6a68321b754475c668a6abfb6e9e71c169a87038d7ea4c6800084deadbeefc0",
     "07f6e9652e1023b331f7f8a129ceae1bd9340d4b308b003de8adf70f77f254d93d4061a37f52a20a6b10b77ea63aacab"
     "3a2d8266d4db8553b2f0b5f727b7abbb00",
     "e63657a2bdb36ae8b668014c3f8d95c4bd1c0ddea57fc0c1a3e5705fb86a0fd5"},
}};
// clang-format on

// A deposit transaction (0x7e) has no signature: its signing preimage IS the full envelope
// (Web3TxHandler's DepositTxHandler::encodeForSign returns encode()). Built the way that handler
// builds it so the bytes are the real wire form.
bcos::bytes buildDepositEnvelope()
{
    auto const sourceHash = bcos::h256(0xaaU);
    auto const from = bcos::Address(
        std::string_view("0x00000000000000000000000000000000000000f1"), bcos::Address::FromHex);
    auto const to = bcos::Address(
        std::string_view("0x4200000000000000000000000000000000000015"), bcos::Address::FromHex);
    bcos::u256 const mint{0};
    bcos::u256 const value{1'000'000};
    uint64_t const gas = 200'000;
    uint32_t const isSystemTransaction = 0;
    bcos::bytes const data{};
    bcos::bytes out;
    out.push_back(0x7e);
    bcos::codec::rlp::encode(
        out, sourceHash, from, to, mint, value, gas, isSystemTransaction, data);
    return out;
}

// Make a tars transaction the way the RPC ingress stores one: type=Web3, extraTransactionBytes
// holds the signing preimage, signature holds r||s||yParity, and extraTransactionHash carries the
// canonical hash (keccak of the full envelope) so hash lookups resolve to it.
bcos::protocol::Transaction::Ptr makeStoredTransaction(
    bcos::bytesConstRef preimage, bcos::bytesConstRef signature, bcos::bytesConstRef canonicalHash)
{
    bcostars::Transaction inner{};
    inner.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    inner.extraTransactionBytes.assign(preimage.begin(), preimage.end());
    inner.signature.assign(signature.begin(), signature.end());
    inner.extraTransactionHash.assign(canonicalHash.begin(), canonicalHash.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [stored = std::move(inner)]() mutable { return std::addressof(stored); });
}

// BYTE-EXACT integration: a block carrying the five known-answer signed transactions plus a
// deposit must come back with every envelope byte-identical to the reference wire form, each
// hashing to its frozen canonical transaction hash, and the whole list reproducing the header's
// transactionsRoot.
//
// This is the test the review asked for (DebugEndpoint.cpp:74): it exercises the actual splice
// the endpoint performs, for every supported type, instead of only the rejection paths.
BOOST_AUTO_TEST_CASE(RawBlockServesByteExactSignedTransactions)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);

    // Expected envelopes, in the order they will be placed in the block.
    std::vector<bcos::bytes> expected;
    std::vector<bcos::protocol::Transaction::Ptr> stored;
    for (auto const& v : SIGNED_TX_VECTORS)
    {
        auto const preimage = fromHex(v.preimageHex);
        auto const signature = fromHex(v.signatureHex);
        // The reference envelope for this vector is what the shared helper produces from the
        // stored pair; the frozen hash below is what independently pins it.
        auto envelope = bcostars::protocol::reassembleWeb3RawTransaction(
            bcos::ref(preimage), bcos::ref(signature));
        BOOST_CHECK_MESSAGE(bcos::crypto::keccak256Hash(bcos::ref(envelope)).hexPrefixed() ==
                                std::string("0x") + v.canonicalHashHex,
            "vector " << v.name << " does not hash to its frozen canonical tx hash");
        expected.push_back(std::move(envelope));
        auto const canonicalHash = fromHex(v.canonicalHashHex);
        auto tx = makeStoredTransaction(
            bcos::ref(preimage), bcos::ref(signature), bcos::ref(canonicalHash));
        // Register the tx by hash so the per-transaction endpoint can find it, exactly as a commit
        // seeds the SYS_HASH_2_TX row.
        m_ledger->storeTxAndReceipt(tx, nullptr);
        stored.push_back(std::move(tx));
    }
    // Deposit: no signature, the preimage is already the whole envelope.
    {
        auto deposit = buildDepositEnvelope();
        auto const depositHash = bcos::crypto::keccak256Hash(bcos::ref(deposit));
        auto tx = makeStoredTransaction(bcos::ref(deposit), bcos::bytesConstRef{},
            bcos::bytesConstRef(depositHash.data(), depositHash.size()));
        m_ledger->storeTxAndReceipt(tx, nullptr);
        expected.push_back(std::move(deposit));
        stored.push_back(std::move(tx));
    }

    // Assemble the block around them.
    auto const number = m_ledger->blockNumber() + 1;
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(1'700'000'000'000);
    header->setBaseFee(7);
    std::vector<bcos::bytesConstRef> refs;
    refs.reserve(expected.size());
    for (auto const& tx : expected)
    {
        refs.emplace_back(tx.data(), tx.size());
    }
    header->setTxsRoot(bcos::ledger::mpt::calculateTransactionsRoot(refs));
    header->setWithdrawalsRoot(mpt::emptyRootHash());
    BOOST_REQUIRE(bcos::protocol::isOpEthereumBlock(*header));
    header->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*header));
    block->setBlockHeader(header);
    block->setBlockType(protocol::CompleteBlock);
    for (auto& tx : stored)
    {
        block->appendTransaction(tx);
    }
    m_ledger->appendBuiltBlock(block);
    m_ledger->indexBlockHash(header->hash(), number);
    {
        storage::Entry withdrawalsRow;
        withdrawalsRow.set(bcos::bytes{0xc0});
        m_ledger->setStateStorageEntry(std::string(ledger::SYS_NUMBER_2_WITHDRAWALS),
            std::to_string(number), std::move(withdrawalsRow));
    }

    auto const rawBlock = resultBytes(call1("debug_getRawBlock", toQuantity(number)));
    bcos::protocol::EthBlock decoded;
    auto mutableBlock = rawBlock;
    decoded.rlpDecode(bcos::bytesConstRef(mutableBlock.data(), mutableBlock.size()));

    BOOST_REQUIRE_EQUAL(decoded.data().transactions.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        BOOST_CHECK_MESSAGE(decoded.data().transactions[i] == expected[i],
            "block transaction " << i << " is not byte-identical to the wire form");
    }
    BOOST_CHECK(bcos::ledger::mpt::calculateTransactionsRoot(refs) == header->txsRoot());

    // The per-transaction endpoint must serve the same bytes for a signed one.
    auto const oneEnvelope = call1("debug_getRawTransaction",
        bcos::crypto::keccak256Hash(bcos::ref(expected[0])).hexPrefixed());
    BOOST_REQUIRE(!oneEnvelope.isMember("error"));
    BOOST_CHECK_EQUAL(oneEnvelope["result"].asString(), toHexStringWithPrefix(expected[0]));
}

// REGRESSION: the ENGINE/OP storage carrier, which is what a block that arrived through
// engine_newPayload actually holds. `decodedTransactionFromEnvelope` and `wrapOpP2pEnvelope` both
// overwrite extraTransactionBytes with the FULL wire envelope (that is the byte string the OP lane
// hashes into txsRoot — OpScheduler's rawTxBytes), while the row still carries the r||s||yParity
// signature. Unconditionally splicing the signature back on therefore appends to an already-signed
// envelope: the previous implementation would corrupt or reject every such transaction, i.e. every
// block on a real OP node.
//
// The pool carrier (preimage + signature) is covered by RawBlockServesByteExactSignedTransactions
// above; this is its twin, with the same expected bytes and the opposite stored shape.
BOOST_AUTO_TEST_CASE(RawBlockServesEngineCarriedFullEnvelopes)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);

    std::vector<bcos::bytes> expected;
    std::vector<bcos::protocol::Transaction::Ptr> stored;
    for (auto const& v : SIGNED_TX_VECTORS)
    {
        auto const signature = fromHex(v.signatureHex);
        auto envelope = bcostars::protocol::reassembleWeb3RawTransaction(
            bcos::ref(fromHex(v.preimageHex)), bcos::ref(signature));
        BOOST_REQUIRE_EQUAL(bcos::crypto::keccak256Hash(bcos::ref(envelope)).hexPrefixed(),
            std::string("0x") + v.canonicalHashHex);

        // The engine shape: the envelope itself in extraTransactionBytes, the signature still
        // present beside it. Splicing on top of this is the bug this test pins.
        auto tx = makeStoredTransaction(
            bcos::ref(envelope), bcos::ref(signature), bcos::ref(fromHex(v.canonicalHashHex)));
        m_ledger->storeTxAndReceipt(tx, nullptr);
        expected.push_back(std::move(envelope));
        stored.push_back(std::move(tx));
    }
    // A deposit in the same carrier: full envelope, empty signature.
    {
        auto deposit = buildDepositEnvelope();
        auto const depositHash = bcos::crypto::keccak256Hash(bcos::ref(deposit));
        auto tx = makeStoredTransaction(bcos::ref(deposit), bcos::bytesConstRef{},
            bcos::bytesConstRef(depositHash.data(), depositHash.size()));
        m_ledger->storeTxAndReceipt(tx, nullptr);
        expected.push_back(std::move(deposit));
        stored.push_back(std::move(tx));
    }

    auto const number = m_ledger->blockNumber() + 1;
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(1'700'000'000'000);
    header->setBaseFee(7);
    std::vector<bcos::bytesConstRef> refs;
    refs.reserve(expected.size());
    for (auto const& tx : expected)
    {
        refs.emplace_back(tx.data(), tx.size());
    }
    header->setTxsRoot(bcos::ledger::mpt::calculateTransactionsRoot(refs));
    header->setWithdrawalsRoot(mpt::emptyRootHash());
    BOOST_REQUIRE(bcos::protocol::isOpEthereumBlock(*header));
    header->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*header));
    block->setBlockHeader(header);
    block->setBlockType(protocol::CompleteBlock);
    for (auto& tx : stored)
    {
        block->appendTransaction(tx);
    }
    m_ledger->appendBuiltBlock(block);
    m_ledger->indexBlockHash(header->hash(), number);

    // Every transaction must come back byte-identical: nothing was appended to the envelope.
    auto const rawBlock = resultBytes(call1("debug_getRawBlock", toQuantity(number)));
    bcos::protocol::EthBlock decoded;
    auto mutableBlock = rawBlock;
    decoded.rlpDecode(bcos::bytesConstRef(mutableBlock.data(), mutableBlock.size()));
    BOOST_REQUIRE_EQUAL(decoded.data().transactions.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        BOOST_CHECK_MESSAGE(decoded.data().transactions[i] == expected[i],
            "engine-carried transaction " << i << " is not byte-identical to the wire form");
    }
    // And the body still reproduces the committed root (the endpoint checks this itself; asserting
    // it here names the failure if the served bytes ever drift).
    BOOST_CHECK(bcos::ledger::mpt::calculateTransactionsRoot(refs) == header->txsRoot());

    auto const oneEnvelope = call1("debug_getRawTransaction",
        bcos::crypto::keccak256Hash(bcos::ref(expected[0])).hexPrefixed());
    BOOST_REQUIRE_MESSAGE(!oneEnvelope.isMember("error"), printJson(oneEnvelope));
    BOOST_CHECK_EQUAL(oneEnvelope["result"].asString(), toHexStringWithPrefix(expected[0]));
}

// REGRESSION (found only against a real node): a block header the ledger rebuilt from its stored
// SYS_NUMBER_2_BLOCK_HEADER row carries NO dataHash, so BlockHeaderImpl::hash() throws
// EmptyBlockHeaderHash. The endpoint used to call canonicalBlockHash(header) here, which reaches
// hash() on the eth lane and turned every debug_getRawBlock on such a node into
// -32603 "Throw in function ...BlockHeaderImpl::hash()". The identity of the bytes served is
// keccak256(encodeHeader(header)) -- which is exactly what a client recomputes -- so the header's
// stored hash must not be load-bearing. This test clears dataHash on purpose to pin that down.
BOOST_AUTO_TEST_CASE(RawBlockServesHeaderWithoutStoredHash)
{
    setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);

    auto const number = m_ledger->blockNumber() + 1;
    m_ledger->addNewBlock(nullptr, number, 0);
    auto header = m_ledger->ledgerData().back()->blockHeader();
    header->setTimestamp(1'700'000'000);
    header->setBaseFee(1'000'000'000);
    header->setTxsRoot(mpt::emptyRootHash());
    header->setWithdrawalsRoot(mpt::emptyRootHash());
    // The shape the ledger hands back: encodable, but with no stored hash. Every tars setter
    // clears dataHash, so re-stamping an unchanged field drops the hash the calculateHash above
    // would otherwise leave behind -- reproducing exactly what a decoded storage row looks like.
    header->setStateRoot(header->stateRoot());

    // Empty withdrawals commitment with no sidecar row is the genesis shape, served as [].
    auto const rlp = protocol::EthBlockHeader::encodeHeader(*header);
    auto const identity = bcos::crypto::keccak256Hash(bcos::ref(rlp));
    m_ledger->indexBlockHash(identity, number);

    auto const raw = resultBytes(call1("debug_getRawBlock", toQuantity(number)));
    bcos::protocol::EthBlock decoded;
    auto mutableRaw = raw;
    decoded.rlpDecode(bcos::bytesConstRef(mutableRaw.data(), mutableRaw.size()));
    // The served header must hash to the published identity of the block just served, and the
    // body must be the empty transaction list this header commits to.
    BOOST_CHECK_EQUAL(
        bcos::crypto::keccak256Hash(bcos::ref(protocol::EthBlockHeader::encodeHeader(*header)))
            .hexPrefixed(),
        identity.hexPrefixed());
    BOOST_CHECK(decoded.data().transactions.empty());
    // And a hash request for that identity serves the same block (the stale-index guard compares
    // against the encoded header, not against a stored hash that is absent).
    BOOST_CHECK(resultBytes(call1("debug_getRawBlock", identity.hexPrefixed())) == raw);
}

// REGRESSION (found only against a real node): the genesis block commits to the empty-withdrawals
// root but has no SYS_NUMBER_2_WITHDRAWALS row (there is nothing to store). Refusing it made
// debug_getRawBlock(0x0) -- and therefore the "earliest" tag -- unavailable. A missing row is
// legitimate IFF the committed root is the empty-trie root; anything else must still fail closed,
// or the endpoint would emit a body that contradicts its own header.
BOOST_AUTO_TEST_CASE(RawBlockServesEmptyWithdrawalsWhenSidecarRowIsAbsent)
{
    setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);

    auto const number = m_ledger->blockNumber() + 1;
    m_ledger->addNewBlock(nullptr, number, 0);
    auto header = m_ledger->ledgerData().back()->blockHeader();
    header->setTimestamp(1'700'000'000);
    header->setBaseFee(1'000'000'000);
    header->setTxsRoot(mpt::emptyRootHash());
    header->setWithdrawalsRoot(mpt::emptyRootHash());  // the empty list's commitment
    // No sidecar row is written for this block on purpose.

    auto const raw = resultBytes(call1("debug_getRawBlock", toQuantity(number)));
    bcos::protocol::EthBlock decoded;
    auto mutableRaw = raw;
    decoded.rlpDecode(bcos::bytesConstRef(mutableRaw.data(), mutableRaw.size()));
    BOOST_REQUIRE(decoded.data().withdrawals.has_value());
    BOOST_CHECK(decoded.data().withdrawals->empty());
    // Re-deriving the root from the served body must reproduce the header's commitment.
    std::vector<bcos::bytesConstRef> refs;  // empty list
    BOOST_CHECK(bcos::ledger::mpt::calculateWithdrawalsRoot(refs) == *header->withdrawalsRoot());

    // Negative control: a NON-empty commitment with no row is a genuinely missing sidecar and
    // must be refused rather than answered with an empty list.
    auto const number2 = m_ledger->blockNumber() + 1;
    m_ledger->addNewBlock(nullptr, number2, 0);
    auto header2 = m_ledger->ledgerData().back()->blockHeader();
    header2->setTimestamp(1'700'001'000);  // whole seconds: the eth header encodes no ms
    header2->setBaseFee(1'000'000'000);
    header2->setTxsRoot(mpt::emptyRootHash());
    header2->setWithdrawalsRoot(h256(0xabcdU));  // commits to something that is not "empty"
    auto const refused = call1("debug_getRawBlock", toQuantity(number2));
    BOOST_REQUIRE_MESSAGE(refused.isMember("error"),
        "a non-empty withdrawals commitment with no "
        "sidecar row must be refused, got: "
            << printJson(refused));
    BOOST_CHECK_MESSAGE(refused["error"]["message"].asString().find("withdrawals row is missing") !=
                            std::string::npos,
        "expected the missing-sidecar refusal, got: " << printJson(refused));
}

// Isthmus+ on the OP lane: the body's withdrawals list is always empty, but the header field is
// the L2ToL1MessagePasser storage root, NOT the empty list's trie root
// (opstack-executor/OpEthBlockExecute.h "withdrawalsRoot by fork"). The endpoint must serve the
// header verbatim with an empty fourth element -- it cannot recompute that root from the body, and
// refusing the mismatch would make every non-empty message-passer block (i.e. every real Isthmus
// L2 block with any withdrawal ever proven) unservable to op-succinct.
BOOST_AUTO_TEST_CASE(RawBlockServesIsthmusOpBlockWithMessagePasserRoot)
{
    setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);

    auto const number = m_ledger->blockNumber() + 1;
    m_ledger->addNewBlock(nullptr, number, 0);
    auto header = m_ledger->ledgerData().back()->blockHeader();
    header->setTimestamp(1'700'000'000);
    header->setBaseFee(7);
    header->setTxsRoot(mpt::emptyRootHash());
    // A message-passer storage root: nothing the empty withdrawals list can reproduce.
    auto const messagePasserRoot = h256(0x1234U);
    BOOST_REQUIRE(messagePasserRoot != mpt::emptyRootHash());
    header->setWithdrawalsRoot(messagePasserRoot);
    BOOST_REQUIRE(bcos::protocol::isOpEthereumBlock(*header));
    header->setRLPHash(bcos::protocol::EthBlockHeader::computeHash(*header));
    auto const blockHash = header->hash();
    m_ledger->indexBlockHash(blockHash, number);

    auto const raw = resultBytes(call1("debug_getRawBlock", toQuantity(number)));
    bcos::protocol::EthBlock decoded;
    auto mutableRaw = raw;
    decoded.rlpDecode(bcos::bytesConstRef(mutableRaw.data(), mutableRaw.size()));

    // Fourth element present and empty, exactly as an OP body must be.
    BOOST_REQUIRE(decoded.data().withdrawals.has_value());
    BOOST_CHECK(decoded.data().withdrawals->empty());
    // The header is served verbatim: its root is the message-passer root, untouched, and the
    // re-encoded header still hashes to the published block hash.
    BOOST_REQUIRE(decoded.data().header.withdrawalsHash.has_value());
    BOOST_CHECK(*decoded.data().header.withdrawalsHash == messagePasserRoot);
    bytes headerRlp;
    codec::rlp::encode(headerRlp, decoded.data().header);
    BOOST_CHECK_EQUAL(bcos::crypto::keccak256Hash(bytesConstRef(headerRlp.data(), headerRlp.size()))
                          .hexPrefixed(),
        blockHash.hexPrefixed());

    // Same answer through the hash parameter (the identity check must not trip on the root).
    BOOST_CHECK(resultBytes(call1("debug_getRawBlock", blockHash.hexPrefixed())) == raw);
}

// F1, the positive half: the ledger reports an absent SYS_HASH_2_TX row by failing the batch with
// a count mismatch. That exact shape is geth's -32000 "not found" and nothing else.
BOOST_AUTO_TEST_CASE(RawTransactionMissingRowIsNotFound)
{
    auto faulty = std::make_shared<FaultyReadLedger>(m_blockFactory, 20, 10, 10);
    faulty->setSystemConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
        std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
    faulty->mode = FaultyReadLedger::Mode::Missing;
    useLedger(faulty);

    auto const resp = call1("debug_getRawTransaction", std::string("0x") + std::string(64, '7'));
    BOOST_REQUIRE_MESSAGE(resp.isMember("error"), printJson(resp));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), Web3DefaultError);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "not found");
}

// F1, the half that was previously broken: a REAL read failure (storage error, not a missing row)
// must keep its diagnostics and surface as an internal error. Answering "not found" here would
// tell kona-host to retry forever against a node that cannot serve the data at all.
BOOST_AUTO_TEST_CASE(RawTransactionReadFailureIsNotFlattenedIntoNotFound)
{
    auto faulty = std::make_shared<FaultyReadLedger>(m_blockFactory, 20, 10, 10);
    faulty->setSystemConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
        std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
    faulty->mode = FaultyReadLedger::Mode::Failing;
    useLedger(faulty);

    auto const resp = call1("debug_getRawTransaction", std::string("0x") + std::string(64, '7'));
    BOOST_REQUIRE_MESSAGE(resp.isMember("error"), printJson(resp));
    // Not -32000: this is an operational failure, so it must not look like a miss.
    BOOST_CHECK(resp["error"]["code"].asInt() != Web3DefaultError);
    BOOST_CHECK_MESSAGE(resp["error"]["message"].asString().find("not found") == std::string::npos,
        "a failed read must not be reported as a miss, got: " << printJson(resp));
}

// Focus #4 (lane consistency): the two places that decide "which lane" — NodeService's cached
// version, used by the body-shape branch, and the ledger's executor_version row, used by the lane
// gate — are filled from the SAME boot-time read (Initializer::init reads the on-chain row, with
// config.genesis executor.version as the fallback), and SystemConfigPrecompiled refuses a
// governance write that would enter the Ethereum lane, so the value cannot drift while the node
// runs. They can only disagree when the row is ABSENT and the genesis fallback was used; this pins
// that the disagreement is harmless: the gate reads the row, sees 0, and answers MethodNotFound
// instead of serving a body shaped by the other lane's rules.
BOOST_AUTO_TEST_CASE(RawBlockFailsClosedWhenLedgerHasNoLaneRow)
{
    // NodeService says OP; the ledger has no executor_version row (never set in this fixture).
    nodeService->setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);

    auto const resp = call1("debug_getRawBlock", "0x0");
    BOOST_REQUIRE_MESSAGE(resp.isMember("error"), printJson(resp));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), MethodNotFound);
    BOOST_TEST(!resp.isMember("result"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
