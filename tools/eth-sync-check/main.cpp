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
 * @file main.cpp
 * @brief Real Sepolia archive-node sync check: pulls blocks from an Ethereum
 *        JSON-RPC archive endpoint and validates them with the same core the
 *        devp2p sync path uses — header RLP re-encoding (keccak hash must equal
 *        the canonical hash), PoS header field rules (EIP-1559 base fee / EIP-4844
 *        blob gas), and the withdrawals trie root.
 *
 *        Usage: eth-sync-check --rpc <url> [--rpc2 <url>] --start <number> --count <n>
 *               [--merge-block <n>]   (override the merge/TTD block; default Sepolia 1735371)
 *        Usage: eth-sync-check --verify-tx <blockNumber> [--rpc <url>]
 *        Usage: eth-sync-check --genesis <file> [--expect <root>]
 *        Usage: eth-sync-check --genesis-ini <config.genesis> [--expect <root>]
 * @date 2026/8/18
 */
#include <bcos-devp2p/sync/Block.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-task/Wait.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <curl/curl.h>
#include <json/json.h>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p::sync;

namespace
{
std::string stripHexPrefix(std::string const& hex)
{
    return hex.rfind("0x", 0) == 0 ? hex.substr(2) : hex;
}

bytes hexToBytes(std::string const& hex)
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

/// libcurl write callback.
size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

std::optional<Json::Value> rpcCall(
    std::vector<std::string> const& urls, std::string const& method, std::string const& params)
{
    for (auto const& url : urls)
    {
        CURL* curl = curl_easy_init();
        if (!curl)
        {
            continue;
        }
        std::string body = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method +
                           "\",\"params\":" + params + ",\"id\":1}";
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "eth-sync-check/1.0");
        CURLcode rc = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK)
        {
            continue;
        }
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root) || root["error"].isObject())
        {
            continue;
        }
        return root["result"];
    }
    return std::nullopt;
}

/// Map an eth_getBlockByNumber JSON object onto our pure Ethereum header struct.
protocol::EthBlockHeaderData headerFromJson(Json::Value const& j)
{
    protocol::EthBlockHeaderData h;
    h.parentInfo.blockHash = crypto::HashType(
        std::string_view(stripHexPrefix(j["parentHash"].asString())), crypto::HashType::FromHex);
    h.uncleHash = crypto::HashType(
        std::string_view(stripHexPrefix(j["sha3Uncles"].asString())), crypto::HashType::FromHex);
    h.stateRoot = crypto::HashType(
        std::string_view(stripHexPrefix(j["stateRoot"].asString())), crypto::HashType::FromHex);
    h.txsRoot = crypto::HashType(std::string_view(stripHexPrefix(j["transactionsRoot"].asString())),
        crypto::HashType::FromHex);
    h.receiptsRoot = crypto::HashType(std::string_view(stripHexPrefix(j["receiptsRoot"].asString())),
        crypto::HashType::FromHex);
    auto bloomBytes = hexToBytes(j["logsBloom"].asString());
    std::copy(bloomBytes.begin(), bloomBytes.end(), h.logsBloom.begin());
    h.difficulty = u256(j["difficulty"].asString());
    h.gasLimit = u256(j["gasLimit"].asString());
    h.gasUsed = u256(j["gasUsed"].asString());
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
        h.baseFee = u256(j["baseFeePerGas"].asString());
    }
    if (j.isMember("withdrawalsRoot"))
    {
        h.withdrawalsHash = crypto::HashType(
            std::string_view(stripHexPrefix(j["withdrawalsRoot"].asString())),
            crypto::HashType::FromHex);
    }
    if (j.isMember("blobGasUsed"))
    {
        h.blobGasUsed = u256(j["blobGasUsed"].asString());
    }
    if (j.isMember("excessBlobGas"))
    {
        h.excessBlobGas = u256(j["excessBlobGas"].asString());
    }
    if (j.isMember("parentBeaconBlockRoot"))
    {
        h.parentBeaconRoot = crypto::HashType(
            std::string_view(stripHexPrefix(j["parentBeaconBlockRoot"].asString())),
            crypto::HashType::FromHex);
    }
    if (j.isMember("requestsHash"))
    {
        h.requestsHash = crypto::HashType(std::string_view(stripHexPrefix(j["requestsHash"].asString())),
            crypto::HashType::FromHex);
    }
    return h;
}

/// The Sepolia chain configuration (public chain spec). Current tool limitation:
/// the schedule stops at Prague and does not model Osaka/BPO1/BPO2 blob
/// parameters yet, so post-Prague blob-bearing headers can fail this standalone
/// check until the fork-tail follow-up lands.
ChainConfig sepoliaConfig()
{
    ChainConfig config;
    config.chainId = 11155111;
    config.londonTime = 0;          // active from genesis
    config.shanghaiTime = 1677557088;
    config.cancunTime = 1706655072;
    config.pragueTime = 1741159776;
    // The Merge (terminal total difficulty) block: blocks below it are PoW
    // (non-zero difficulty, ommers allowed); from it onward PoS rules apply.
    // Without this the PoS field checks misjudge every pre-merge block.
    config.mergeBlock = 1735371;
    return config;
}

int failures = 0;

void report(std::string const& what, bool ok, std::string const& detail = {})
{
    if (!ok)
    {
        ++failures;
    }
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what;
    if (!detail.empty())
    {
        std::cout << " (" << detail << ")";
    }
    std::cout << std::endl;
}
/// Parse a standard Ethereum genesis.json (alloc: {addr: {balance, code, storage, nonce}})
/// into a GenesisConfig and compute the op-geth-compatible genesis state root.
void runGenesisCheck(std::string const& path, std::optional<std::string> const& expect)
{
    Json::Value root;
    Json::Reader reader;
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    if (!reader.parse(ss.str(), root) || !root.isMember("alloc"))
    {
        std::cerr << "cannot parse genesis.json (missing alloc)" << std::endl;
        ++failures;
        return;
    }

    ledger::GenesisConfig genesis;
    for (auto const& addr : root["alloc"].getMemberNames())
    {
        ledger::Alloc a;
        a.address = stripHexPrefix(addr);
        auto const& av = root["alloc"][addr];
        if (av.isMember("balance"))
        {
            a.balance = u256(av["balance"].asString());
        }
        if (av.isMember("nonce"))
        {
            a.nonce = std::to_string(hexToU64(av["nonce"].asString()));
        }
        if (av.isMember("code"))
        {
            a.code = stripHexPrefix(av["code"].asString());
        }
        if (av.isMember("storage"))
        {
            for (auto const& slot : av["storage"].getMemberNames())
            {
                a.storage.emplace_back(stripHexPrefix(slot),
                    stripHexPrefix(av["storage"][slot].asString()));
            }
        }
        genesis.m_allocs.push_back(std::move(a));
    }
    std::cout << "alloc count: " << genesis.m_allocs.size() << std::endl;

    auto trie = task::syncWait(ledger::computeGenesisStateTrie(genesis));
    std::cout << "genesis stateRoot: " << trie.root.hex()
              << " trie-nodes: " << trie.nodes.size() << std::endl;
    if (expect)
    {
        auto expected = crypto::HashType(
            std::string_view(stripHexPrefix(*expect)), crypto::HashType::FromHex);
        report("genesis stateRoot match", trie.root == expected, trie.root.hex());
    }
}

/// Verify the real raw->Transaction decoder (decodeWeb3RawTransaction, the shared
/// path used by eth_sendRawTransaction / devp2p sync / the external block verifier)
/// against a REAL on-chain block: pull the raw EIP-2718 bytes of every transaction
/// via eth_getRawTransactionByHash and check the decoded sender, canonical hash,
/// and key fields against the block's transaction objects.
void runTxVerification(std::vector<std::string> const& rpcs, int64_t blockNumber)
{
    std::ostringstream params;
    params << "[\"0x" << std::hex << blockNumber << "\",true]";
    auto block = rpcCall(rpcs, "eth_getBlockByNumber", params.str());
    if (!block)
    {
        report("block " + std::to_string(blockNumber) + " fetch", false, "RPC unavailable");
        return;
    }
    auto const& txs = (*block)["transactions"];
    if (!txs.isArray() || txs.size() == 0)
    {
        std::cout << "block " << blockNumber << " has no transactions (nothing to decode)"
                  << std::endl;
        return;
    }
    std::cout << "block " << blockNumber << " txs=" << txs.size() << std::endl;

    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);

    int idx = 0;
    for (auto const& txJson : txs)
    {
        auto const txHashHex = txJson["hash"].asString();
        auto raw = rpcCall(rpcs, "eth_getRawTransactionByHash",
            "[\"" + txHashHex + "\"]");
        if (!raw)
        {
            report("tx[" + std::to_string(idx) + "] raw fetch", false, "RPC unavailable");
            ++idx;
            continue;
        }
        auto rawHex = raw->asString();
        auto rawBytes = hexToBytes(rawHex);

        // The shared decoder used by the sync/verifier path.
        std::shared_ptr<bcostars::protocol::TransactionImpl> decoded;
        try
        {
            decoded = bcos::rpc::decodeWeb3RawTransaction(
                bcos::bytesConstRef(rawBytes.data(), rawBytes.size()), *cryptoSuite->hashImpl());
        }
        catch (std::exception const& e)
        {
            report("tx[" + std::to_string(idx) + "] decode", false, e.what());
            ++idx;
            continue;
        }

        // 1. Canonical tx hash must equal the on-chain hash.
        auto canonical = crypto::HashType(
            std::string_view(stripHexPrefix(txHashHex)), crypto::HashType::FromHex);
        report("tx[" + std::to_string(idx) + "] hash", decoded->hash() == canonical,
            decoded->hash().hex().substr(0, 18));

        // 2. Recovered sender must equal the on-chain from.
        auto senderHex =
            bcos::toHexStringWithPrefix(bcos::bytes(decoded->sender().begin(), decoded->sender().end()));
        report("tx[" + std::to_string(idx) + "] sender", senderHex == txJson["from"].asString(),
            senderHex.substr(0, 18));

        // 3. Key fields: type, to, value, nonce, gasLimit, gas prices.
        auto toStr = txJson["to"].isString() ? txJson["to"].asString() : "";
        report("tx[" + std::to_string(idx) + "] to", decoded->to() == toStr,
            std::string(decoded->to()));
        report("tx[" + std::to_string(idx) + "] value",
            decoded->value() == u256(txJson["value"].asString()),
            decoded->value().str(0, std::ios_base::hex));
        report("tx[" + std::to_string(idx) + "] nonce",
            decoded->nonce() == txJson["nonce"].asString(),
            std::string(decoded->nonce()));
        report("tx[" + std::to_string(idx) + "] gasLimit",
            static_cast<uint64_t>(decoded->gasLimit()) == hexToU64(txJson["gas"].asString()),
            std::to_string(decoded->gasLimit()));
        report("tx[" + std::to_string(idx) + "] typedKind",
            static_cast<uint64_t>(decoded->web3TypedTxKind()) ==
                static_cast<uint64_t>(hexToU64(txJson["type"].asString())),
            std::to_string(static_cast<uint8_t>(decoded->web3TypedTxKind())));
        if (txJson.isMember("maxFeePerGas"))
        {
            report("tx[" + std::to_string(idx) + "] maxFeePerGas",
                decoded->maxFeePerGas().has_value() &&
                    *decoded->maxFeePerGas() == u256(txJson["maxFeePerGas"].asString()),
                std::string(decoded->maxFeePerGas().has_value() ?
                                decoded->maxFeePerGas()->str(0, std::ios_base::hex) :
                                ""));
        }
        // EIP-4844 blob fields (type 3): maxFeePerBlobGas + blob versioned hashes.
        if (txJson.isMember("maxFeePerBlobGas"))
        {
            auto blobGasMatches = decoded->maxFeePerBlobGas().has_value() &&
                                  *decoded->maxFeePerBlobGas() ==
                                      u256(txJson["maxFeePerBlobGas"].asString());
            report("tx[" + std::to_string(idx) + "] maxFeePerBlobGas", blobGasMatches,
                std::string(decoded->maxFeePerBlobGas().has_value() ?
                                decoded->maxFeePerBlobGas()->str(0, std::ios_base::hex) :
                                ""));
        }
        if (txJson.isMember("blobVersionedHashes"))
        {
            auto const& onChainBlobs = txJson["blobVersionedHashes"];
            auto const& decodedBlobs = decoded->blobVersionedHashes();
            bool blobsMatch = decodedBlobs.size() == onChainBlobs.size();
            if (blobsMatch)
            {
                for (Json::ArrayIndex i = 0; i < onChainBlobs.size(); ++i)
                {
                    if (bcos::toHexStringWithPrefix(decodedBlobs[i]) !=
                        onChainBlobs[i].asString())
                    {
                        blobsMatch = false;
                        break;
                    }
                }
            }
            report("tx[" + std::to_string(idx) + "] blobVersionedHashes", blobsMatch,
                "count=" + std::to_string(decodedBlobs.size()));
        }
        ++idx;
    }
}

/// Decode a single raw EIP-2718 transaction (hex) offline with the shared decoder
/// and print its fields. Used to sanity-check transactions fetched out-of-band
/// (e.g. blob txs from a block that is otherwise too heavy to re-fetch per tx).
void runRawTxDecode(std::string const& rawHex)
{
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    auto rawBytes = hexToBytes(rawHex);
    std::shared_ptr<bcostars::protocol::TransactionImpl> decoded;
    try
    {
        decoded = bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(rawBytes.data(), rawBytes.size()), *cryptoSuite->hashImpl());
    }
    catch (std::exception const& e)
    {
        report("raw decode", false, e.what());
        return;
    }
    std::cout << "  hash: " << decoded->hash().hex() << std::endl;
    std::cout << "  sender: "
              << bcos::toHexStringWithPrefix(
                     bcos::bytes(decoded->sender().begin(), decoded->sender().end()))
              << std::endl;
    std::cout << "  to: " << std::string(decoded->to()) << std::endl;
    std::cout << "  type: " << static_cast<int>(decoded->web3TypedTxKind()) << std::endl;
    std::cout << "  nonce: " << std::string(decoded->nonce()) << std::endl;
    std::cout << "  gasLimit: " << decoded->gasLimit() << std::endl;
    std::cout << "  value: " << decoded->value().str(0, std::ios_base::hex) << std::endl;
    if (decoded->maxFeePerGas().has_value())
    {
        std::cout << "  maxFeePerGas: " << decoded->maxFeePerGas()->str(0, std::ios_base::hex)
                  << std::endl;
    }
    if (decoded->maxFeePerBlobGas().has_value())
    {
        std::cout << "  maxFeePerBlobGas: "
                  << decoded->maxFeePerBlobGas()->str(0, std::ios_base::hex) << std::endl;
    }
    auto const& blobs = decoded->blobVersionedHashes();
    if (!blobs.empty())
    {
        std::cout << "  blobVersionedHashes:" << std::endl;
        for (auto const& b : blobs)
        {
            std::cout << "    " << bcos::toHexStringWithPrefix(b) << std::endl;
        }
    }
    report("raw tx decode", true);
}

/// Parse a FISCO config.genesis (INI) with the same NodeConfig loader the node uses, then
/// compute the Ethereum genesis state root from the [alloc.*] sections. This validates an
/// EL-mode config.genesis exactly as the node would derive it.
void runGenesisIniCheck(std::string const& path, std::optional<std::string> const& expect)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    bcos::tool::NodeConfig cfg(keyFactory);
    cfg.loadGenesisConfig(path);
    auto trie = task::syncWait(ledger::computeGenesisStateTrie(cfg.genesisConfig()));
    std::cout << "genesis stateRoot: " << trie.root.hex()
              << " trie-nodes: " << trie.nodes.size() << " allocs: "
              << cfg.genesisConfig().m_allocs.size() << std::endl;
    if (expect)
    {
        auto expected = crypto::HashType(
            std::string_view(stripHexPrefix(*expect)), crypto::HashType::FromHex);
        report("genesis stateRoot match", trie.root == expected, trie.root.hex());
    }
    // Genesis header hash: re-encode the [eth_genesis_header] fields the same
    // way Ledger::applyEthGenesisHeader does (fork-gated fields only when
    // present) and compare keccak256(rlp(header)) against the artifact's hash
    // claim. This is the byte-exact check — a mismatch means the config cannot
    // reproduce the canonical genesis hash.
    if (auto const& eth = cfg.genesisConfig().m_ethGenesisHeader; eth.has_value())
    {
        protocol::EthBlockHeaderData h;
        h.parentInfo.blockHash = eth->m_parentHash;
        h.uncleHash = eth->m_sha3Uncles;
        h.coinbase = eth->m_miner;
        h.stateRoot = eth->m_stateRoot;
        h.txsRoot = eth->m_transactionsRoot;
        h.receiptsRoot = eth->m_receiptsRoot;
        std::copy(eth->m_logsBloom.begin(), eth->m_logsBloom.end(), h.logsBloom.begin());
        h.difficulty = eth->m_difficulty;
        h.gasLimit = eth->m_gasLimit;
        h.gasUsed = eth->m_gasUsed;
        h.number = eth->m_number;
        h.timestamp = eth->m_timestamp;
        h.extraData = eth->m_extraData;
        std::copy(eth->m_mixHash.begin(), eth->m_mixHash.end(), h.prevRandao.begin());
        std::copy(eth->m_nonce.begin(), eth->m_nonce.end(), h.nonce.begin());
        h.baseFee = eth->m_baseFeePerGas;
        h.withdrawalsHash = eth->m_withdrawalsRoot;
        h.blobGasUsed = eth->m_blobGasUsed;
        h.excessBlobGas = eth->m_excessBlobGas;
        h.parentBeaconRoot = eth->m_parentBeaconBlockRoot;
        h.requestsHash = eth->m_requestsHash;
        auto computed = headerHash(h);
        report("genesis header hash match", computed == eth->m_hash,
            computed.hex() + " vs " + eth->m_hash.hex());
    }
}

}  // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> rpcs;
    int64_t start = 1;
    int64_t count = 10;
    std::optional<std::string> genesisPath;
    std::optional<std::string> genesisIniPath;
    std::optional<std::string> expectRoot;
    std::optional<int64_t> verifyTxBlock;
    std::optional<std::string> rawTxHex;
    std::optional<uint64_t> mergeBlockOverride;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--rpc" && i + 1 < argc)
        {
            rpcs.push_back(argv[++i]);
        }
        else if (arg == "--rpc2" && i + 1 < argc)
        {
            rpcs.push_back(argv[++i]);
        }
        else if (arg == "--start" && i + 1 < argc)
        {
            start = std::stoll(argv[++i]);
        }
        else if (arg == "--count" && i + 1 < argc)
        {
            count = std::stoll(argv[++i]);
        }
        else if (arg == "--genesis" && i + 1 < argc)
        {
            genesisPath = argv[++i];
        }
        else if (arg == "--genesis-ini" && i + 1 < argc)
        {
            genesisIniPath = argv[++i];
        }
        else if (arg == "--expect" && i + 1 < argc)
        {
            expectRoot = argv[++i];
        }
        else if (arg == "--verify-tx" && i + 1 < argc)
        {
            verifyTxBlock = std::stoll(argv[++i]);
        }
        else if (arg == "--raw-tx" && i + 1 < argc)
        {
            rawTxHex = argv[++i];
        }
        else if (arg == "--merge-block" && i + 1 < argc)
        {
            mergeBlockOverride = static_cast<uint64_t>(std::stoull(argv[++i]));
        }
    }
    if (genesisPath)
    {
        runGenesisCheck(*genesisPath, expectRoot);
        return failures == 0 ? 0 : 1;
    }
    if (genesisIniPath)
    {
        runGenesisIniCheck(*genesisIniPath, expectRoot);
        std::cout << std::endl
                  << (failures == 0 ? "ALL CHECKS PASSED" :
                                      std::to_string(failures) + " CHECK(S) FAILED")
                  << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (rawTxHex)
    {
        runRawTxDecode(*rawTxHex);
        std::cout << std::endl
                  << (failures == 0 ? "ALL CHECKS PASSED" :
                                      std::to_string(failures) + " CHECK(S) FAILED")
                  << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (rpcs.empty())
    {
        rpcs.push_back("https://1rpc.io/sepolia");
        rpcs.push_back("https://ethereum-sepolia-rpc.publicnode.com");
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (verifyTxBlock)
    {
        runTxVerification(rpcs, *verifyTxBlock);
        curl_global_cleanup();
        std::cout << std::endl
                  << (failures == 0 ? "ALL CHECKS PASSED" :
                                      std::to_string(failures) + " CHECK(S) FAILED")
                  << std::endl;
        return failures == 0 ? 0 : 1;
    }

    auto config = sepoliaConfig();
    if (mergeBlockOverride)
    {
        config.mergeBlock = *mergeBlockOverride;
    }
    std::optional<protocol::EthBlockHeaderData> prev;
    for (int64_t i = start; i < start + count; ++i)
    {
        std::ostringstream params;
        params << "[\"0x" << std::hex << i << "\",true]";
        auto block = rpcCall(rpcs, "eth_getBlockByNumber", params.str());
        if (!block)
        {
            report("block " + std::to_string(i) + " fetch", false, "RPC unavailable");
            continue;
        }
        auto h = headerFromJson(*block);
        std::cout << "block " << i << " ts=" << h.timestamp
                  << " hash=" << (*block)["hash"].asString().substr(0, 18) << "..." << std::endl;

        // 1. Header RLP re-encoding: keccak(rlp(header)) must equal the canonical hash.
        auto canonical = crypto::HashType(std::string_view(stripHexPrefix((*block)["hash"].asString())),
            crypto::HashType::FromHex);
        report("header hash", headerHash(h) == canonical);

        // 2. PoS header field rules against the parent (base fee, gas limit, timestamp...),
        //    plus the EIP-1559 base-fee recomputation from the PARENT.
        if (prev)
        {
            auto pos = validateHeaderPoS(h, *prev, config);
            report("PoS header fields", pos.valid, pos.error);
            if (h.baseFee && prev->baseFee)
            {
                auto expected = computeNextBaseFee(*prev);
                report("baseFee recompute", *h.baseFee == expected);
            }
        }
        prev = h;

        // 3. Withdrawals trie root (Shanghai+): rebuild the RLP and compare.
        if (h.withdrawalsHash && (*block).isMember("withdrawals"))
        {
            std::vector<bytes> wdRlps;
            for (auto const& wdJson : (*block)["withdrawals"])
            {
                protocol::EthWithdrawalData wd;
                wd.index = hexToU64(wdJson["index"].asString());
                wd.validatorIndex = hexToU64(wdJson["validatorIndex"].asString());
                auto addr = hexToBytes(wdJson["address"].asString());
                std::copy(addr.begin(), addr.end(), wd.address.begin());
                wd.amount = hexToU64(wdJson["amount"].asString());
                bytes rlp;
                bcos::codec::rlp::encode(rlp, wd);
                wdRlps.push_back(std::move(rlp));
            }
            std::vector<bytesConstRef> refs;
            refs.reserve(wdRlps.size());
            for (auto const& rlp : wdRlps)
            {
                refs.emplace_back(bcos::ref(rlp));
            }
            auto computed = ledger::mpt::calculateWithdrawalsRoot(refs);
            report("withdrawalsRoot", computed == *h.withdrawalsHash);
        }
    }
    curl_global_cleanup();
    std::cout << std::endl
              << (failures == 0 ? "ALL CHECKS PASSED" :
                                  std::to_string(failures) + " CHECK(S) FAILED")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
