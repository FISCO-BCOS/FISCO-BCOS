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
 *        Usage: eth-sync-check --op [--rpc <url>] --start <number> --count <n>
 *               [--op-chain-id <n>] [--op-block-time <s>] [--op-fork <name> <timestamp>]
 *                                   (OP Stack header validation; defaults: op-sepolia,
 *                                    RPC https://sepolia.optimism.io)
 *        Usage: eth-sync-check --verify-tx <blockNumber> [--rpc <url>]
 *        Usage: eth-sync-check --genesis <file> [--expect <root>]
 *        Usage: eth-sync-check --genesis-ini <config.genesis> [--expect <root>]
 *        Usage: eth-sync-check --genesis2ini <genesis.json> [--output <file>]
 *                                   (geth genesis.json -> EL-mode config.genesis, for hive)
 *        Usage: eth-sync-check --enode-from-key <keyfile> [--ip <ip>] [--port <port>]
 *                                   (secp256k1 node key -> enode:// URL, for hive)
 * @date 2026/8/18
 */
#include <bcos-devp2p/sync/Block.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <bcos-devp2p/sync/OpHeaderValidator.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-task/Wait.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/EthGenesisHeader.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <curl/curl.h>
#include <json/json.h>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <cctype>
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

/// The Sepolia chain configuration (public chain spec; the fork tail matches the
/// repo's tools/BcosBuilder/src/tpl/config.genesis.el template, which carries the
/// scheduled Osaka/BPO1/BPO2 timestamps). With the tail filled, post-Osaka headers
/// validate with the EIP-7840 blob-schedule / EIP-7918 excess-blob-gas rules.
ChainConfig sepoliaConfig()
{
    ChainConfig config;
    config.chainId = 11155111;
    config.londonTime = 0;          // active from genesis
    config.shanghaiTime = 1677557088;
    config.cancunTime = 1706655072;
    config.pragueTime = 1741159776;
    config.osakaTime = 1760427360;
    config.bpo1Time = 1761017184;
    config.bpo2Time = 1761607008;
    // The Merge (terminal total difficulty) block: blocks below it are PoW
    // (non-zero difficulty, ommers allowed); from it onward PoS rules apply.
    // Without this the PoS field checks misjudge every pre-merge block.
    config.mergeBlock = 1735371;
    return config;
}

/// The OP Sepolia chain configuration (superchain-registry
/// superchain/configs/sepolia/op.toml): chain id 11155420, 2-second blocks, the
/// historical OP fork timestamps, and the standard EIP-1559 constants
/// (Bedrock denominator 50 / Canyon denominator 250 / elasticity 6). Regolith was
/// active from genesis on op-sepolia; it has no header-level rules. Jovian and Karst
/// are scheduled (and already active): Jovian switches extraData to 17 bytes, meters
/// the DA footprint in blobGasUsed and applies the minBaseFee floor.
OpChainConfig opSepoliaConfig()
{
    OpChainConfig config;
    config.chainId = 11155420;
    config.blockTimeSeconds = 2;
    config.forkSchedule.m_regolithTime = 0;  // active from genesis
    config.forkSchedule.m_canyonTime = 1699981200;
    config.forkSchedule.m_deltaTime = 1703203200;
    config.forkSchedule.m_ecotoneTime = 1708534800;
    config.forkSchedule.m_fjordTime = 1716998400;
    config.forkSchedule.m_graniteTime = 1723478400;
    config.forkSchedule.m_holoceneTime = 1732633200;
    config.forkSchedule.m_isthmusTime = 1744905600;
    config.forkSchedule.m_jovianTime = 1763568001;
    config.forkSchedule.m_karstTime = 1781712001;
    return config;
}

/// --op-fork <name> <timestamp>: override one OP fork activation time.
bool setOpForkTime(OpChainConfig& config, std::string const& name, uint64_t ts)
{
    if (name == "regolith") { config.forkSchedule.m_regolithTime = ts; }
    else if (name == "canyon") { config.forkSchedule.m_canyonTime = ts; }
    else if (name == "delta") { config.forkSchedule.m_deltaTime = ts; }
    else if (name == "ecotone") { config.forkSchedule.m_ecotoneTime = ts; }
    else if (name == "fjord") { config.forkSchedule.m_fjordTime = ts; }
    else if (name == "granite") { config.forkSchedule.m_graniteTime = ts; }
    else if (name == "holocene") { config.forkSchedule.m_holoceneTime = ts; }
    else if (name == "isthmus") { config.forkSchedule.m_isthmusTime = ts; }
    else if (name == "jovian") { config.forkSchedule.m_jovianTime = ts; }
    else if (name == "karst") { config.forkSchedule.m_karstTime = ts; }
    else { return false; }
    return true;
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
/// executorVersion selects the executor lane the genesis is judged for: the system-address
/// alloc guard only applies to the legacy lane (< ETHEREUM_EXECUTOR_VERSION); the v2/v3
/// executors keep every alloc under /apps/, so a 0x1000-range alloc is ordinary there.
void runGenesisCheck(std::string const& path, std::optional<std::string> const& expect,
    int executorVersion)
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
    genesis.m_executorVersion = executorVersion;
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
    // Genesis header hash: re-encode the [eth_genesis_header] fields via the shared
    // toEthBlockHeaderData mapping (fork-gated fields only when present) and compare
    // keccak256(rlp(header)) against the artifact's hash claim. This is the byte-exact
    // check — a mismatch means the config cannot reproduce the canonical genesis hash.
    if (auto const& eth = cfg.genesisConfig().m_ethGenesisHeader; eth.has_value())
    {
        auto h = protocol::toEthBlockHeaderData(*eth);
        auto computed = bcos::protocol::ethHeaderHash(h);
        report("genesis header hash match", computed == eth->m_hash,
            computed.hex() + " vs " + eth->m_hash.hex());
    }
}

/// A geth chain-config quantity: fork blocks/times and chainId are JSON numbers,
/// header quantities are 0x-hex strings, alloc.balance may be either hex or a
/// decimal string. Absent or explicit null yields nullopt. u256 parses both the
/// 0x-prefixed hex and the plain decimal shapes, so one path covers all of geth.
std::optional<u256> jsonQuantity(Json::Value const& parent, char const* key)
{
    if (!parent.isMember(key) || parent[key].isNull())
    {
        return std::nullopt;
    }
    auto const& value = parent[key];
    if (value.isIntegral())
    {
        return u256(value.asUInt64());
    }
    return u256(value.asString());
}

/// 0x-prefixed minimal hex ("0x0" for zero) — the quantity shape the node's
/// [eth_genesis_header] parser (quantityField/optionalQuantityField) accepts.
std::string quantityHex(u256 const& value)
{
    return "0x" + value.str(0, std::ios_base::hex);
}

/// Left-pad a hex string (with or without 0x prefix) to exactly len chars, lowercased.
std::string padHex(std::string const& hex, size_t len, std::string const& what)
{
    auto body = stripHexPrefix(hex);
    if (body.size() > len)
    {
        throw std::runtime_error(
            what + " exceeds " + std::to_string(len) + " hex chars: " + hex);
    }
    body.insert(0, len - body.size(), '0');
    std::transform(body.begin(), body.end(), body.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return body;
}

/// --genesis2ini: convert a standard geth genesis.json into the EL-mode
/// config.genesis (INI) the node parses via NodeConfig::loadGenesisConfig. The
/// output contract is the node's parser, not geth: [fork_timestamps] requires the
/// full london..prague ladder (a fork the JSON leaves unscheduled inherits the
/// previous fork's time, with a warning comment), storage slots/values are
/// 0x-prefixed 64-hex, balances/nonces are decimal, and the fork-gated
/// [eth_genesis_header] keys are emitted only when the JSON carries the field
/// (absent key = omitted RLP field). state_root and hash are computed here with
/// the same computeGenesisStateTrie / ethHeaderHash the check modes use.
int runGenesis2Ini(std::string const& path, std::optional<std::string> const& output)
{
    try
    {
        Json::Value root;
        Json::Reader reader;
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        if (!reader.parse(ss.str(), root))
        {
            throw std::runtime_error("cannot parse genesis.json: " +
                                     reader.getFormattedErrorMessages());
        }
        // An empty (or absent) alloc is a valid EL-mode genesis: the alloc loop
        // below emits no [alloc.*] sections and computeGenesisStateTrie returns
        // the canonical empty-trie root, matching geth's empty-alloc stateRoot.
        // (NodeConfig::validateL2Invariants exempts [ethereum] mode=el from the
        // non-empty-alloc invariant; the L2/OP lanes still require allocs.)
        if (!root.isMember("config") || !root["config"].isObject())
        {
            throw std::runtime_error("genesis.json is missing the config object");
        }
        auto const& config = root["config"];
        auto chainId = jsonQuantity(config, "chainId");
        if (!chainId)
        {
            throw std::runtime_error("genesis.json config is missing chainId");
        }

        // The Merge: TTD 0/absent means PoS from genesis (paris_time=0, merge_block=0).
        // A positive TTD implies a PoW phase, which EL mode cannot replay.
        auto ttd = jsonQuantity(config, "terminalTotalDifficulty");
        if (ttd && *ttd > 0)
        {
            throw std::runtime_error(
                "terminalTotalDifficulty > 0: chains with a PoW phase are not supported "
                "(EL mode is post-merge only)");
        }

        std::vector<std::string> warnings;
        // Block-height-based forks have no EL-mode representation: the timestamp
        // ladder activates everything from genesis. Flag any configured at a
        // non-zero height so the operator knows the semantics shifted.
        static constexpr char const* kBlockForks[] = {
            "homesteadBlock", "daoForkBlock", "eip150Block", "eip155Block",
            "byzantiumBlock", "constantinopleBlock", "petersburgBlock", "istanbulBlock",
            "muirGlacierBlock", "berlinBlock", "arrowGlacierBlock", "grayGlacierBlock",
            "londonBlock",
        };
        for (auto const* key : kBlockForks)
        {
            auto value = jsonQuantity(config, key);
            if (value && *value > 0)
            {
                warnings.push_back(std::string(key) + "=" + value->str() +
                                   " ignored: EL mode only supports forks active from "
                                   "genesis (London and later start at london_time)");
            }
        }

        // [fork_timestamps] requires the london..prague ladder (readForkTimestamp
        // throws on an absent key), and UINT64_MAX is the ladder's terminal
        // "never activates" value (NodeConfig's ladder check treats it as such),
        // so an unscheduled fork is emitted as UINT64_MAX — NOT inherited from
        // the previous fork: inheriting 0 would ACTIVATE the fork from genesis
        // (e.g. Prague on a Cancun chain makes the verifier demand requestsHash
        // that Cancun-era blocks do not carry).
        uint64_t const kNever = std::numeric_limits<uint64_t>::max();
        // The emitted ladder hardcodes london_time=0 (EL mode runs London+ from
        // genesis regardless of the JSON), but geth activates London BLOCK-based:
        // Genesis.ToBlock consults g.Config.IsLondon(0), i.e. config.londonBlock
        // must be present and 0. A genesis whose config lacks londonBlock (the
        // smoke/genesis fixtures carry an empty config) mints a header WITHOUT
        // baseFee in geth, so the baseFee default below must key off the JSON,
        // not off the emitted ladder.
        auto const londonBlock = jsonQuantity(config, "londonBlock");
        bool const londonAtGenesis = londonBlock && *londonBlock == 0;
        uint64_t const parisTime = 0;  // TTD == 0: PoS from genesis
        bool forkNeverSeen = false;
        auto forkTime = [&](char const* jsonKey) -> uint64_t {
            auto value = jsonQuantity(config, jsonKey);
            if (!value)
            {
                forkNeverSeen = true;
                return kNever;
            }
            if (*value > u256(kNever))
            {
                throw std::runtime_error(std::string("config.") + jsonKey +
                                         " exceeds uint64: " + value->str());
            }
            // A scheduled fork after an unscheduled one breaks the parser's
            // non-decreasing ladder check — reject here with a clearer message.
            if (forkNeverSeen)
            {
                throw std::runtime_error(
                    std::string("config.") + jsonKey +
                    " is scheduled while an earlier fork is absent (fork times must "
                    "form a prefix: once a fork is unscheduled, all later forks must "
                    "be unscheduled too)");
            }
            return static_cast<uint64_t>(*value);
        };
        auto shanghaiTime = forkTime("shanghaiTime");
        auto cancunTime = forkTime("cancunTime");
        auto pragueTime = forkTime("pragueTime");
        // The post-Prague tail is optional in the parser (absent = not scheduled),
        // so these keys are emitted only when the JSON carries them.
        auto osakaTime = jsonQuantity(config, "osakaTime");
        auto bpo1Time = jsonQuantity(config, "bpo1Time");
        auto bpo2Time = jsonQuantity(config, "bpo2Time");

        // --- allocs: normalized once, used for both the trie and the INI ---
        ledger::GenesisConfig genesis;
        genesis.m_executorVersion = 2;
        for (auto const& addr : root["alloc"].getMemberNames())
        {
            auto const& av = root["alloc"][addr];
            ledger::Alloc a;
            a.address = padHex(addr, 40, "alloc address");
            a.balance = jsonQuantity(av, "balance").value_or(u256(0));
            auto nonce = jsonQuantity(av, "nonce").value_or(u256(0));
            if (nonce > u256(std::numeric_limits<uint64_t>::max()))
            {
                throw std::runtime_error("alloc " + addr + " nonce exceeds uint64");
            }
            a.nonce = nonce.str();
            if (av.isMember("code"))
            {
                auto code = stripHexPrefix(av["code"].asString());
                if (code.size() % 2 != 0)
                {
                    code.insert(0, 1, '0');
                }
                a.code = std::move(code);
            }
            if (av.isMember("storage"))
            {
                for (auto const& slot : av["storage"].getMemberNames())
                {
                    // The genesis trie hashes the slot bytes as configured, so the
                    // 32-byte left-padding here IS the Ethereum semantics.
                    a.storage.emplace_back(padHex(slot, 64, "storage slot"),
                        padHex(av["storage"][slot].asString(), 64, "storage value"));
                }
            }
            genesis.m_allocs.push_back(std::move(a));
        }

        auto trie = task::syncWait(ledger::computeGenesisStateTrie(genesis));

        // --- header: defaults match geth's zero values for absent fields ---
        auto headerQuantity = [&](char const* key) {
            return jsonQuantity(root, key).value_or(u256(0));
        };
        if (!root.isMember("gasLimit"))
        {
            throw std::runtime_error("genesis.json is missing gasLimit");
        }
        protocol::EthBlockHeaderData h;
        h.parentInfo.blockHash = crypto::HashType();  // genesis has no parent
        h.uncleHash = protocol::c_emptyOmmersHash;
        h.stateRoot = trie.root;
        h.txsRoot = crypto::HashType(
            std::string_view("56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"),
            crypto::HashType::FromHex);
        h.receiptsRoot = h.txsRoot;
        if (root.isMember("logsBloom") && !root["logsBloom"].isNull())
        {
            auto bloom = hexToBytes(root["logsBloom"].asString());
            if (bloom.size() != h.logsBloom.size())
            {
                throw std::runtime_error("logsBloom must be 256 bytes");
            }
            std::copy(bloom.begin(), bloom.end(), h.logsBloom.begin());
        }
        h.difficulty = headerQuantity("difficulty");
        h.gasLimit = headerQuantity("gasLimit");
        h.gasUsed = headerQuantity("gasUsed");
        h.prevRandao = crypto::HashType(
            "0x" + padHex(
                root.isMember("mixHash") ? root["mixHash"].asString() : "0x0", 64, "mixHash"));
        if (root.isMember("extraData"))
        {
            h.extraData = hexToBytes(root["extraData"].asString());
        }
        h.coinbase = Address("0x" + padHex(
            root.isMember("coinbase") ? root["coinbase"].asString() : "0x0", 40, "coinbase"));
        h.nonce = h64("0x" + padHex(
            root.isMember("nonce") ? root["nonce"].asString() : "0x0", 16, "nonce"));
        h.number = 0;
        auto timestamp = headerQuantity("timestamp");
        if (timestamp > u256(std::numeric_limits<int64_t>::max() / 1000))
        {
            throw std::runtime_error("timestamp exceeds int64 milliseconds");
        }
        h.timestamp = static_cast<int64_t>(timestamp);
        // Fork-gated fields: present in the JSON -> set on the header AND emitted
        // as an INI key. Absent fields follow geth's ToBlock semantics: when a
        // fork is active at the genesis timestamp, its header fields take their
        // empty defaults (the node's header-version detection keys off field
        // presence, so an active fork's fields must be emitted consistently).
        auto forkActiveAtGenesis = [&](uint64_t forkTime) {
            return forkTime != kNever && timestamp >= u256(forkTime);
        };
        std::optional<u256> baseFee, blobGasUsed, excessBlobGas;
        std::optional<crypto::HashType> withdrawalsHash, beaconRoot, requestsHash;
        if (auto v = jsonQuantity(root, "baseFeePerGas"))
        {
            baseFee = h.baseFee = *v;
        }
        else if (londonAtGenesis)
        {
            // geth's Genesis.ToBlock: London active at genesis and no explicit
            // baseFeePerGas -> params.InitialBaseFee (1 gwei). Omitting it would
            // mint a pre-London-shaped header whose hash disagrees with geth's,
            // and block 1 would fail "baseFeePerGas present but the parent is
            // pre-London" (the same constant the validator uses).
            baseFee = h.baseFee = protocol::kInitialBaseFee;
        }
        // consume-generated genesis files name it "withdrawalsRoot" (the fixture
        // header field); accept the geth-config-style "withdrawalsHash" too.
        auto const* withdrawalsKey = root.isMember("withdrawalsRoot") ? "withdrawalsRoot" :
                                                                          "withdrawalsHash";
        if (root.isMember(withdrawalsKey) && !root[withdrawalsKey].isNull())
        {
            withdrawalsHash = h.withdrawalsHash = crypto::HashType(
                "0x" + padHex(root[withdrawalsKey].asString(), 64, withdrawalsKey));
        }
        else if (forkActiveAtGenesis(shanghaiTime))
        {
            // keccak256(rlp([])) == empty MPT root: the empty withdrawals trie.
            withdrawalsHash = h.withdrawalsHash = crypto::HashType(
                std::string_view(
                    "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"),
                crypto::HashType::FromHex);
        }
        if (auto v = jsonQuantity(root, "blobGasUsed"))
        {
            blobGasUsed = h.blobGasUsed = *v;
        }
        else if (forkActiveAtGenesis(cancunTime))
        {
            blobGasUsed = h.blobGasUsed = u256(0);
        }
        if (auto v = jsonQuantity(root, "excessBlobGas"))
        {
            excessBlobGas = h.excessBlobGas = *v;
        }
        else if (forkActiveAtGenesis(cancunTime))
        {
            excessBlobGas = h.excessBlobGas = u256(0);
        }
        if (root.isMember("parentBeaconBlockRoot") && !root["parentBeaconBlockRoot"].isNull())
        {
            beaconRoot = h.parentBeaconRoot = crypto::HashType(
                "0x" + padHex(root["parentBeaconBlockRoot"].asString(), 64,
                    "parentBeaconBlockRoot"));
        }
        else if (forkActiveAtGenesis(cancunTime))
        {
            beaconRoot = h.parentBeaconRoot = crypto::HashType();
        }
        if (root.isMember("requestsHash") && !root["requestsHash"].isNull())
        {
            requestsHash = h.requestsHash = crypto::HashType(
                "0x" + padHex(root["requestsHash"].asString(), 64, "requestsHash"));
        }
        else if (forkActiveAtGenesis(pragueTime))
        {
            // EIP-7685: sha256("") over the empty requests list.
            requestsHash = h.requestsHash = crypto::HashType(
                std::string_view(
                    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
                crypto::HashType::FromHex);
        }
        auto hash = protocol::ethHeaderHash(h);

        // --- emit the INI (layout follows tools/BcosBuilder/src/tpl/config.genesis.el) ---
        std::ostringstream ini;
        ini << "; ============================================================================\n"
            << "; FISCO-BCOS Ethereum L1 EL-mode genesis configuration (config.genesis)\n"
            << "; Generated by eth-sync-check --genesis2ini from " << path << "\n"
            << "; ============================================================================\n\n"
            << "[chain]\n"
            << "    sm_crypto=false\n"
            << "    group_id=group0\n"
            << "    chain_id=" << chainId->str() << "\n\n"
            << "[web3]\n"
            << "    chain_id=" << chainId->str() << "\n\n"
            << "[version]\n"
            << "    compatibility_version=3.18.0\n\n"
            << "[consensus]\n"
            << "    consensus_type=pbft\n"
            << "    block_tx_count_limit=1000\n"
            << "    leader_period=100\n"
            << "    node.0=\n\n"
            << "[tx]\n"
            << "    gas_limit=" << h.gasLimit.str() << "\n\n"
            << "[executor]\n"
            << "    version=2\n"
            << "    is_auth_check=false\n"
            << "    auth_admin_account=0x0000000000000000000000000000000000000000\n"
            << "    is_serial_execute=true\n\n"
            << "[ethereum]\n"
            << "    mode=el\n\n"
            << "[fork_timestamps]\n";
        for (auto const& warning : warnings)
        {
            ini << "    ; WARNING: " << warning << "\n";
        }
        ini << "    london_time=0\n"
            << "    paris_time=" << parisTime << "\n"
            << "    merge_block=0\n"
            << "    shanghai_time=" << shanghaiTime << "\n"
            << "    cancun_time=" << cancunTime << "\n"
            << "    prague_time=" << pragueTime << "\n";
        if (osakaTime)
        {
            ini << "    osaka_time=" << osakaTime->str() << "\n";
        }
        if (bpo1Time)
        {
            ini << "    bpo1_time=" << bpo1Time->str() << "\n";
        }
        if (bpo2Time)
        {
            ini << "    bpo2_time=" << bpo2Time->str() << "\n";
        }
        ini << "\n";
        for (size_t i = 0; i < genesis.m_allocs.size(); ++i)
        {
            auto const& a = genesis.m_allocs[i];
            ini << "[alloc." << i << "]\n"
                << "    address=0x" << a.address << "\n"
                << "    balance=" << a.balance.str() << "\n"
                << "    nonce=" << a.nonce << "\n";
            if (!a.code.empty())
            {
                ini << "    code=0x" << a.code << "\n";
            }
            if (!a.storage.empty())
            {
                ini << "[alloc." << i << ".storage]\n";
                for (auto const& [slot, value] : a.storage)
                {
                    ini << "    0x" << slot << "=0x" << value << "\n";
                }
            }
            ini << "\n";
        }
        ini << "[eth_genesis_header]\n"
            << "    parent_hash=" << h.parentInfo.blockHash.hexPrefixed() << "\n"
            << "    sha3_uncles=" << h.uncleHash.hexPrefixed() << "\n"
            << "    miner=0x" << padHex(
                   root.isMember("coinbase") ? root["coinbase"].asString() : "0x0", 40, "coinbase")
            << "\n"
            << "    state_root=" << h.stateRoot.hexPrefixed() << "\n"
            << "    transactions_root=" << h.txsRoot.hexPrefixed() << "\n"
            << "    receipts_root=" << h.receiptsRoot.hexPrefixed() << "\n"
            << "    logs_bloom="
            << (root.isMember("logsBloom") && !root["logsBloom"].isNull() ?
                       "0x" + padHex(root["logsBloom"].asString(), 512, "logsBloom") :
                       "0x" + std::string(512, '0'))
            << "\n"
            << "    difficulty=" << quantityHex(h.difficulty) << "\n"
            << "    number=0x0\n"
            << "    gas_limit=" << quantityHex(h.gasLimit) << "\n"
            << "    gas_used=" << quantityHex(h.gasUsed) << "\n"
            << "    timestamp=" << quantityHex(timestamp) << "\n"
            << "    extra_data="
            << (root.isMember("extraData") ? "0x" + stripHexPrefix(root["extraData"].asString()) :
                                           std::string("0x"))
            << "\n"
            << "    mix_hash=" << h.prevRandao.hexPrefixed() << "\n"
            << "    nonce=" << h.nonce.hexPrefixed() << "\n";
        if (baseFee)
        {
            ini << "    base_fee_per_gas=" << quantityHex(*baseFee) << "\n";
        }
        if (withdrawalsHash)
        {
            ini << "    withdrawals_root=" << withdrawalsHash->hexPrefixed() << "\n";
        }
        if (blobGasUsed)
        {
            ini << "    blob_gas_used=" << quantityHex(*blobGasUsed) << "\n";
        }
        if (excessBlobGas)
        {
            ini << "    excess_blob_gas=" << quantityHex(*excessBlobGas) << "\n";
        }
        if (beaconRoot)
        {
            ini << "    parent_beacon_block_root=" << beaconRoot->hexPrefixed() << "\n";
        }
        if (requestsHash)
        {
            ini << "    requests_hash=" << requestsHash->hexPrefixed() << "\n";
        }
        ini << "    hash=" << hash.hexPrefixed() << "\n";

        auto const text = ini.str();
        if (output)
        {
            std::ofstream out(*output, std::ios::trunc);
            if (!out)
            {
                throw std::runtime_error("cannot open output file " + *output);
            }
            out << text;
        }
        else
        {
            std::cout << text;
        }
        // Diagnostics (and any geth-embedded claims to cross-check against) go to
        // stderr so stdout stays a clean INI.
        std::cerr << "allocs: " << genesis.m_allocs.size()
                  << "  state_root: " << h.stateRoot.hexPrefixed()
                  << "  header hash: " << hash.hexPrefixed() << std::endl;
        for (auto const& warning : warnings)
        {
            std::cerr << "WARNING: " << warning << std::endl;
        }
        if (root.isMember("stateRoot") &&
            stripHexPrefix(root["stateRoot"].asString()) != trie.root.hex())
        {
            std::cerr << "WARNING: genesis.json stateRoot "
                      << root["stateRoot"].asString() << " differs from the computed "
                      << h.stateRoot.hexPrefixed() << std::endl;
        }
        if (root.isMember("hash") &&
            stripHexPrefix(root["hash"].asString()) != hash.hex())
        {
            std::cerr << "WARNING: genesis.json hash " << root["hash"].asString()
                      << " differs from the computed " << hash.hexPrefixed() << std::endl;
        }
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "--genesis2ini: " << e.what() << std::endl;
        return 1;
    }
}

/// --enode-from-key: read a 32-byte secp256k1 private key file (same contract as
/// EthereumSyncInitializer::readNodeKeyFile — hex text, optional 0x prefix,
/// surrounding whitespace ignored), derive the uncompressed public key (without
/// the 04 prefix, i.e. the devp2p node id) and print the enode:// URL.
int runEnodeFromKey(std::string const& path, std::string const& ip, uint16_t port)
{
    try
    {
        std::ifstream in(path);
        if (!in)
        {
            throw std::runtime_error("cannot open node key file " + path);
        }
        std::stringstream ss;
        ss << in.rdbuf();
        auto hex = ss.str();
        boost::algorithm::trim(hex);
        if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
        {
            hex.erase(0, 2);
        }
        // Exactly 64 hex chars: fromHex pads odd-length input with a leading '0',
        // which would silently shift a 63-char typo into a wrong-but-valid key.
        if (hex.size() != 64 ||
            !std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); }))
        {
            throw std::runtime_error("node key file " + path +
                                     " must hold exactly 64 hex chars "
                                     "(a 32-byte secp256k1 private key, optional 0x prefix)");
        }
        bcos::crypto::KeyFactoryImpl keyFactory;
        auto secret = keyFactory.createKey(bcos::fromHex(hex));
        auto pub = bcos::crypto::secp256k1PriToPub(secret);
        std::cout << "enode://" << bcos::toHexStringWithPrefix(pub->data()).substr(2) << "@"
                  << ip << ":" << port << std::endl;
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "--enode-from-key: " << e.what() << std::endl;
        return 1;
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
    std::optional<std::string> genesis2IniPath;
    std::optional<std::string> genesis2IniOutput;
    std::optional<std::string> enodeKeyPath;
    std::string enodeIp = "127.0.0.1";
    uint16_t enodePort = 30303;
    std::optional<std::string> expectRoot;
    std::optional<int64_t> verifyTxBlock;
    std::optional<std::string> rawTxHex;
    std::optional<uint64_t> mergeBlockOverride;
    int executorVersion = 0;
    bool opMode = false;
    OpChainConfig opConfig = opSepoliaConfig();
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--op")
        {
            opMode = true;
        }
        else if (arg == "--op-chain-id" && i + 1 < argc)
        {
            opConfig.chainId = std::stoull(argv[++i]);
        }
        else if (arg == "--op-block-time" && i + 1 < argc)
        {
            opConfig.blockTimeSeconds = std::stoull(argv[++i]);
        }
        else if (arg == "--op-fork" && i + 2 < argc)
        {
            std::string name = argv[++i];
            uint64_t ts = std::stoull(argv[++i]);
            if (!setOpForkTime(opConfig, name, ts))
            {
                std::cerr << "unknown OP fork name: " << name << std::endl;
                return 1;
            }
        }
        else if (arg == "--rpc" && i + 1 < argc)
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
        else if (arg == "--genesis2ini" && i + 1 < argc)
        {
            genesis2IniPath = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc)
        {
            genesis2IniOutput = argv[++i];
        }
        else if (arg == "--enode-from-key" && i + 1 < argc)
        {
            enodeKeyPath = argv[++i];
        }
        else if (arg == "--ip" && i + 1 < argc)
        {
            enodeIp = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            auto port = std::stoul(argv[++i]);
            if (port == 0 || port > 65535)
            {
                std::cerr << "--port out of range: " << port << std::endl;
                return 1;
            }
            enodePort = static_cast<uint16_t>(port);
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
        else if (arg == "--executor-version" && i + 1 < argc)
        {
            executorVersion = std::stoi(argv[++i]);
        }
    }
    if (genesisPath)
    {
        runGenesisCheck(*genesisPath, expectRoot, executorVersion);
        return failures == 0 ? 0 : 1;
    }
    if (genesis2IniPath)
    {
        return runGenesis2Ini(*genesis2IniPath, genesis2IniOutput);
    }
    if (enodeKeyPath)
    {
        return runEnodeFromKey(*enodeKeyPath, enodeIp, enodePort);
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
        if (opMode)
        {
            rpcs.push_back("https://sepolia.optimism.io");
            rpcs.push_back("https://optimism-sepolia-rpc.publicnode.com");
        }
        else
        {
            rpcs.push_back("https://1rpc.io/sepolia");
            rpcs.push_back("https://ethereum-sepolia-rpc.publicnode.com");
        }
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
        report("header hash", bcos::protocol::ethHeaderHash(h) == canonical);

        // 2. Header field rules against the parent: OP Stack rules in --op mode
        //    (validateOpHeader covers the base-fee recomputation, the fork-gated field
        //    presence and the extraData shapes), Ethereum PoS rules otherwise.
        if (prev)
        {
            if (opMode)
            {
                auto op = validateOpHeader(h, *prev, opConfig);
                report("OP header fields", op.valid, op.error);
            }
            else
            {
                auto pos = validateHeaderPoS(h, *prev, config);
                report("PoS header fields", pos.valid, pos.error);
                if (h.baseFee && prev->baseFee)
                {
                    auto expected = computeNextBaseFee(*prev);
                    report("baseFee recompute", *h.baseFee == expected);
                }
            }
        }
        prev = h;

        // 3. Withdrawals trie root (Shanghai+): rebuild the RLP and compare.
        //    Skipped in --op mode: OP blocks never carry withdrawals, and from Isthmus
        //    the withdrawalsHash is the L2ToL1MessagePasser storage root, not a
        //    withdrawals trie root.
        if (!opMode && h.withdrawalsHash && (*block).isMember("withdrawals"))
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
