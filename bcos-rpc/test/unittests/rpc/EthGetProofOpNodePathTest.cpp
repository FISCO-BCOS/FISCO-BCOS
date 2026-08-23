/**
 * @file EthGetProofOpNodePathTest.cpp
 * @author: octopus
 * @date: 2026-08-23
 *        Pins the eth_getProof integration ordering on the OP path (baseFee-carrying
 *        headers, 5178d86db): the node-backed proof is tried FIRST — it serves ANY height
 *        including historical tags, which the one-version flat plane cannot — and the
 *        flat-state rebuild (7dbf4da6f) is a FALLBACK for roots without persisted rows
 *        (BlockNotCommitted), where it keeps the latest-only honest-refusal contract.
 *
 *        Discriminating design: the node-path cases deliberately do NOT wire the flat
 *        provider (a flat fallback there would answer -32603 "not wired"), and the
 *        fallback cases deliberately wire an EMPTY node plane (success can then only come
 *        from the flat rebuild).
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rpc/Rpc.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-storage/MPTNodeReadStorage.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

class EthGetProofOpNodePathFixture : public RPCFixture
{
public:
    /// The node plane: "/mpt/" rows standing in for the committed backend
    /// (GlobalStateStorage::latestBackend() in production).
    using StateRowStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

    /// The flat plane: the committed flat state stand-in (same shape as
    /// EthGetProofFlatStateTest).
    using FlatStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::LOGICAL_DELETION>;

    EthGetProofOpNodePathFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
        // An OP chain is scenario B since genesis — fullTrie walks, exclusion proofs for
        // absent slots (FakeLedger::fetchFeature mirrors this injection).
        bcos::ledger::Features features;
        features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
        m_ledger->setFeatures(std::move(features));
    }

    bcos::rpc::Rpc::Ptr rpc;
    bcos::rpc::Web3JsonRpcImpl::Ptr web3JsonRpc;

    // ---- deterministic addresses / values (same as the sibling fixtures) ----
    bcos::Address const targetAddr{
        std::string_view{"1111111111111111111111111111110000000001"}, bcos::Address::FromHex};
    bcos::h256 const slotA{"000000000000000000000000000000000000000000000000000000000000000a"};
    bcos::h256 const slotB{"000000000000000000000000000000000000000000000000000000000000000b"};
    bcos::h256 const neverSlot{"000000000000000000000000000000000000000000000000000000000000000e"};
    bcos::h256 const targetCodeHash{
        "2222222222222222222222222222222222222222222222222222222222222222"};
    /// 32-byte slot values; valueA trims to 0x64 (100) in its trie leaf.
    bcos::bytes const valueA = bcos::bytes(31, 0x00) + bcos::bytes{0x64};
    bcos::bytes const valueB = [] {
        bcos::bytes v(32, 0x77);
        v[0] = 0x11;
        return v;
    }();

    // ---- node plane (EthGetProofReaderWiringTest idiom) ----

    /// Commit @p entries into a trie whose nodes land as "/mpt/" state rows.
    bcos::h256 commitIntoStateRows(std::map<bcos::h256, bcos::bytes> const& entries)
    {
        std::map<bcos::h256, std::optional<bcos::bytes>> changes;
        for (auto const& [key, value] : entries)
        {
            changes[key] = value;
        }
        return task::syncWait([&]() -> task::Task<bcos::h256> {
            storage2::MPTNodeReadStorage reader(m_stateRows);
            auto result = co_await mpt::commitTrie(reader, mpt::emptyRootHash(), changes);
            for (auto const& [hash, rlp] : result.newNodes)
            {
                storage::Entry entry;
                entry.set(bcos::bytes(rlp));
                co_await storage2::writeOne(
                    m_stateRows, storage2::mptNodeStateKey(hash), std::move(entry));
            }
            co_return result.root;
        }());
    }

    /// Build the two-level trie for the target account and return its state root — every
    /// node of BOTH levels is persisted into m_stateRows (the ①a chain shape).
    bcos::h256 buildNodeTrie()
    {
        auto const storageRoot = commitIntoStateRows(
            {{mpt::slotKeyHash(slotA), mpt::encodeStorageValue({valueA.data(), valueA.size()})},
                {mpt::slotKeyHash(slotB),
                    mpt::encodeStorageValue({valueB.data(), valueB.size()})}});

        mpt::Account account;
        account.nonce = 7;
        account.balance = 1000000;
        account.storageRoot = storageRoot;
        account.codeHash = targetCodeHash;
        return commitIntoStateRows({{mpt::accountKeyHash(targetAddr), account.encode()}});
    }

    void wireReader() { nodeService->setMPTNodeReader(storage2::makeMPTNodeReader(m_stateRows)); }

    // ---- flat plane (EthGetProofFlatStateTest idiom, single account) ----

    void writeRow(std::string_view table, std::string_view field, bcos::bytes value)
    {
        storage::Entry entry;
        entry.set(std::move(value));
        task::syncWait(storage2::writeOne(m_flat,
            executor_v1::StateKey{executor_v1::StateKeyView{table, field}}, std::move(entry)));
    }

    void writeRow(std::string_view table, std::string_view field, std::string_view value)
    {
        writeRow(table, field, bcos::bytes(value.begin(), value.end()));
    }

    static std::string tableName(bcos::Address const& addr)
    {
        return std::string{bcos::ledger::SYS_DIRECTORY::USER_APPS} + addr.hex();
    }

    /// Raw 32-byte hash as the big-endian byte string the executor stores.
    static bcos::bytes toBytes(bcos::h256 const& hash)
    {
        return bcos::bytes(hash.ref().data(), hash.ref().data() + bcos::h256::SIZE);
    }

    /// Raw 32-byte slot key as the row-field string the executor writes.
    static std::string_view slotField(bcos::h256 const& slot)
    {
        return {reinterpret_cast<char const*>(slot.ref().data()), bcos::h256::SIZE};
    }

    /// The root the flat rows rebuild to — assembled with the same primitives the seal-time
    /// stateRootOf uses (computeTrieRoot over the account leaf).
    [[nodiscard]] bcos::h256 flatRoot() const
    {
        std::map<bcos::h256, bcos::bytes> storage;
        storage[mpt::slotKeyHash(slotA)] =
            mpt::encodeStorageValue(bcos::bytesConstRef(valueA.data(), valueA.size()));
        storage[mpt::slotKeyHash(slotB)] =
            mpt::encodeStorageValue(bcos::bytesConstRef(valueB.data(), valueB.size()));

        mpt::Account account;
        account.nonce = 7;
        account.balance = 1000000;
        account.storageRoot = mpt::computeTrieRoot(storage).root;
        account.codeHash = targetCodeHash;
        return mpt::computeTrieRoot({{mpt::accountKeyHash(targetAddr), account.encode()}}).root;
    }

    /// A root neither plane can produce — for the honest-refusal case.
    [[nodiscard]] bcos::h256 divergentRoot() const
    {
        mpt::Account divergent;
        divergent.balance = 43;
        return mpt::computeTrieRoot({{mpt::accountKeyHash(targetAddr), divergent.encode()}}).root;
    }

    void seedFlatState()
    {
        auto const table = tableName(targetAddr);
        writeRow(bcos::ledger::SYS_TABLES, table, "1");
        writeRow(table, mpt::ROW_BALANCE, "1000000");
        writeRow(table, mpt::ROW_NONCE, "7");
        writeRow(table, mpt::ROW_CODE_HASH, toBytes(targetCodeHash));
        writeRow(table, slotField(slotA), valueA);
        writeRow(table, slotField(slotB), valueB);
    }

    void wireProvider()
    {
        nodeService->setStateStorageProvider([this]() { return makeOwningFlatStorage(m_flat); });
    }

    // ---- verifyProof round trip (verbatim from EthGetProofRpcTest) ----

    /// A JSON quantity ("0x2a") back to trimmed big-endian bytes; "0x0" -> empty.
    static bytes quantityToBytes(std::string const& quantity)
    {
        std::string hex = quantity;
        if (hex.starts_with("0x") || hex.starts_with("0X"))
        {
            hex = hex.substr(2);
        }
        if (hex.empty() || hex == "0")
        {
            return {};
        }
        if (hex.size() % 2 != 0)
        {
            hex.insert(0, "0");
        }
        return fromHex(hex);
    }

    /// Rebuild the binary EIP1186Proof from the endpoint's JSON: hex fields back to bytes, and
    /// each slot "value" (a QUANTITY per EIP-1186) back to the trie's RLP leaf encoding via
    /// encodeStorageValue — verifyProof compares against the raw leaf bytes.
    static mpt::EIP1186Proof proofFromJson(Json::Value const& result)
    {
        mpt::EIP1186Proof out;
        out.address = Address(result["address"].asString(), Address::FromHex);
        out.balance = fromBigQuantity(result["balance"].asString());
        out.nonce = fromBigQuantity(result["nonce"].asString());
        out.codeHash = h256(result["codeHash"].asString(), h256::FromHex);
        out.storageHash = h256(result["storageHash"].asString(), h256::FromHex);
        for (auto const& node : result["accountProof"])
        {
            out.accountProof.push_back(fromHexWithPrefix(node.asString()));
        }
        for (auto const& entryJson : result["storageProof"])
        {
            mpt::StorageProof entry;
            entry.key = h256(entryJson["key"].asString(), h256::FromHex);
            auto raw = quantityToBytes(entryJson["value"].asString());
            entry.value = mpt::encodeStorageValue(bcos::ref(raw));
            for (auto const& node : entryJson["proof"])
            {
                entry.proof.push_back(fromHexWithPrefix(node.asString()));
            }
            out.storageProof.push_back(std::move(entry));
        }
        return out;
    }

    // ---- OP-path header stamping at an arbitrary height ----

    void stampOpHeader(protocol::BlockNumber number, bcos::h256 const& root)
    {
        for (auto const& block : m_ledger->ledgerData())
        {
            if (block->blockHeader()->number() == number)
            {
                block->blockHeader()->setBaseFee(bcos::u256(7));  // the OP discriminator
                block->blockHeader()->setStateRoot(root);
                return;
            }
        }
        BOOST_FAIL("no block " + std::to_string(number) + " in the fake ledger");
    }

    [[nodiscard]] protocol::BlockNumber latestNumber() const
    {
        return m_ledger->ledgerData().back()->blockHeader()->number();
    }

    // ---- JSON RPC plumbing (same as the sibling fixtures) ----

    Json::Value request(std::string const& req)
    {
        Json::Value value;
        Json::Reader reader;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(req, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Json::Value getProof(
        std::string const& addressHex, std::vector<std::string> const& keys, std::string const& tag)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getProof";
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        Json::Value keysJson(Json::arrayValue);
        for (auto const& key : keys)
        {
            keysJson.append(key);
        }
        params.append(keysJson);
        params.append(tag);
        req["params"] = params;
        return request(printJson(req));
    }

    StateRowStorage m_stateRows;
    FlatStorage m_flat;
};

BOOST_FIXTURE_TEST_SUITE(EthGetProofOpNodePathTest, EthGetProofOpNodePathFixture)

// Historical height + persisted nodes → the node-backed proof serves. No flat provider is
// wired: had the endpoint taken the flat path it would answer -32603 "not wired" — success
// here can only come from the node plane, at a height the one-version flat plane cannot
// serve. This is the S-RPC-6 headline capability (①a chains prove ANY historical block).
BOOST_AUTO_TEST_CASE(NodePathServesHistoricalBlock)
{
    auto const root = buildNodeTrie();
    stampOpHeader(3, root);
    wireReader();  // deliberately NO flat provider

    auto resp =
        getProof("0x" + targetAddr.hex(), {"0x" + slotA.hex(), "0x" + neverSlot.hex()}, "0x3");
    BOOST_REQUIRE(!resp.isMember("error"));

    auto const& result = resp["result"];
    BOOST_CHECK_EQUAL(result["balance"].asString(), "0xf4240");  // 1000000
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x7");
    BOOST_CHECK_EQUAL(result["codeHash"].asString(), "0x" + targetCodeHash.hex());
    BOOST_CHECK(result["accountProof"].isArray());
    BOOST_CHECK_GT(result["accountProof"].size(), 0U);

    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 2U);
    auto const& present = result["storageProof"][0U];
    BOOST_CHECK_EQUAL(present["key"].asString(), "0x" + slotA.hex());
    BOOST_CHECK_EQUAL(present["value"].asString(), "0x64");  // valueA trims to 100
    BOOST_CHECK_GT(present["proof"].size(), 0U);
    auto const& absent = result["storageProof"][1U];
    // Scenario B: the storage tries are complete, so absence is a provable zero —
    // an exclusion proof, never the scenario-A empty shape.
    BOOST_CHECK_EQUAL(absent["value"].asString(), "0x0");
    BOOST_CHECK_GT(absent["proof"].size(), 0U);

    // Round trip: the JSON-serialized proof must still verify against the stamped root.
    auto const verify = mpt::verifyProof(root, proofFromJson(result));
    BOOST_CHECK(verify.accountValid);
    BOOST_REQUIRE_EQUAL(verify.storageValid.size(), 2U);
    BOOST_CHECK(verify.storageValid[0]);
    BOOST_CHECK(verify.storageValid[1]);
}

// Reader wired but the requested root has no "/mpt/" rows (a pre-①a chain segment) and the
// tag is LATEST → the flat rebuild engages and serves. Success can only come from the flat
// plane: the node attempt reports BlockNotCommitted for a root with no rows.
BOOST_AUTO_TEST_CASE(NodeMissingRootFallsBackToFlatOnLatest)
{
    seedFlatState();
    stampOpHeader(latestNumber(), flatRoot());
    wireReader();  // node plane EMPTY: the stamped root has no rows
    wireProvider();

    auto resp = getProof("0x" + targetAddr.hex(), {"0x" + slotA.hex()}, "latest");
    BOOST_REQUIRE(!resp.isMember("error"));
    auto const& result = resp["result"];
    BOOST_CHECK_EQUAL(result["balance"].asString(), "0xf4240");
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x7");
    BOOST_CHECK_GT(result["accountProof"].size(), 0U);
    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 1U);
    BOOST_CHECK_EQUAL(result["storageProof"][0U]["value"].asString(), "0x64");
}

// Reader wired, no rows, HISTORICAL tag → the flat fallback refuses honestly (-32004, the
// 7dbf4da6f contract): the flat plane holds exactly one state version and must never dress
// the latest state up as an older block's proof.
BOOST_AUTO_TEST_CASE(NodeMissingHistoricalKeepsHonestRefusal)
{
    seedFlatState();
    stampOpHeader(3, divergentRoot());  // a root neither plane has
    wireReader();
    wireProvider();

    auto resp = getProof("0x" + targetAddr.hex(), {}, "0x3");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(resp["error"]["message"].asString().find("latest committed") != std::string::npos);
}

// Node path serves genesis too (rows come from the l2EthereumCompat genesis import) —
// pinning that the node-first ordering did not regress the genesis case that the pure
// flat path could not answer.
BOOST_AUTO_TEST_CASE(NodePathServesGenesisBlock)
{
    auto const root = buildNodeTrie();
    stampOpHeader(0, root);
    wireReader();

    auto resp = getProof("0x" + targetAddr.hex(), {}, "0x0");
    BOOST_REQUIRE(!resp.isMember("error"));
    BOOST_CHECK_GT(resp["result"]["accountProof"].size(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
