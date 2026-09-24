// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSepoliaReplayTest — M5a acceptance: replay REAL op-sepolia blocks from genesis
// against OpBlockVerifier<MultiLayerStorage>::verifyAndCommit(devp2p::sync::Block),
// the exact entry the devp2p sync loop drives. A public JSON-RPC endpoint supplies
// the canonical blocks (headers via eth_getBlockByNumber, opaque EIP-2718 envelopes
// via batched eth_getRawTransactionByHash); every announced header commitment is
// re-derived and compared INSIDE the verifier, so a divergence surfaces as
// OpBlockVerificationFailed carrying block number, field, and both values.
//
// Gated by environment (CI-safe: unset => the case passes with zero assertions):
//   OP_SEPOLIA_REPLAY_RPC      (required) e.g. https://optimism-sepolia-rpc.publicnode.com
//   OP_SEPOLIA_REPLAY_GENESIS  (default /tmp/op-sepolia-config.genesis) full config.genesis
//   OP_SEPOLIA_REPLAY_BLOCKS   (default 1000) blocks to replay
//   OP_SEPOLIA_REPLAY_START    (default 1) first block to replay
//
// The whole body (genesis build + replay loop) runs on a worker thread with a 1 GiB
// stack reservation: this repo's ASAN configuration does not tail-call the
// bcos::task symmetric-transfer resume, so a long per-block co_await chain accumulates
// native frames. That is a test-side guard only — production feeds blocks from the
// sync loop one task per block.

#include <opstack-executor/OpBlockVerifier.h>

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-devp2p/sync/Block.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-table/src/LegacyStorageWrapper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <curl/curl.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>
#include <boost/thread/thread_only.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{

constexpr uint64_t kOpSepoliaChainId = 11155420;
constexpr int64_t kFetchWindow = 25;  // blocks per header batch; raw txs of the window batch too

// ── fixture pieces (same shape as OpBlockVerifierTest; anonymous-namespace fixture
//    pieces are deliberately not shared across test TUs) ──

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) & { std::abort(); }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using Verifier = bcos::executor_v1::opstack::OpBlockVerifier<MLS>;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

// ── hex / JSON-RPC helpers (same mapping as tools/eth-sync-check/main.cpp) ──

std::string stripHexPrefix(std::string const& hex)
{
    return hex.rfind("0x", 0) == 0 ? hex.substr(2) : hex;
}

bcos::bytes hexToBytes(std::string const& hex)
{
    return bcos::fromHex(stripHexPrefix(hex));
}

uint64_t hexToU64(std::string const& hex)
{
    return std::stoull(stripHexPrefix(hex), nullptr, 16);
}

int64_t hexToI64(std::string const& hex)
{
    return static_cast<int64_t>(hexToU64(hex));
}

std::string hexQuantity(int64_t number)
{
    std::ostringstream ss;
    ss << "0x" << std::hex << number;
    return ss.str();
}

/// Map an eth_getBlockByNumber JSON object onto the pure Ethereum header struct —
/// fork-gated optionals follow PRESENCE in the JSON (verbatim from eth-sync-check).
bcos::protocol::EthBlockHeaderData headerFromJson(Json::Value const& j)
{
    bcos::protocol::EthBlockHeaderData h;
    h.parentInfo.blockHash = bcos::crypto::HashType(
        std::string_view(stripHexPrefix(j["parentHash"].asString())), bcos::crypto::HashType::FromHex);
    h.uncleHash = bcos::crypto::HashType(
        std::string_view(stripHexPrefix(j["sha3Uncles"].asString())), bcos::crypto::HashType::FromHex);
    h.stateRoot = bcos::crypto::HashType(
        std::string_view(stripHexPrefix(j["stateRoot"].asString())), bcos::crypto::HashType::FromHex);
    h.txsRoot = bcos::crypto::HashType(
        std::string_view(stripHexPrefix(j["transactionsRoot"].asString())),
        bcos::crypto::HashType::FromHex);
    h.receiptsRoot = bcos::crypto::HashType(
        std::string_view(stripHexPrefix(j["receiptsRoot"].asString())),
        bcos::crypto::HashType::FromHex);
    auto bloomBytes = hexToBytes(j["logsBloom"].asString());
    std::copy(bloomBytes.begin(), bloomBytes.end(), h.logsBloom.begin());
    h.difficulty = bcos::u256(j["difficulty"].asString());
    h.gasLimit = bcos::u256(j["gasLimit"].asString());
    h.gasUsed = bcos::u256(j["gasUsed"].asString());
    auto randao = hexToBytes(j["mixHash"].asString());
    std::copy(randao.begin(), randao.end(), h.prevRandao.begin());
    h.extraData = hexToBytes(j["extraData"].asString());
    auto coinbase = hexToBytes(j["miner"].asString());
    if (coinbase.size() == 20)
    {
        std::copy(coinbase.begin(), coinbase.end(), h.coinbase.begin());
    }
    auto nonce = hexToBytes(j["nonce"].asString());
    if (nonce.size() == 8)
    {
        std::copy(nonce.begin(), nonce.end(), h.nonce.begin());
    }
    h.number = hexToI64(j["number"].asString());
    h.timestamp = hexToI64(j["timestamp"].asString());
    if (j.isMember("baseFeePerGas"))
    {
        h.baseFee = bcos::u256(j["baseFeePerGas"].asString());
    }
    if (j.isMember("withdrawalsRoot"))
    {
        h.withdrawalsHash = bcos::crypto::HashType(
            std::string_view(stripHexPrefix(j["withdrawalsRoot"].asString())),
            bcos::crypto::HashType::FromHex);
    }
    if (j.isMember("blobGasUsed"))
    {
        h.blobGasUsed = bcos::u256(j["blobGasUsed"].asString());
    }
    if (j.isMember("excessBlobGas"))
    {
        h.excessBlobGas = bcos::u256(j["excessBlobGas"].asString());
    }
    if (j.isMember("parentBeaconBlockRoot"))
    {
        h.parentBeaconRoot = bcos::crypto::HashType(
            std::string_view(stripHexPrefix(j["parentBeaconBlockRoot"].asString())),
            bcos::crypto::HashType::FromHex);
    }
    if (j.isMember("requestsHash"))
    {
        h.requestsHash = bcos::crypto::HashType(
            std::string_view(stripHexPrefix(j["requestsHash"].asString())),
            bcos::crypto::HashType::FromHex);
    }
    return h;
}

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

/// JSON-RPC batch client over HTTP. One call = one HTTP POST carrying the whole batch;
/// results are matched back by request id. Transport/parse failures retry with backoff.
class RpcClient
{
public:
    explicit RpcClient(std::string url) : m_url(std::move(url)) {}

    std::vector<Json::Value> batch(
        std::vector<std::pair<std::string, std::string>> const& calls) const
    {
        std::string body = "[";
        for (size_t i = 0; i < calls.size(); ++i)
        {
            if (i != 0)
            {
                body += ",";
            }
            body += "{\"jsonrpc\":\"2.0\",\"method\":\"" + calls[i].first +
                    "\",\"params\":" + calls[i].second + ",\"id\":" + std::to_string(i) + "}";
        }
        body += "]";

        std::string lastError;
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            if (attempt > 0)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500 * (1 << (attempt - 1))));
            }
            std::string response;
            CURL* curl = curl_easy_init();
            if (!curl)
            {
                lastError = "curl_easy_init failed";
                continue;
            }
            curl_easy_setopt(curl, CURLOPT_URL, m_url.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "op-sepolia-replay-test/1.0");
            CURLcode rc = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            if (rc != CURLE_OK)
            {
                lastError = std::string("curl: ") + curl_easy_strerror(rc);
                continue;
            }
            Json::Value root;
            Json::Reader reader;
            if (!reader.parse(response, root) || !root.isArray())
            {
                lastError = "unparseable batch response: " + response.substr(0, 200);
                continue;
            }
            std::vector<Json::Value> results(calls.size());
            std::vector<bool> seen(calls.size(), false);
            bool rpcError = false;
            for (auto const& entry : root)
            {
                auto const id = entry["id"].asUInt();
                if (entry["error"].isObject())
                {
                    lastError = "RPC error on id " + std::to_string(id) + ": " +
                                entry["error"]["message"].asString();
                    rpcError = true;
                    break;
                }
                if (id >= calls.size())
                {
                    lastError = "batch response id out of range";
                    rpcError = true;
                    break;
                }
                results[id] = entry["result"];
                seen[id] = true;
            }
            if (rpcError)
            {
                continue;
            }
            bool allSeen = true;
            for (size_t i = 0; i < seen.size(); ++i)
            {
                if (!seen[i])
                {
                    lastError = "batch response missing id " + std::to_string(i);
                    allSeen = false;
                    break;
                }
            }
            if (allSeen)
            {
                return results;
            }
        }
        throw std::runtime_error("RPC batch failed after retries: " + lastError);
    }

private:
    std::string m_url;
};

// ── block fetching ──

struct FetchedBlock
{
    bcos::protocol::EthBlockHeaderData header;
    bcos::h256 announcedHash;                 // the RPC-announced block hash
    std::vector<std::string> txHashHex;       // on-chain tx hashes (announcement)
    std::vector<bcos::bytes> rawTxs;          // opaque EIP-2718 envelopes
};

/// Fetch `count` blocks starting at `startNum`: one batch of eth_getBlockByNumber
/// (hash lists only), then one flattened batch of eth_getRawTransactionByHash for
/// every transaction of the window.
std::vector<FetchedBlock> fetchWindow(RpcClient const& client, int64_t startNum, int64_t count)
{
    std::vector<std::pair<std::string, std::string>> headerCalls;
    headerCalls.reserve(count);
    for (int64_t i = 0; i < count; ++i)
    {
        headerCalls.emplace_back("eth_getBlockByNumber",
            "[\"" + hexQuantity(startNum + i) + "\",false]");
    }
    auto headers = client.batch(headerCalls);

    std::vector<FetchedBlock> blocks(count);
    std::vector<std::pair<std::string, std::string>> txCalls;
    std::vector<std::pair<size_t, size_t>> txOwner;  // (block index, tx index)
    for (int64_t i = 0; i < count; ++i)
    {
        auto const& j = headers[i];
        if (!j.isObject() || !j.isMember("number"))
        {
            throw std::runtime_error(
                "eth_getBlockByNumber returned no block for " + std::to_string(startNum + i));
        }
        auto& b = blocks[i];
        b.header = headerFromJson(j);
        if (b.header.number != startNum + i)
        {
            throw std::runtime_error("block number mismatch: asked " +
                                     std::to_string(startNum + i) + ", got " +
                                     std::to_string(b.header.number));
        }
        b.announcedHash = bcos::crypto::HashType(
            std::string_view(stripHexPrefix(j["hash"].asString())), bcos::crypto::HashType::FromHex);
        for (auto const& txHash : j["transactions"])
        {
            b.txHashHex.push_back(txHash.asString());
            txCalls.emplace_back("eth_getRawTransactionByHash",
                "[\"" + txHash.asString() + "\"]");
            txOwner.emplace_back(static_cast<size_t>(i), b.txHashHex.size() - 1);
        }
        b.rawTxs.resize(b.txHashHex.size());
    }
    if (!txCalls.empty())
    {
        auto raws = client.batch(txCalls);
        for (size_t k = 0; k < raws.size(); ++k)
        {
            if (!raws[k].isString())
            {
                throw std::runtime_error(
                    "eth_getRawTransactionByHash returned no raw tx for " +
                    blocks[txOwner[k].first].txHashHex[txOwner[k].second]);
            }
            blocks[txOwner[k].first].rawTxs[txOwner[k].second] =
                hexToBytes(raws[k].asString());
        }
    }
    return blocks;
}

// ── the replay itself ──

struct ReplayOptions
{
    std::string rpcUrl;
    std::string genesisPath;
    int64_t startBlock = 1;
    int64_t blockCount = 1000;
};

void runReplay(ReplayOptions const& options)
{
    // ── genesis config (the same loader the node boots with) ──
    bcos::tool::NodeConfig nodeConfig(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    nodeConfig.loadGenesisConfig(options.genesisPath);
    auto const& genesis = nodeConfig.genesisConfig();
    BOOST_REQUIRE_MESSAGE(genesis.m_opStackELMode, "genesis must declare opstack-el mode");
    BOOST_REQUIRE_MESSAGE(
        genesis.m_ethGenesisHeader.has_value(), "genesis must carry [eth_genesis_header]");
    BOOST_REQUIRE_MESSAGE(
        genesis.m_opForkSchedule.has_value(), "genesis must carry [fork_timestamps] (op)");
    BOOST_REQUIRE_EQUAL(nodeConfig.ethereumChainId(), kOpSepoliaChainId);
    BOOST_TEST_MESSAGE("genesis loaded: allocs=" << genesis.m_allocs.size() << " chainId="
                                                 << nodeConfig.ethereumChainId());

    // ── fixture: MLS over an in-memory backend, Ledger over a LegacyStorageWrapper on
    //    the SAME backend (the OpBlockVerifierTest wiring) — no manual seeding at all;
    //    buildGenesisBlock creates every sys table, imports the 2066 allocs, persists
    //    the /mpt/ nodes and strong-validates state_root + the B0 hash. ──
    auto cryptoSuite = makeCryptoSuite();
    auto hashImpl = cryptoSuite->hashImpl();
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    auto blockFactory = makeBlockFactory();

    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    auto legacyLedgerStorage =
        std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(backendStorage);
    auto ledger = std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000);
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1);

    bcos::ledger::LedgerConfig genesisParam;
    genesisParam.setBlockNumber(0);
    genesisParam.setHash(bcos::crypto::HashType{});
    genesisParam.setBlockTxCountLimit(0);

    auto const genesisT0 = std::chrono::steady_clock::now();
    auto const genesisOk =
        bcos::task::syncWait(bcos::ledger::buildGenesisBlock(*ledger, genesis, genesisParam));
    auto const genesisMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - genesisT0)
                               .count();
    BOOST_REQUIRE_MESSAGE(genesisOk,
        "ledger::buildGenesisBlock rejected the real op-sepolia genesis "
        "(state_root/hash strong validation must pass byte-for-byte)");
    BOOST_TEST_MESSAGE("genesis buildGenesisBlock: OK in " << genesisMs << " ms");

    Verifier verifier(receiptFactory, hashImpl, nodeConfig.ethereumChainId(),
        *genesis.m_opForkSchedule, blockFactory, multiLayerStorage, ledger, ioServicePool);

    // ── replay loop with a one-window fetch pipeline ──
    RpcClient client(options.rpcUrl);
    auto const endBlock = options.startBlock + options.blockCount - 1;
    int64_t verified = 0;
    uint64_t totalTxs = 0;
    auto const replayT0 = std::chrono::steady_clock::now();
    double rpcSeconds = 0.0;

    auto fetchTimed = [&](int64_t num, int64_t count) {
        auto const t0 = std::chrono::steady_clock::now();
        auto blocks = fetchWindow(client, num, count);
        rpcSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return blocks;
    };

    int64_t windowStart = options.startBlock;
    auto pending = std::async(std::launch::async, fetchTimed, windowStart,
        std::min(kFetchWindow, endBlock - windowStart + 1));
    while (windowStart <= endBlock)
    {
        auto blocks = pending.get();
        auto const thisCount = static_cast<int64_t>(blocks.size());
        auto const nextStart = windowStart + thisCount;
        if (nextStart <= endBlock)
        {
            pending = std::async(std::launch::async, fetchTimed, nextStart,
                std::min(kFetchWindow, endBlock - nextStart + 1));
        }

        for (auto& fetched : blocks)
        {
            auto const number = fetched.header.number;

            bcos::devp2p::sync::Block block;
            block.header = fetched.header;
            bcos::codec::rlp::encode(block.headerRlp, block.header);
            block.hash = bcos::protocol::ethHeaderHash(block.header);
            block.transactions = std::move(fetched.rawTxs);
            totalTxs += block.transactions.size();

            // Byte-level self-checks against the RPC announcements BEFORE execution:
            // keccak256(rlp(header)) must reproduce the announced block hash, and every
            // envelope's keccak must reproduce its announced transaction hash.
            BOOST_REQUIRE_MESSAGE(block.hash == fetched.announcedHash,
                "block " << number << " header RLP hash mismatch: computed=" << block.hash.hex()
                         << " announced=" << fetched.announcedHash.hex());
            BOOST_REQUIRE_EQUAL(block.transactions.size(), fetched.txHashHex.size());
            for (size_t t = 0; t < block.transactions.size(); ++t)
            {
                auto const txHash = hashImpl->hash(bcos::bytesConstRef(
                    block.transactions[t].data(), block.transactions[t].size()));
                auto const announced = bcos::crypto::HashType(
                    std::string_view(stripHexPrefix(fetched.txHashHex[t])),
                    bcos::crypto::HashType::FromHex);
                BOOST_REQUIRE_MESSAGE(txHash == announced,
                    "block " << number << " tx[" << t << "] hash mismatch: computed="
                             << txHash.hex() << " announced=" << announced.hex());
            }

            try
            {
                bcos::task::syncWait(verifier.verifyAndCommit(block));
            }
            catch (bcos::executor_v1::opstack::OpBlockVerificationFailed const& e)
            {
                BOOST_REQUIRE_MESSAGE(false,
                    "block " << number << " commitment mismatch: field=" << e.field
                             << " computed=" << e.computedValue
                             << " announced=" << e.announcedValue);
            }
            catch (bcos::executor_v1::opstack::OpStaleOrOutOfOrderBlock const& e)
            {
                BOOST_REQUIRE_MESSAGE(false, "block " << number << " stale/out-of-order: "
                                                      << e.what());
            }
            catch (std::exception const& e)
            {
                BOOST_REQUIRE_MESSAGE(
                    false, "block " << number << " verifyAndCommit threw: " << e.what());
            }
            ++verified;

            if (verified % 100 == 0)
            {
                auto const elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - replayT0)
                                         .count();
                BOOST_TEST_MESSAGE(
                    "replayed " << verified << " blocks (height " << number << ", txs "
                                << totalTxs << ") in " << elapsed << " s — "
                                << (verified / elapsed) << " blocks/s, rpc share "
                                << (rpcSeconds / elapsed * 100.0) << "%");
            }
        }
        windowStart = nextStart;
    }

    auto const elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - replayT0).count();
    BOOST_TEST_MESSAGE("REPLAY DONE: blocks [" << options.startBlock << ".." << endBlock << "] ("
                                               << verified << " blocks, " << totalTxs << " txs) in "
                                               << elapsed << " s — " << (verified / elapsed)
                                               << " blocks/s, rpc share "
                                               << (rpcSeconds / elapsed * 100.0) << "%");
    BOOST_REQUIRE_EQUAL(verified, options.blockCount);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpSepoliaReplaySuite)

BOOST_AUTO_TEST_CASE(ReplayRealOpSepoliaFromGenesis)
{
    const char* rpc = std::getenv("OP_SEPOLIA_REPLAY_RPC");
    if (rpc == nullptr || *rpc == '\0')
    {
        BOOST_TEST_MESSAGE(
            "skipping OpSepoliaReplayTest: OP_SEPOLIA_REPLAY_RPC is not set "
            "(network-gated acceptance test)");
        return;
    }

    ReplayOptions options;
    options.rpcUrl = rpc;
    if (const char* genesis = std::getenv("OP_SEPOLIA_REPLAY_GENESIS"))
    {
        options.genesisPath = genesis;
    }
    else
    {
        options.genesisPath = "/tmp/op-sepolia-config.genesis";
    }
    if (const char* blocks = std::getenv("OP_SEPOLIA_REPLAY_BLOCKS"))
    {
        options.blockCount = std::stoll(blocks);
    }
    if (const char* start = std::getenv("OP_SEPOLIA_REPLAY_START"))
    {
        options.startBlock = std::stoll(start);
    }
    {
        std::ifstream probe(options.genesisPath);
        if (!probe.good())
        {
            BOOST_TEST_MESSAGE("skipping OpSepoliaReplayTest: genesis config not readable at "
                               << options.genesisPath);
            return;
        }
    }
    BOOST_TEST_MESSAGE("replaying op-sepolia blocks [" << options.startBlock << ".."
                                                       << (options.startBlock +
                                                              options.blockCount - 1)
                                                       << "] via " << options.rpcUrl);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 1 GiB stack reservation for the worker (see the file header: ASAN does not
    // tail-call the task symmetric transfer, and a long per-block co_await chain
    // would accumulate native frames on the default 8 MiB stack).
    std::exception_ptr failure;
    boost::thread::attributes attrs;
    attrs.set_stack_size(std::size_t{1} << 30);
    boost::thread worker(attrs, [&] {
        try
        {
            runReplay(options);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
    });
    worker.join();
    curl_global_cleanup();
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

BOOST_AUTO_TEST_SUITE_END()
