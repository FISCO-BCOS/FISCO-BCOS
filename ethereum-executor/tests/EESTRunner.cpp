/**
 * @file EESTRunner.cpp
 * @brief Standalone Ethereum Execution Spec Tests (EEST) runner.
 *
 * Runs the ethereum/execution-specs fixture release (tests@vX.Y.Z: state_tests,
 * blockchain_tests, blockchain_tests_engine) through the production L1 block pipeline
 * (EESTProductionChain.h). Per-file and per-test diagnostics, single-fixture regression
 * mode, TBB-based parallel execution over files, and a known-failures gate for CI.
 *
 * Usage:
 *   ./eest-runner --fixture-dir /path/to/fixtures           # run all
 *   ./eest-runner --fixture-dir /path/to/fixtures --quiet   # summary only
 *   ./eest-runner --fixture /path/to/single.json            # single file
 *   ./eest-runner --fixture-dir /path --jobs 8              # parallel
 */

#include "EESTRunner.h"
#include "EESTProductionChain.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/TBBWait.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <evmc/evmc.h>
#include <tbb/concurrent_vector.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/core.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <range/v3/algorithm/equal.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "bcos-protocol/TransactionStatus.h"

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
namespace fs = std::filesystem;

// =============================================================================
//  Fixture loader — out-of-line definitions of the functions declared in
//  EESTRunner.h (moved out of the header so only the data structures stay
//  in the header).
// =============================================================================
namespace bcos::test
{
std::string readHexField(Json::Value const& obj, std::string const& key)
{
    if (!obj.isMember(key) || obj[key].isNull())
        return {};
    auto const& val = obj[key];
    if (val.isString())
        return val.asString();
    if (val.isInt64())
    {
        // Small integers: format as hex
        std::ostringstream oss;
        oss << "0x" << std::hex << val.asInt64();
        return oss.str();
    }
    return {};
}

std::string readRequiredHex(Json::Value const& obj, std::string const& key)
{
    auto val = readHexField(obj, key);
    if (val.empty() && !obj.isMember(key))
        BOOST_THROW_EXCEPTION(std::runtime_error("Missing required field: " + key));
    return val;
}

bcos::bytes hexToBytes(std::string const& hex)
{
    if (hex.empty() || hex == "0x")
        return {};
    std::string cleaned = hex;
    if (cleaned.size() >= 2 && cleaned[0] == '0' && cleaned[1] == 'x')
        cleaned = cleaned.substr(2);
    bcos::bytes result;
    boost::algorithm::unhex(cleaned, std::back_inserter(result));
    return result;
}

bcos::u256 hexToU256(std::string const& hex)
{
    if (hex.empty() || hex == "0x")
        return 0;
    return bcos::u256(hex);
}

int64_t hexToInt64(std::string const& hex)
{
    if (hex.empty() || hex == "0x")
        return 0;
    return static_cast<int64_t>(hexToU256(hex));
}

int64_t hexToTimestamp(std::string const& hex)
{
    if (hex.empty() || hex == "0x")
        return 0;
    // Preserve the low 64 bits exactly, avoid sign-extension issues.
    return static_cast<int64_t>(static_cast<uint64_t>(hexToU256(hex)));
}

std::string strip0x(std::string const& hex)
{
    if (hex.size() >= 2 && hex[0] == '0' && hex[1] == 'x')
        return hex.substr(2);
    return hex;
}

std::string detectFixtureFormat(Json::Value const& fixtureJson)
{
    if (fixtureJson.isMember("env") && fixtureJson["env"].isObject() &&
        fixtureJson.isMember("post") && fixtureJson["post"].isObject())
        return "state_test";
    if (fixtureJson.isMember("blocks") && fixtureJson["blocks"].isArray())
        return "blockchain_test";
    return "unknown";
}

EESTEnvironment parseEnvironment(Json::Value const& envJson)
{
    EESTEnvironment env;
    env.coinbase = readHexField(envJson, "currentCoinbase");
    // currentGasLimit: use readHexField with default so that corner-case
    // fixtures (e.g. blockchain_test_from_state_test) don't cause parse failures.
    env.gasLimit = readHexField(envJson, "currentGasLimit");
    if (env.gasLimit.empty())
        env.gasLimit = "0x7fffffffffffffff";  // effectively unlimited
    env.number = readRequiredHex(envJson, "currentNumber");
    env.timestamp = readRequiredHex(envJson, "currentTimestamp");
    env.difficulty = readHexField(envJson, "currentDifficulty");
    env.baseFee = readHexField(envJson, "currentBaseFee");
    env.random = readHexField(envJson, "currentRandom");
    env.excessBlobGas = readHexField(envJson, "currentExcessBlobGas");
    env.blobGasUsed = readHexField(envJson, "currentBlobGasUsed");
    env.parentBeaconRoot = readHexField(envJson, "parentBeaconBlockRoot");
    return env;
}

EESTBlockHeader parseBlockHeader(Json::Value const& headerJson)
{
    EESTBlockHeader h;
    h.coinbase = readHexField(headerJson, "coinbase");
    h.gasLimit = readHexField(headerJson, "gasLimit");
    h.number = readHexField(headerJson, "number");
    h.timestamp = readHexField(headerJson, "timestamp");
    h.difficulty = readHexField(headerJson, "difficulty");
    h.baseFee = readHexField(headerJson, "baseFeePerGas");
    h.random = readHexField(headerJson, "mixHash");
    h.excessBlobGas = readHexField(headerJson, "excessBlobGas");
    h.blobGasUsed = readHexField(headerJson, "blobGasUsed");
    h.parentBeaconRoot = readHexField(headerJson, "parentBeaconBlockRoot");
    h.hash = readHexField(headerJson, "hash");
    return h;
}

EESTAccount parseAccount(Json::Value const& accJson)
{
    EESTAccount acc;
    acc.nonce = readHexField(accJson, "nonce");
    acc.balance = readHexField(accJson, "balance");
    acc.code = readHexField(accJson, "code");
    if (accJson.isMember("storage") && !accJson["storage"].isNull())
    {
        auto const& storageJson = accJson["storage"];
        for (auto it = storageJson.begin(); it != storageJson.end(); ++it)
        {
            acc.storage[it.key().asString()] = (*it).asString();
        }
    }
    return acc;
}
}  // namespace bcos::test

namespace bcos::test
{
EESTTransaction parseTransaction(Json::Value const& txJson)
{
    EESTTransaction tx;
    tx.nonce = readHexField(txJson, "nonce");
    tx.chainId = readHexField(txJson, "chainId");
    tx.gasPrice = readHexField(txJson, "gasPrice");
    tx.maxPriorityFeePerGas = readHexField(txJson, "maxPriorityFeePerGas");
    tx.maxFeePerGas = readHexField(txJson, "maxFeePerGas");
    tx.to = readHexField(txJson, "to");
    tx.sender = readHexField(txJson, "sender");
    tx.secretKey = readHexField(txJson, "secretKey");
    tx.maxFeePerBlobGas = readHexField(txJson, "maxFeePerBlobGas");

    // gasLimit array
    if (txJson.isMember("gasLimit") && txJson["gasLimit"].isArray())
    {
        for (auto const& v : txJson["gasLimit"])
            tx.gasLimit.push_back(v.asString());
    }
    // value array
    if (txJson.isMember("value") && txJson["value"].isArray())
    {
        for (auto const& v : txJson["value"])
            tx.value.push_back(v.asString());
    }
    // data array
    if (txJson.isMember("data") && txJson["data"].isArray())
    {
        for (auto const& v : txJson["data"])
            tx.data.push_back(v.asString());
    }
    // accessLists array
    if (txJson.isMember("accessLists") && txJson["accessLists"].isArray())
    {
        for (auto const& al : txJson["accessLists"])
        {
            if (al.isNull())
            {
                tx.accessLists.push_back(std::nullopt);
                continue;
            }
            std::vector<std::pair<std::string, std::vector<std::string>>> entries;
            if (al.isArray())
            {
                for (auto const& entry : al)
                {
                    std::string addr = readHexField(entry, "address");
                    std::vector<std::string> keys;
                    if (entry.isMember("storageKeys") && entry["storageKeys"].isArray())
                    {
                        for (auto const& k : entry["storageKeys"])
                            keys.push_back(k.asString());
                    }
                    entries.emplace_back(std::move(addr), std::move(keys));
                }
            }
            tx.accessLists.push_back(std::move(entries));
        }
    }
    // authorizationList
    if (txJson.isMember("authorizationList") && !txJson["authorizationList"].isNull())
    {
        tx.authorizationList.emplace();
        for (auto const& auth : txJson["authorizationList"])
            tx.authorizationList->push_back(auth);
    }
    // blobVersionedHashes (EIP-4844)
    if (txJson.isMember("blobVersionedHashes") && txJson["blobVersionedHashes"].isArray())
    {
        for (auto const& h : txJson["blobVersionedHashes"])
            tx.blobVersionedHashes.push_back(h.asString());
    }
    return tx;
}

EESTForkPost parseForkPost(Json::Value const& postJson)
{
    EESTForkPost post;
    if (postJson.isMember("indexes") && !postJson["indexes"].isNull())
    {
        auto const& idx = postJson["indexes"];
        post.dataIndex = idx.isMember("data") ? idx["data"].asInt() : 0;
        post.gasIndex = idx.isMember("gas") ? idx["gas"].asInt() : 0;
        post.valueIndex = idx.isMember("value") ? idx["value"].asInt() : 0;
    }
    post.stateRoot = readHexField(postJson, "hash");
    post.logsHash = readHexField(postJson, "logs");
    post.txBytes = readHexField(postJson, "txbytes");
    if (postJson.isMember("expectException") && !postJson["expectException"].isNull())
    {
        auto const& exc = postJson["expectException"];
        if (exc.isObject() && exc.isMember("type"))
            post.expectException = exc["type"].asString();
        else if (exc.isString())
            post.expectException = exc.asString();
    }
    if (postJson.isMember("state") && !postJson["state"].isNull())
    {
        auto const& stateJson = postJson["state"];
        for (auto it = stateJson.begin(); it != stateJson.end(); ++it)
            post.state[it.key().asString()] = parseAccount(*it);
    }
    return post;
}

EESTFixture parseFixture(std::string const& name, Json::Value const& fixtureJson)
{
    EESTFixture fixture;
    fixture.name = name;
    fixture.env = parseEnvironment(fixtureJson["env"]);

    // Pre state
    if (fixtureJson.isMember("pre") && !fixtureJson["pre"].isNull())
    {
        auto const& preJson = fixtureJson["pre"];
        for (auto it = preJson.begin(); it != preJson.end(); ++it)
            fixture.pre[it.key().asString()] = parseAccount(*it);
    }

    // Transaction
    fixture.transaction = parseTransaction(fixtureJson["transaction"]);

    // Post state (per fork)
    if (fixtureJson.isMember("post") && !fixtureJson["post"].isNull())
    {
        auto const& postJson = fixtureJson["post"];
        for (auto it = postJson.begin(); it != postJson.end(); ++it)
        {
            std::vector<EESTForkPost> posts;
            for (auto const& p : *it)
                posts.push_back(parseForkPost(p));
            fixture.post[it.key().asString()] = std::move(posts);
        }
    }

    // Config
    if (fixtureJson.isMember("config"))
    {
        auto chainIdHex = readHexField(fixtureJson["config"], "chainid");
        fixture.chainId = hexToInt64(chainIdHex);
        if (fixture.chainId == 0)
            fixture.chainId = 1;
    }

    return fixture;
}

std::vector<EESTFixture> loadEESTFixtures(std::string const& filePath)
{
    std::vector<EESTFixture> fixtures;

    // Read file
    std::ifstream file(filePath);
    if (!file.is_open())
        BOOST_THROW_EXCEPTION(std::runtime_error("Cannot open fixture file: " + filePath));

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors))
        BOOST_THROW_EXCEPTION(
            std::runtime_error("JSON parse error in " + filePath + ": " + errors));

    // The root is a map of test_name → fixture_data
    int skippedBlockchain = 0;
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (it.key().asString().rfind("//", 0) == 0 ||  // comment entry
            it.key().asString().rfind("_", 0) == 0)     // meta entry like _info
            continue;

        // Skip non-object values (e.g. metadata files with string/bool/int entries)
        if (!it->isObject())
            continue;

        // Per-fixture format check: some state_tests directories contain
        // blockchain_test_from_state_test fixtures that lack currentGasLimit.
        auto fmt = detectFixtureFormat(*it);
        if (fmt == "blockchain_test")
        {
            ++skippedBlockchain;
            continue;
        }

        try
        {
            fixtures.push_back(parseFixture(it.key().asString(), *it));
        }
        catch (std::exception const& e)
        {
            std::cerr << "Warning: Failed to parse fixture '" << it.key().asString()
                      << "': " << e.what() << std::endl;
        }
    }

    if (skippedBlockchain > 0 && fixtures.empty())
    {
        std::cerr << "Info: " << filePath << " contains " << skippedBlockchain
                  << " blockchain_test fixture(s); use --fixture-dir with the"
                  << " blockchain_tests directory or the individual file.\n";
    }

    return fixtures;
}

EESTTransaction parseBlockchainTransaction(Json::Value const& txJson)
{
    EESTTransaction tx;
    tx.nonce = readHexField(txJson, "nonce");
    tx.chainId = readHexField(txJson, "chainId");
    tx.gasPrice = readHexField(txJson, "gasPrice");
    tx.maxPriorityFeePerGas = readHexField(txJson, "maxPriorityFeePerGas");
    tx.maxFeePerGas = readHexField(txJson, "maxFeePerGas");
    // blockchain tests have scalar values, wrap in single-element arrays
    auto gasLimit = readHexField(txJson, "gasLimit");
    if (!gasLimit.empty())
        tx.gasLimit.push_back(gasLimit);
    else
        tx.gasLimit.push_back("0x0");
    tx.to = readHexField(txJson, "to");
    auto value = readHexField(txJson, "value");
    if (!value.empty())
        tx.value.push_back(value);
    else
        tx.value.push_back("0x0");
    auto data = readHexField(txJson, "data");
    if (!data.empty())
        tx.data.push_back(data);
    else
        tx.data.push_back("0x");
    tx.sender = readHexField(txJson, "sender");
    tx.secretKey = readHexField(txJson, "secretKey");
    tx.maxFeePerBlobGas = readHexField(txJson, "maxFeePerBlobGas");

    // accessList (optional, array format)
    if (txJson.isMember("accessList") && txJson["accessList"].isArray())
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> entries;
        for (auto const& entry : txJson["accessList"])
        {
            std::string addr = readHexField(entry, "address");
            std::vector<std::string> keys;
            if (entry.isMember("storageKeys") && entry["storageKeys"].isArray())
                for (auto const& k : entry["storageKeys"])
                    keys.push_back(k.asString());
            entries.emplace_back(std::move(addr), std::move(keys));
        }
        tx.accessLists.push_back(std::move(entries));
    }

    // authorizationList (EIP-7702)
    if (txJson.isMember("authorizationList") && !txJson["authorizationList"].isNull())
    {
        tx.authorizationList.emplace();
        for (auto const& auth : txJson["authorizationList"])
            tx.authorizationList->push_back(auth);
    }

    // blobVersionedHashes
    if (txJson.isMember("blobVersionedHashes") && txJson["blobVersionedHashes"].isArray())
        for (auto const& h : txJson["blobVersionedHashes"])
            tx.blobVersionedHashes.push_back(h.asString());

    // Determine typed tx kind
    auto type = readHexField(txJson, "type");
    // type is stored separately from the web3TypedTxKind; we'll set it later

    return tx;
}

EESTBlockchainFixture parseBlockchainFixture(
    std::string const& name, Json::Value const& fixtureJson)
{
    EESTBlockchainFixture fixture;
    fixture.name = name;

    // genesisBlockHeader (optional: engine_x format lacks it)
    if (fixtureJson.isMember("genesisBlockHeader") && fixtureJson["genesisBlockHeader"].isObject())
        fixture.genesisBlockHeader = parseBlockHeader(fixtureJson["genesisBlockHeader"]);
    if (fixtureJson.isMember("genesisBlockHeader"))
        fixture.genesisHeaderJson = fixtureJson["genesisBlockHeader"];
    fixture.genesisRLP = readHexField(fixtureJson, "genesisRLP");
    fixture.lastBlockHash = readHexField(fixtureJson, "lastblockhash");
    if (fixtureJson.isMember("engineNewPayloads") && fixtureJson["engineNewPayloads"].isArray())
    {
        for (auto const& payloadJson : fixtureJson["engineNewPayloads"])
        {
            EESTEnginePayload payload;
            payload.params = payloadJson["params"];
            if (payloadJson.isMember("newPayloadVersion"))
                payload.newPayloadVersion = hexToInt64(payloadJson["newPayloadVersion"].asString());
            if (payloadJson.isMember("forkchoiceUpdatedVersion"))
                payload.forkchoiceUpdatedVersion =
                    hexToInt64(payloadJson["forkchoiceUpdatedVersion"].asString());
            if (payloadJson.isMember("validationError") &&
                payloadJson["validationError"].isString())
                payload.validationError = payloadJson["validationError"].asString();
            if (payloadJson.isMember("errorCode") && !payloadJson["errorCode"].isNull())
            {
                auto const& code = payloadJson["errorCode"];
                payload.errorCode = code.isString() ? std::stoi(code.asString()) : code.asInt();
            }
            fixture.enginePayloads.push_back(std::move(payload));
        }
    }
    if (fixtureJson.isMember("network") && fixtureJson["network"].isString())
        fixture.network = fixtureJson["network"].asString();

    // pre state
    if (fixtureJson.isMember("pre") && !fixtureJson["pre"].isNull())
        for (auto it = fixtureJson["pre"].begin(); it != fixtureJson["pre"].end(); ++it)
            fixture.pre[it.key().asString()] = parseAccount(*it);

    // blocks
    if (fixtureJson.isMember("blocks") && fixtureJson["blocks"].isArray())
    {
        for (auto const& blockJson : fixtureJson["blocks"])
        {
            EESTBlock block;
            if (blockJson.isMember("blockHeader") && blockJson["blockHeader"].isObject())
                block.blockHeader = parseBlockHeader(blockJson["blockHeader"]);
            block.rlp = readHexField(blockJson, "rlp");
            if (blockJson.isMember("expectException") && blockJson["expectException"].isString())
                block.expectException = blockJson["expectException"].asString();

            if (blockJson.isMember("transactions") && blockJson["transactions"].isArray())
                for (auto const& txJson : blockJson["transactions"])
                    block.transactions.push_back(parseBlockchainTransaction(txJson));

            // Withdrawals (EIP-4895)
            if (blockJson.isMember("withdrawals") && blockJson["withdrawals"].isArray())
            {
                for (auto const& wJson : blockJson["withdrawals"])
                {
                    EESTWithdrawal w;
                    w.index = readHexField(wJson, "index");
                    w.validatorIndex = readHexField(wJson, "validatorIndex");
                    w.address = readRequiredHex(wJson, "address");
                    w.amount = readRequiredHex(wJson, "amount");
                    block.withdrawals.push_back(std::move(w));
                }
            }

            fixture.blocks.push_back(std::move(block));
        }
    }

    // postState
    if (fixtureJson.isMember("postState") && !fixtureJson["postState"].isNull())
        for (auto it = fixtureJson["postState"].begin(); it != fixtureJson["postState"].end(); ++it)
            fixture.postState[it.key().asString()] = parseAccount(*it);

    // config
    if (fixtureJson.isMember("config"))
    {
        auto chainIdHex = readHexField(fixtureJson["config"], "chainid");
        fixture.chainId = hexToInt64(chainIdHex);
        if (fixture.chainId == 0)
            fixture.chainId = 1;
        if (fixture.network.empty() && fixtureJson["config"].isMember("network"))
            fixture.network = fixtureJson["config"]["network"].asString();
    }

    return fixture;
}

std::vector<EESTBlockchainFixture> loadEESTBlockchainFixtures(std::string const& filePath)
{
    std::vector<EESTBlockchainFixture> fixtures;

    std::ifstream file(filePath);
    if (!file.is_open())
        BOOST_THROW_EXCEPTION(std::runtime_error("Cannot open fixture file: " + filePath));

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors))
        BOOST_THROW_EXCEPTION(
            std::runtime_error("JSON parse error in " + filePath + ": " + errors));

    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (it.key().asString().rfind("//", 0) == 0 || it.key().asString().rfind("_", 0) == 0)
            continue;

        // Skip non-object values
        if (!it->isObject())
            continue;

        try
        {
            fixtures.push_back(parseBlockchainFixture(it.key().asString(), *it));
        }
        catch (std::exception const& e)
        {
            std::cerr << "Warning: Failed to parse blockchain fixture '" << it.key().asString()
                      << "': " << e.what() << std::endl;
        }
    }

    return fixtures;
}

evmc_revision forkNameToRevision(std::string const& forkName)
{
    // Normalize: lowercase
    auto lower = forkName;
    boost::algorithm::to_lower(lower);

    if (lower == "frontier")
        return EVMC_FRONTIER;
    if (lower == "homestead")
        return EVMC_HOMESTEAD;
    if (lower == "tangerine whistle" || lower == "tangerinewhistle" || lower == "eip150")
        return EVMC_TANGERINE_WHISTLE;
    if (lower == "spurious dragon" || lower == "spuriousdragon" || lower == "eip158")
        return EVMC_SPURIOUS_DRAGON;
    if (lower == "byzantium")
        return EVMC_BYZANTIUM;
    if (lower == "constantinople")
        return EVMC_CONSTANTINOPLE;
    if (lower == "constantinoplefix" || lower == "petersburg")
        return EVMC_PETERSBURG;
    if (lower == "istanbul")
        return EVMC_ISTANBUL;
    // Muir Glacier (EIP-2384) only delays the difficulty bomb; it changes no
    // EVM semantics over Istanbul. Mapping it to Berlin would wrongly activate
    // EIP-2929 access-list gas for MuirGlacier fixtures.
    if (lower == "muir glacier" || lower == "muirglacier")
        return EVMC_ISTANBUL;
    if (lower == "berlin")
        return EVMC_BERLIN;
    if (lower == "london")
        return EVMC_LONDON;
    if (lower == "paris" || lower == "merge")
        return EVMC_PARIS;
    if (lower == "shanghai")
        return EVMC_SHANGHAI;
    if (lower == "cancun")
        return EVMC_CANCUN;
    if (lower == "prague")
        return EVMC_PRAGUE;
    if (lower == "osaka")
        return EVMC_OSAKA;

    // Unknown fork → default to the latest handled revision so a future fork
    // is not silently mis-executed as Cancun (which would produce false
    // pass/fail outcomes in the compliance suite).
    return EVMC_OSAKA;
}
}  // namespace bcos::test

// =============================================================================
//  Command-line options (parsed early)
// =============================================================================
struct ProgramOptions
{
    std::string fixtureDir;     // directory with .json fixtures
    std::string singleFixture;  // single fixture file path
    bool quiet = false;         // suppress per-file/per-test output
    bool verbose = false;       // account-level diagnostics on a state-root mismatch
    bool noLog = false;         // suppress BCOS internal trace/debug log
    bool help = false;
    unsigned jobs = 0;          // fixture files processed concurrently; 0 = hardware threads
    std::string knownFailures;  // file of regexes over "name [fork]" naming the open gaps
};

ProgramOptions g_opts;

// =============================================================================
//  Global counters (atomic for parallel safety)
// =============================================================================
std::atomic<int> g_totalTests{0};
std::atomic<int> g_totalPassed{0};
std::atomic<int> g_totalFailed{0};

/// Per fixture-class counters. The summary prints one line per class so CI can require
/// 100% on the classes this runner executes while the classes it does not (counted as
/// failures, see processFixtureFile) stay visible in the overall rate.
struct ClassCounters
{
    std::atomic<int> total{0};
    std::atomic<int> passed{0};
    std::atomic<int> failed{0};
};
constexpr std::array<std::string_view, 6> c_fixtureClasses{"state_test", "blockchain_test",
    "blockchain_engine_test", "blockchain_engine_x_test", "blockchain_sync_test",
    "transaction_test"};
std::array<ClassCounters, c_fixtureClasses.size()> g_classCounters;

ClassCounters& countersFor(std::string_view format)
{
    auto it = std::find(c_fixtureClasses.begin(), c_fixtureClasses.end(), format);
    if (it == c_fixtureClasses.end())
    {
        BOOST_THROW_EXCEPTION(std::logic_error("unknown fixture class " + std::string(format)));
    }
    return g_classCounters[static_cast<std::size_t>(it - c_fixtureClasses.begin())];
}

void countOutcome(std::string_view format, bool passed, int n = 1)
{
    auto& counters = countersFor(format);
    counters.total += n;
    g_totalTests += n;
    if (passed)
    {
        counters.passed += n;
        g_totalPassed += n;
    }
    else
    {
        counters.failed += n;
        g_totalFailed += n;
    }
}

// Per-error-type counters
std::atomic<int> g_expectedException{0};
std::atomic<int> g_unexpectedFailure{0};

// Files that were intentionally skipped (engine-format duplicates /
// transaction_tests) or failed to load/parse — kept so the summary's 100%
// carries its domain and a loader regression can't silently raise the pass rate.
std::atomic<int> g_loadFailures{0};

// Failure details for summary
struct FailureDetail
{
    std::string testName;
    std::string forkName;
    std::string reason;
};
tbb::concurrent_vector<FailureDetail> g_failureDetails;

// Mutex for console output (keep lines from interleaving)
std::mutex g_printMutex;

void printLine(std::string const& msg)
{
    std::lock_guard<std::mutex> lock(g_printMutex);
    std::cout << msg << '\n';
}

// =============================================================================
//  Usage
// =============================================================================
void printUsage(char const* prog)
{
    std::cout << R"(Usage: )" << prog << R"( [OPTIONS]

Options:
  --fixture-dir DIR    Directory containing EEST .json fixture files (recursive)
  --fixture FILE       Run a single fixture file only
  --quiet              Suppress per-file/per-test output; show summary only
  --verbose            On a state-root mismatch, list the accounts that differ
  --no-log             Suppress BCOS internal trace/debug log output
  --jobs N             Process N fixture files concurrently (default: hardware threads)
  --known-failures F   File of ECMAScript regexes (one per line, # comments) matched against
                       "<fixture name> [<fork>]"; a failure that matches is a known gap. The
                       exit code is then 1 only for failures outside the file and for file
                       entries that match nothing (stale), so the file shrinks as gaps close.
  --help               Show this help message

Examples:
  )" << prog << R"( --fixture-dir ./fixtures/state_tests
  )" << prog << R"( --fixture-dir ./fixtures/state_tests --quiet
  )" << prog << R"( --fixture ./fixtures/state_tests/cancun/eip4844/test.json

Environment:
  The program also reads EEST_FIXTURE_DIR if --fixture-dir is not set.
)";
}

void parseOptions(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            g_opts.help = true;
            return;
        }
        if (arg == "--quiet" || arg == "-q")
        {
            g_opts.quiet = true;
        }
        else if (arg == "--verbose" || arg == "-v")
        {
            g_opts.verbose = true;
        }
        else if (arg == "--no-log")
        {
            g_opts.noLog = true;
        }
        else if (arg == "--jobs" && i + 1 < argc)
        {
            g_opts.jobs = static_cast<unsigned>(std::stoul(argv[++i]));
        }
        else if (arg.rfind("--jobs=", 0) == 0)
        {
            g_opts.jobs = static_cast<unsigned>(std::stoul(arg.substr(7)));
        }
        else if (arg == "--known-failures" && i + 1 < argc)
        {
            g_opts.knownFailures = argv[++i];
        }
        else if (arg.rfind("--known-failures=", 0) == 0)
        {
            g_opts.knownFailures = arg.substr(17);
        }
        else if (arg == "--fixture-dir" && i + 1 < argc)
        {
            g_opts.fixtureDir = argv[++i];
        }
        else if (arg == "--fixture" && i + 1 < argc)
        {
            g_opts.singleFixture = argv[++i];
        }
        else if (arg.rfind("--fixture-dir=", 0) == 0)
        {
            g_opts.fixtureDir = arg.substr(14);
        }
        else if (arg.rfind("--fixture=", 0) == 0)
        {
            g_opts.singleFixture = arg.substr(10);
        }
    }
}

// =============================================================================
//  Core test runner
// =============================================================================
namespace
{
/// The fixtures (top-level keys) in a file: a file whose format this runner does not execute
/// still counts every fixture as a failure, so nothing leaves the denominator silently.
int countFixtureEntries(fs::path const& filePath)
{
    std::ifstream file(filePath);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!file.is_open() || !Json::parseFromStream(builder, file, &root, &errors))
    {
        return 1;
    }
    int count = 0;
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        auto const key = it.key().asString();
        if (key.rfind("//", 0) == 0 || key.rfind("_", 0) == 0)
        {
            continue;
        }
        ++count;
    }
    return std::max(count, 1);
}

h256 toH256(std::string const& hex)
{
    auto raw = test::hexToBytes(hex);
    h256 out{};
    if (raw.size() <= h256::SIZE)
    {
        std::copy(raw.begin(), raw.end(), out.begin() + (h256::SIZE - raw.size()));
    }
    else
    {
        std::copy_n(raw.end() - h256::SIZE, h256::SIZE, out.begin());
    }
    return out;
}

bcos::Address toEthAddress(std::string const& hex)
{
    auto raw = test::hexToBytes(hex);
    bcos::Address out{};
    if (raw.size() == bcos::Address::SIZE)
    {
        std::copy(raw.begin(), raw.end(), out.begin());
    }
    return out;
}
}  // namespace

/// Runs EEST fixtures through the production L1 block pipeline (EESTProductionChain.h):
/// a fresh in-memory chain per fixture, seeded from `pre`, then the fixture's blocks
/// through scheduler_v1::EthereumBlockVerifier. No execution logic of its own.
class EESTRunner
{
public:
    static std::string extractForkFromName(std::string const& name)
    {
        // The fork marker is always "[fork_..." in EEST names. Anchoring on the
        // bracket avoids matching a "fork_" in the test-method path
        // ("test_fork_transition...") or in the parameter list
        // ("blocks_after_fork_257").
        auto pos = name.find("[fork_");
        if (pos == std::string::npos)
        {
            return {};
        }
        auto start = pos + 6;  // skip "[fork_"
        auto end = name.find('-', start);
        if (end == std::string::npos)
        {
            end = name.find(']', start);
        }
        if (end == std::string::npos)
        {
            return {};
        }
        return name.substr(start, end - start);
    }

    /// state_tests: one transaction (post.txBytes) over a synthetic block built from `env`,
    /// executed by executeEthereumBlock with no system calls and no PoW reward (t8n's
    /// state-test mode). Passes when the MPT root of the executed state equals post.hash,
    /// or, for an expected exception, when the pipeline refuses the block.
    bool runFixture(test::EESTFixture const& fixture, std::string const& forkName,
        test::EESTForkPost const& post)
    {
        using namespace bcos::test::pipeline;
        auto fail = [&](std::string reason) {
            printLine("FAIL: " + fixture.name + " [" + forkName + "] " + reason);
            g_failureDetails.push_back({fixture.name, forkName, std::move(reason)});
            ++g_unexpectedFailure;
            return false;
        };
        FixtureSchedule schedule;
        try
        {
            schedule = scheduleForNetwork(forkName);
        }
        catch (std::exception const& e)
        {
            return fail(e.what());
        }
        if (!schedule.unsupported.empty())
        {
            return fail("fork schedule cannot express " + schedule.unsupported.front());
        }
        if (post.txBytes.empty())
        {
            return fail("post entry carries no txbytes");
        }

        auto const& env = fixture.env;
        Chain chain;
        chain.stateTestBlockHashes = true;

        // The parent: the fixture's pre-state is its post-state; the trie builder sets the root.
        protocol::EthBlockHeaderData parent;
        parent.number = test::hexToInt64(env.number) - 1;
        parent.timestamp = test::hexToTimestamp(env.timestamp) - 1;
        parent.gasLimit = test::hexToU256(env.gasLimit);
        parent.difficulty = env.difficulty.empty() ? u256(0) : test::hexToU256(env.difficulty);
        parent.uncleHash = protocol::c_emptyOmmersHash;
        parent.txsRoot = ledger::mpt::emptyRootHash();
        parent.receiptsRoot = ledger::mpt::emptyRootHash();
        if (!env.baseFee.empty())
        {
            parent.baseFee = test::hexToU256(env.baseFee);
        }
        try
        {
            chain.seedGenesis(fixture.pre, parent, schedule.forks);
        }
        catch (std::exception const& e)
        {
            return fail(std::string("pre-state import failed: ") + e.what());
        }

        protocol::EthBlockHeaderData header;
        header.parentInfo = protocol::ParentInfo{
            .blockNumber = parent.number, .blockHash = protocol::ethHeaderHash(chain.parent())};
        header.number = test::hexToInt64(env.number);
        header.timestamp = test::hexToTimestamp(env.timestamp);
        header.gasLimit = test::hexToU256(env.gasLimit);
        header.difficulty = parent.difficulty;
        header.coinbase = toEthAddress(env.coinbase);
        header.uncleHash = protocol::c_emptyOmmersHash;
        if (!env.random.empty())
        {
            header.prevRandao = toH256(env.random);
        }
        if (!env.baseFee.empty())
        {
            header.baseFee = test::hexToU256(env.baseFee);
        }
        auto const revision = scheduler_v1::evmcRevisionForTimestamp(
            schedule.forks, header.timestamp, header.difficulty);
        std::optional<std::vector<bytes>> withdrawals;
        if (revision >= EVMC_SHANGHAI)
        {
            withdrawals.emplace();
            header.withdrawalsHash = ledger::mpt::emptyRootHash();
        }
        auto raw = test::hexToBytes(post.txBytes);
        if (revision >= EVMC_CANCUN)
        {
            header.excessBlobGas =
                env.excessBlobGas.empty() ? u256(0) : test::hexToU256(env.excessBlobGas);
            u256 blobGasUsed = 0;
            try
            {
                auto decoded = bcos::rpc::decodeWeb3RawTransaction(
                    bytesConstRef(raw.data(), raw.size()), chain.hashImpl());
                if (decoded)
                {
                    blobGasUsed =
                        u256(decoded->blobVersionedHashes().size()) * protocol::BLOB_GAS_PER_BLOB;
                }
            }
            catch (...)
            {
                // An undecodable transaction is the pipeline's verdict to give below.
            }
            header.blobGasUsed = blobGasUsed;
        }

        auto outcome = chain.executeOnly(header, chain.parent(), std::vector<bytes>{raw},
            withdrawals, schedule, static_cast<uint64_t>(fixture.chainId),
            Verifier::BlockExecutionOptions{.systemCalls = false, .powRewards = false});
        if (!post.expectException.empty())
        {
            if (outcome.error.has_value())
            {
                ++g_expectedException;
                return true;
            }
            return fail("expected " + post.expectException + " but the block executed");
        }
        if (outcome.error.has_value())
        {
            return fail("block execution failed: " + *outcome.error);
        }
        auto const expectedRoot = toH256(post.stateRoot);
        if (outcome.stateRoot != expectedRoot)
        {
            auto diff = verifyPostState(*outcome.view, post.state);
            if (g_opts.verbose)
            {
                // Which accounts differ: the fixture's `state` hashed on its own tells whether
                // it is the complete world state (then EXTRA/MISSING below is authoritative).
                auto const stateRoot = rootOfAccounts(post.state);
                diff += "    fixture.state root " + stateRoot.hex() +
                        (stateRoot == expectedRoot ? " (complete)" : " (partial)") + "\n" +
                        diffAccounts(*outcome.view, post.state);
            }
            return fail("state root mismatch: expected " + expectedRoot.hex() + ", got " +
                        outcome.stateRoot.hex() + (diff.empty() ? "" : "\n" + diff));
        }
        return true;
    }

    /// blockchain_tests_engine: the same chain, driven as a CL would — engine_newPayload for
    /// every payload (expected VALID, INVALID or a JSON-RPC error as the fixture says) and a
    /// forkchoiceUpdated to each accepted head — through the in-process EthEngineService over
    /// the production verifier. The head must be lastblockhash and the committed state must
    /// carry postState.
    bool runEngineFixture(test::EESTBlockchainFixture const& fixture)
    {
        using namespace bcos::test::pipeline;
        auto network =
            fixture.network.empty() ? extractForkFromName(fixture.name) : fixture.network;
        auto fail = [&](std::string reason) {
            printLine("FAIL: " + fixture.name + " [" + network + "] " + reason);
            g_failureDetails.push_back({fixture.name, network, std::move(reason)});
            ++g_unexpectedFailure;
            return false;
        };
        FixtureSchedule schedule;
        try
        {
            schedule = scheduleForNetwork(network);
        }
        catch (std::exception const& e)
        {
            return fail(e.what());
        }
        if (!schedule.unsupported.empty())
        {
            return fail("fork schedule cannot express " + schedule.unsupported.front());
        }
        auto const chainId = static_cast<uint64_t>(fixture.chainId);

        protocol::EthBlockHeaderData genesis;
        try
        {
            genesis = headerFromJson(fixture.genesisHeaderJson);
        }
        catch (std::exception const& e)
        {
            return fail(std::string("genesisBlockHeader unreadable: ") + e.what());
        }
        if (!fixture.genesisBlockHeader.hash.empty() &&
            protocol::ethHeaderHash(genesis) != toH256(fixture.genesisBlockHeader.hash))
        {
            return fail("genesis header re-encodes to " + protocol::ethHeaderHash(genesis).hex() +
                        ", fixture says " + fixture.genesisBlockHeader.hash);
        }
        Chain chain;
        h256 root;
        try
        {
            root = chain.seedGenesis(fixture.pre, genesis, schedule.forks);
        }
        catch (std::exception const& e)
        {
            return fail(std::string("pre-state import failed: ") + e.what());
        }
        if (root != genesis.stateRoot)
        {
            return fail("genesis state root mismatch: trie " + root.hex() + ", header " +
                        genesis.stateRoot.hex());
        }

        EngineHarness engine(chain, schedule, chainId);
        h256 head = protocol::ethHeaderHash(genesis);
        for (std::size_t i = 0; i < fixture.enginePayloads.size(); ++i)
        {
            auto const& payload = fixture.enginePayloads[i];
            std::optional<engine::PayloadStatus> status;
            std::string thrown;
            try
            {
                status = engine.newPayload(payload.params, payload.newPayloadVersion);
            }
            catch (std::exception const& e)
            {
                thrown = e.what();
            }
            auto const index = std::to_string(i);
            if (payload.errorCode.has_value())
            {
                if (!thrown.empty())
                {
                    ++g_expectedException;
                    continue;
                }
                return fail("payload " + index + " expected JSON-RPC error " +
                            std::to_string(*payload.errorCode) + ", got status " +
                            std::to_string(static_cast<int>(status->status)));
            }
            if (!payload.validationError.empty())
            {
                if (!thrown.empty())
                {
                    return fail("payload " + index + " threw instead of INVALID (" +
                                payload.validationError + "): " + thrown);
                }
                if (status->status != engine::PayloadValidationStatus::Invalid &&
                    status->status != engine::PayloadValidationStatus::InvalidBlockHash)
                {
                    return fail("payload " + index + " expected INVALID (" +
                                payload.validationError + "), got status " +
                                std::to_string(static_cast<int>(status->status)));
                }
                ++g_expectedException;
                continue;
            }
            if (!thrown.empty())
            {
                return fail("payload " + index + " threw: " + thrown);
            }
            if (status->status != engine::PayloadValidationStatus::Valid)
            {
                return fail("payload " + index + " status " +
                            std::to_string(static_cast<int>(status->status)) + ": " +
                            status->validationError.value_or(""));
            }
            auto const hash = engine.blockHashOf(payload.params, payload.newPayloadVersion);
            engine::ForkchoiceUpdatedResult fcu;
            try
            {
                fcu = engine.forkchoice(hash, payload.forkchoiceUpdatedVersion);
            }
            catch (std::exception const& e)
            {
                return fail("forkchoiceUpdated after payload " + index + " threw: " + e.what());
            }
            if (fcu.payloadStatus.status != engine::PayloadValidationStatus::Valid)
            {
                return fail("forkchoiceUpdated after payload " + index + " status " +
                            std::to_string(static_cast<int>(fcu.payloadStatus.status)) + ": " +
                            fcu.payloadStatus.validationError.value_or(""));
            }
            head = hash;
        }

        if (!fixture.lastBlockHash.empty() && head != toH256(fixture.lastBlockHash))
        {
            return fail("head is " + head.hex() + ", fixture expects " + fixture.lastBlockHash);
        }
        auto view = chain.committedView();
        if (auto diff = verifyPostState(view, fixture.postState); !diff.empty())
        {
            return fail("post-state mismatch\n" + diff);
        }
        return true;
    }

    /// blockchain_tests: genesis from genesisRLP + `pre`, then every block's RLP through
    /// verifyAndCommit. A block with expectException must be refused (or fail to decode);
    /// every other block must commit. The chain head must be lastblockhash and the
    /// committed state must carry postState.
    bool runBlockchainFixture(test::EESTBlockchainFixture const& fixture)
    {
        using namespace bcos::test::pipeline;
        auto network =
            fixture.network.empty() ? extractForkFromName(fixture.name) : fixture.network;
        auto fail = [&](std::string reason) {
            printLine("FAIL: " + fixture.name + " [" + network + "] " + reason);
            g_failureDetails.push_back({fixture.name, network, std::move(reason)});
            ++g_unexpectedFailure;
            return false;
        };
        FixtureSchedule schedule;
        try
        {
            schedule = scheduleForNetwork(network);
        }
        catch (std::exception const& e)
        {
            return fail(e.what());
        }
        if (!schedule.unsupported.empty())
        {
            return fail("fork schedule cannot express " + schedule.unsupported.front());
        }
        auto const chainId = static_cast<uint64_t>(fixture.chainId);

        protocol::EthBlock genesis;
        try
        {
            auto rlp = test::hexToBytes(fixture.genesisRLP);
            genesis.rlpDecode(bytesConstRef(rlp.data(), rlp.size()));
        }
        catch (std::exception const& e)
        {
            return fail(std::string("genesisRLP undecodable: ") + e.what());
        }
        Chain chain;
        h256 root;
        try
        {
            root = chain.seedGenesis(fixture.pre, genesis.data().header, schedule.forks);
        }
        catch (std::exception const& e)
        {
            return fail(std::string("pre-state import failed: ") + e.what());
        }
        if (root != genesis.data().header.stateRoot)
        {
            return fail("genesis state root mismatch: trie " + root.hex() + ", header " +
                        genesis.data().header.stateRoot.hex());
        }

        for (std::size_t i = 0; i < fixture.blocks.size(); ++i)
        {
            auto const& block = fixture.blocks[i];
            bool const expectInvalid = !block.expectException.empty();
            protocol::EthBlock decoded;
            try
            {
                auto rlp = test::hexToBytes(block.rlp);
                decoded.rlpDecode(bytesConstRef(rlp.data(), rlp.size()));
            }
            catch (std::exception const& e)
            {
                if (expectInvalid)
                {
                    ++g_expectedException;
                    continue;
                }
                return fail("block " + std::to_string(i) + " RLP undecodable: " + e.what());
            }
            auto outcome = chain.applyBlock(decoded.data(), schedule, chainId);
            if (expectInvalid)
            {
                if (outcome.valid)
                {
                    return fail("block " + std::to_string(i) + " accepted, fixture expects " +
                                block.expectException);
                }
                ++g_expectedException;
                continue;
            }
            if (!outcome.valid)
            {
                return fail("block " + std::to_string(i) + " refused: " + outcome.error);
            }
        }

        if (!fixture.lastBlockHash.empty())
        {
            auto const head = protocol::ethHeaderHash(chain.parent());
            if (head != toH256(fixture.lastBlockHash))
            {
                return fail("head is " + head.hex() + ", fixture expects " + fixture.lastBlockHash);
            }
        }
        auto view = chain.committedView();
        if (auto diff = verifyPostState(view, fixture.postState); !diff.empty())
        {
            return fail("post-state mismatch\n" + diff);
        }
        return true;
    }
};

// =============================================================================
//  Detect fixture format from directory path
// =============================================================================
/// Determine fixture format from the file path.
/// EEST fixtures are organized by type in directory names:
///   .../state_tests/...       → state_test
///   .../blockchain_tests/...  → blockchain_test
///   .../transaction_tests/... → transaction_test (skip)
inline std::string detectFormatFromPath(fs::path const& filePath)
{
    auto str = filePath.string();
    // Match top-level fixture type directory by full path.
    // Order: engine_x before engine before blockchain_tests to avoid
    // substring match issues. blockchain_tests before state_tests so
    // that blockchain_tests/static/state_tests/ files are correctly
    // classified as blockchain_tests.
    if (str.find("/blockchain_tests_engine_x/") != std::string::npos)
    {
        return "blockchain_engine_x_test";
    }
    if (str.find("/blockchain_tests_engine/") != std::string::npos)
    {
        return "blockchain_engine_test";
    }
    if (str.find("/blockchain_tests_sync/") != std::string::npos)
    {
        return "blockchain_sync_test";
    }
    if (str.find("/blockchain_tests/") != std::string::npos)
    {
        return "blockchain_test";
    }
    if (str.find("/state_tests/") != std::string::npos)
    {
        return "state_test";
    }
    if (str.find("/transaction_tests/") != std::string::npos)
    {
        return "transaction_test";
    }
    // Unknown — default to state_test
    return "state_test";
}

// =============================================================================
//  Process a single JSON fixture file (supports both state_test & blockchain_test)
// =============================================================================
struct FileResult
{
    std::string path;
    int passed = 0;
    int failed = 0;
};

FileResult processFixtureFile(EESTRunner& runner, fs::path const& filePath)
{
    FileResult result;
    result.path = filePath.string();

    if (!g_opts.quiet)
    {
        printLine("  Loading: " + filePath.string());
    }

    auto format = detectFormatFromPath(filePath);

    // --- blockchain_test ---

    // --- transaction_test / unsupported (skip) ---
    if (format == "blockchain_test" || format == "blockchain_engine_test")
    {
        std::vector<test::EESTBlockchainFixture> bcFixtures;
        try
        {
            bcFixtures = test::loadEESTBlockchainFixtures(filePath.string());
        }
        catch (std::exception const& e)
        {
            std::cerr << "Warning: failed to load fixture file " << filePath.string() << ": "
                      << e.what() << "\n";
            ++g_loadFailures;
            countOutcome(format, false);
            ++result.failed;
            return result;
        }
        for (auto const& fixture : bcFixtures)
        {
            bool passed = format == "blockchain_engine_test" ? runner.runEngineFixture(fixture) :
                                                               runner.runBlockchainFixture(fixture);
            countOutcome(format, passed);
            (passed ? result.passed : result.failed)++;
        }
        return result;
    }
    if (format == "transaction_test" || format == "blockchain_engine_x_test" ||
        format == "blockchain_sync_test")
    {
        // Not executed by this runner (yet): every fixture in the file counts as a failure,
        // named, so the summary cannot read 100% while a class is unrun.
        auto const entries = countFixtureEntries(filePath);
        countOutcome(format, false, entries);
        result.failed += entries;
        g_failureDetails.push_back({filePath.filename().string(), format,
            "fixture format not executed by this runner (" + std::to_string(entries) + ")"});
        return result;
    }

    // --- state_test (default) ---
    std::vector<test::EESTFixture> fixtures;
    try
    {
        fixtures = test::loadEESTFixtures(filePath.string());
    }
    catch (std::exception const& e)
    {
        std::cerr << "Warning: failed to load fixture file " << filePath.string() << ": "
                  << e.what() << "\n";
        ++g_loadFailures;
        countOutcome(format, false);
        ++result.failed;
        return result;
    }
    for (auto const& fixture : fixtures)
    {
        for (auto const& [forkName, posts] : fixture.post)
        {
            for (auto const& post : posts)
            {
                bool passed = runner.runFixture(fixture, forkName, post);
                countOutcome(format, passed);
                (passed ? result.passed : result.failed)++;
            }
        }
    }
    return result;
}

/// One line of the --known-failures file: a regex over "<fixture name> [<fork>]".
struct KnownFailure
{
    std::string text;
    std::regex pattern;
    int hits = 0;
};

/// Parses the --known-failures file ('#' starts a comment, blank lines skipped). nullopt and a
/// message on stderr when the file cannot be read or a line is not a valid regex; an empty
/// path yields an empty list.
std::optional<std::vector<KnownFailure>> loadKnownFailures(std::string const& path)
{
    std::vector<KnownFailure> known;
    if (path.empty())
    {
        return known;
    }
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Error: cannot open --known-failures file " << path << '\n';
        return std::nullopt;
    }
    std::string line;
    while (std::getline(file, line))
    {
        auto const hash = line.find('#');
        if (hash != std::string::npos)
        {
            line.erase(hash);
        }
        boost::algorithm::trim(line);
        if (line.empty())
        {
            continue;
        }
        try
        {
            known.push_back(KnownFailure{.text = line, .pattern = std::regex(line)});
        }
        catch (std::regex_error const& e)
        {
            std::cerr << "Error: bad regex in " << path << ": " << line << " (" << e.what()
                      << ")\n";
            return std::nullopt;
        }
    }
    return known;
}

// =============================================================================
//  Print a progress bar
// =============================================================================
void printProgress(int done, int total, const char* unit = "files")
{
    if (total == 0)
    {
        return;
    }
    int barWidth = 40;
    float ratio = static_cast<float>(done) / total;
    int filled = static_cast<int>(barWidth * ratio);
    std::ostringstream oss;
    oss << "\r[" << std::string(filled, '=') << std::string(barWidth - filled, ' ') << "] " << done
        << "/" << total << " " << unit;
    std::cerr << oss.str() << std::flush;
}

// =============================================================================
//  Main
// =============================================================================
int main(int argc, char* argv[])
{
    parseOptions(argc, argv);

    if (g_opts.help)
    {
        printUsage(argv[0]);
        return 0;
    }

    // Suppress BCOS internal Boost.Log output if --no-log is set
    if (g_opts.noLog)
    {
        boost::log::core::get()->set_logging_enabled(false);
    }

    // Resolve fixture directory
    std::string fixtureDir = g_opts.fixtureDir;
    if (fixtureDir.empty() && !g_opts.singleFixture.empty())
    {
        // --fixture mode: use containing directory for display
        fixtureDir = fs::path(g_opts.singleFixture).parent_path().string();
    }
    if (fixtureDir.empty())
    {
        char const* envDir = std::getenv("EEST_FIXTURE_DIR");
        if (envDir)
        {
            fixtureDir = envDir;
        }
    }

    // Collect file list
    std::vector<fs::path> fileList;

    if (!g_opts.singleFixture.empty())
    {
        fs::path p(g_opts.singleFixture);
        if (!fs::exists(p) || !fs::is_regular_file(p))
        {
            std::cerr << "Error: fixture file not found: " << p << '\n';
            return 1;
        }
        fileList.push_back(p);
        std::cout << "[EEST Runner] Single fixture mode: " << p << '\n';
    }
    else if (!fixtureDir.empty())
    {
        if (!fs::exists(fixtureDir) || !fs::is_directory(fixtureDir))
        {
            std::cerr << "Error: fixture directory not found: " << fixtureDir << '\n';
            return 1;
        }
        for (auto const& entry : fs::recursive_directory_iterator(fixtureDir))
        {
            if (!entry.is_regular_file() || entry.path().extension().string() != ".json")
            {
                continue;
            }
            // blockchain_tests_engine_x/pre_alloc/<hash>.json are shared pre-state groups the
            // engine_x fixtures reference by preHash, not fixtures: they are not counted.
            if (entry.path().string().find("/pre_alloc/") != std::string::npos)
            {
                continue;
            }
            fileList.push_back(entry.path());
        }
        std::cout << "[EEST Runner] Found " << fileList.size()
                  << " fixture files in: " << fixtureDir << '\n';
    }
    else
    {
        std::cerr << "Error: no fixture directory specified.\n"
                  << "  Use --fixture-dir DIR, --fixture FILE, or set "
                     "EEST_FIXTURE_DIR environment variable.\n";
        return 1;
    }

    if (fileList.empty())
    {
        std::cerr << "No fixture files found." << '\n';
        return 0;
    }

    // Sort for deterministic output
    std::sort(fileList.begin(), fileList.end());

    // Print directory tree (compact)
    if (!g_opts.quiet && fileList.size() > 1)
    {
        std::cout << "\n--- Fixture tree ---" << '\n';
        fs::path baseDir = fs::absolute(fixtureDir);
        std::map<std::string, int> dirCounts;
        for (auto const& p : fileList)
        {
            auto rel = fs::relative(p, baseDir);
            auto parent = rel.parent_path();
            if (parent.empty())
            {
                dirCounts["(root)"]++;
            }
            else
            {
                auto it = parent.begin();
                std::string topDir = it->string();
                dirCounts[topDir]++;
            }
        }
        for (auto const& [dir, count] : dirCounts)
        {
            std::cout << "  " << dir << "/  (" << count << " files)" << '\n';
        }
        std::cout << "---------------------\n";
    }

    auto startTime = std::chrono::steady_clock::now();

    // The known-failures file is read before the sweep: a bad regex fails in seconds, not
    // after the whole release has run.
    auto knownFailures = loadKnownFailures(g_opts.knownFailures);
    if (!knownFailures)
    {
        return 2;
    }

    // One fixture file per task: every fixture builds its own in-memory chain, so files
    // share nothing but the counters (atomic) and the console (g_printMutex).
    auto const jobs =
        g_opts.jobs != 0 ? g_opts.jobs : std::max(1U, std::thread::hardware_concurrency());
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, jobs);
    // Fixtures recurse the EVM to its 1024-frame call depth; a TBB worker's default stack is
    // far smaller than the main thread's and overflows (SIGBUS on macOS). 256 MiB is virtual.
    tbb::global_control workerStack(tbb::global_control::thread_stack_size, std::size_t{256} << 20);
    EESTRunner runner;
    std::atomic<std::size_t> done{0};
    fs::path const base = fs::absolute(fixtureDir);
    tbb::parallel_for(std::size_t{0}, fileList.size(), [&](std::size_t i) {
        auto const& p = fileList[i];
        auto fr = processFixtureFile(runner, p);
        if (!g_opts.quiet)
        {
            auto rel = fs::relative(p, base);
            int total = fr.passed + fr.failed;
            printLine("  " + rel.string() + " ... " +
                      (fr.failed == 0 ?
                              "OK (" + std::to_string(total) + " tests)" :
                              std::to_string(fr.failed) + "/" + std::to_string(total) + " FAILED"));
        }
        auto const finished = ++done;
        if (jobs == 1 || finished % 50 == 0 || finished == fileList.size())
        {
            std::lock_guard<std::mutex> lock(g_printMutex);
            printProgress(static_cast<int>(finished), static_cast<int>(fileList.size()));
        }
    });

    // Clear progress line
    if (!g_opts.quiet)
    {
        std::cerr << "\r" << std::string(60, ' ') << "\r";
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

    // =========================================================================
    //  Final Summary
    // =========================================================================
    int totalFiles = static_cast<int>(fileList.size());

    std::cout << "\n========== EEST Fixture Test Summary ==========\n";
    std::cout << "  Total tests:   " << g_totalTests.load() << "\n";
    std::cout << "  Passed:        " << g_totalPassed.load() << "\n";
    std::cout << "  Failed:        " << g_totalFailed.load() << "\n";
    std::cout << "  Pass rate:     "
              << (g_totalTests > 0 ? std::to_string(100 * g_totalPassed / g_totalTests) + "%" :
                                     "N/A")
              << "\n";
    std::cout << "  Files:         " << totalFiles << "\n";
    std::cout << "  Load failures: " << g_loadFailures.load() << "\n";
    std::cout << "  Jobs:          " << jobs << "\n";
    std::cout << "  Elapsed:       " << elapsed << "s\n";

    // One line per class, parseable: "Class <name>: <passed>/<total> passed, rate <n>%".
    // A class with no fixtures under the given directory is omitted.
    std::cout << "\n  By class:\n";
    for (std::size_t i = 0; i < c_fixtureClasses.size(); ++i)
    {
        auto const& counters = g_classCounters[i];
        if (counters.total == 0)
        {
            continue;
        }
        std::cout << "    Class " << c_fixtureClasses[i] << ": " << counters.passed.load() << "/"
                  << counters.total.load() << " passed, rate "
                  << (100 * counters.passed.load() / counters.total.load()) << "%\n";
    }

    std::cout << "\n  Error breakdown:\n";
    std::cout << "    Expected exception: " << g_expectedException.load() << "\n";
    std::cout << "    Unexpected failure: " << g_unexpectedFailure.load() << "\n";

    // Feature-level summary
    std::cout << "\n  By feature:\n";
    std::map<std::string, int> featureCounts;
    for (auto const& fd : g_failureDetails)
    {
        std::string name = fd.testName;
        auto pos1 = name.find('/');
        if (pos1 != std::string::npos)
        {
            auto pos2 = name.find('/', pos1 + 1);
            if (pos2 != std::string::npos)
            {
                auto pos3 = name.find('/', pos2 + 1);
                std::string feature = (pos3 != std::string::npos) ?
                                          name.substr(pos1 + 1, pos3 - pos1 - 1) :
                                          name.substr(pos1 + 1);
                featureCounts[feature]++;
            }
        }
    }
    for (auto const& [feat, count] : featureCounts)
    {
        std::cout << "    " << feat << ": " << count << "\n";
    }

    // Fork-level summary
    std::cout << "\n  By fork:\n";
    std::map<std::string, int> forkCounts;
    for (auto const& fd : g_failureDetails)
    {
        forkCounts[fd.forkName]++;
    }
    for (auto const& [fork, count] : forkCounts)
    {
        std::cout << "    " << fork << ": " << count << "\n";
    }

    // Known-failure triage: each failure is matched against the file's regexes over
    // "<name> [<fork>]" (the FAIL line's own label). The gate is on the rest.
    int knownFailureCount = 0;
    std::vector<std::string> unexpectedFailures;
    if (!g_opts.knownFailures.empty())
    {
        auto& known = *knownFailures;
        for (auto const& fd : g_failureDetails)
        {
            auto const label = fd.testName + " [" + fd.forkName + "]";
            bool matched = false;
            for (auto& entry : known)
            {
                if (std::regex_search(label, entry.pattern))
                {
                    ++entry.hits;
                    matched = true;
                    break;
                }
            }
            if (matched)
            {
                ++knownFailureCount;
            }
            else
            {
                unexpectedFailures.push_back(label);
            }
        }
        std::vector<std::string> stale;
        for (auto const& entry : known)
        {
            if (entry.hits == 0)
            {
                stale.push_back(entry.text);
            }
        }
        std::cout << "\n  Known failures:      " << knownFailureCount << " (" << known.size()
                  << " entries in " << g_opts.knownFailures << ")\n";
        std::cout << "  Unexpected failures: " << unexpectedFailures.size() << "\n";
        std::cout << "  Stale entries:       " << stale.size() << "\n";
        for (auto const& label : unexpectedFailures)
        {
            std::cout << "    UNEXPECTED " << label << "\n";
        }
        for (auto const& text : stale)
        {
            std::cout << "    STALE " << text << "\n";
        }
        std::cout << "=================================================\n";
        return (unexpectedFailures.empty() && stale.empty()) ? 0 : 1;
    }
    std::cout << "=================================================\n";

    return g_totalFailed > 0 ? 1 : 0;
}
