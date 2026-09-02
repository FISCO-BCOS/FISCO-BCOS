/**
 * @file EESTRunner.cpp
 * @brief Standalone Ethereum Execution Spec Tests (EEST) runner.
 *
 * A dedicated command-line tool for running EEST v5.4.0 JSON state test fixtures
 * against ethereum-executor (based on bcos-evm).  Provides detailed per-file and per-test
 * diagnostic output, configurable verbosity, single-fixture regression mode,
 * and TBB-based parallel execution.
 *
 * Usage:
 *   ./eest-runner --fixture-dir /path/to/fixtures           # run all
 *   ./eest-runner --fixture-dir /path/to/fixtures --quiet   # summary only
 *   ./eest-runner --fixture /path/to/single.json            # single file
 *   ./eest-runner --fixture-dir /path --jobs 8              # parallel
 */

#include "EESTRunner.h"
#include "TestMemoryStorage.h"
#include "TestStorageBridge.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/state/block.hpp"
#include "bcos-evm/eth/state/system_contracts.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/TBBWait.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <evmc/evmc.h>
#include <tbb/concurrent_vector.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/core.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <range/v3/algorithm/equal.hpp>
#include <sstream>
#include <string>
#include <vector>

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
            block.blockHeader = parseBlockHeader(blockJson["blockHeader"]);

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
    bool noLog = false;         // suppress BCOS internal trace/debug log
    bool help = false;
};

ProgramOptions g_opts;

// =============================================================================
//  Global counters (atomic for parallel safety)
// =============================================================================
std::atomic<int> g_totalTests{0};
std::atomic<int> g_totalPassed{0};
std::atomic<int> g_totalFailed{0};

// Per-error-type counters
std::atomic<int> g_balanceMismatch{0};
std::atomic<int> g_storageMismatch{0};
std::atomic<int> g_nonceMismatch{0};
std::atomic<int> g_codeMismatch{0};
std::atomic<int> g_expectedException{0};
std::atomic<int> g_unexpectedFailure{0};

// Files that were intentionally skipped (engine-format duplicates /
// transaction_tests) or failed to load/parse — kept so the summary's 100%
// carries its domain and a loader regression can't silently raise the pass rate.
std::atomic<int> g_skippedFiles{0};
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

// =============================================================================
//  Helper: timestamp for elapsed-time display
// =============================================================================
std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&nowTime), "%H:%M:%S");
    return oss.str();
}

void printLine(std::string const& msg)
{
    std::lock_guard<std::mutex> lock(g_printMutex);
    std::cout << msg << '\n';
}

// =============================================================================
//  BlockHashes that accumulates hashes from fixture blocks
// =============================================================================
/// A BlockHashes implementation that stores block_number → hash mappings.
/// Populated from fixture block headers as blocks are processed.
struct FixtureBlockHashes : evmone::state::BlockHashes
{
    std::map<int64_t, evmc::bytes32> hashes;

    evmc::bytes32 get_block_hash(int64_t blockNumber) const noexcept override
    {
        auto it = hashes.find(blockNumber);
        if (it != hashes.end())
        {
            return it->second;
        }
        return {};
    }
};

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
  --no-log             Suppress BCOS internal trace/debug log output
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
        else if (arg == "--no-log")
        {
            g_opts.noLog = true;
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
//  Core test runner (reused from original but made standalone)
// =============================================================================
/// Fork transition info parsed from fork names like "CancunToPragueAtTime15k".
struct ForkTransition
{
    evmc_revision fromRev = EVMC_CANCUN;
    evmc_revision toRev = EVMC_CANCUN;
    int64_t transitionTimestamp = 0;
};

class EESTRunner
{
public:
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite;
    bcostars::protocol::TransactionFactoryImpl transactionFactory;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    // BlockHashes provider injected into the executor's constructor. The
    // block-test path clears and repopulates it per fixture run.
    std::shared_ptr<FixtureBlockHashes> m_blockHashes = std::make_shared<FixtureBlockHashes>();
    eth::EthereumExecutor executor;
    evmc_revision m_currentRevision = EVMC_CANCUN;
    ledger::LedgerConfig m_ledgerConfig;
    std::optional<ForkTransition> m_forkTransition;
    int64_t m_chainId = 1;  // EIP-155 chain id from the fixture config

    EESTRunner()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        transactionFactory(cryptoSuite),
        receiptFactory(cryptoSuite),
        executor(receiptFactory,
            [blockHashes = m_blockHashes](int64_t blockNumber, int64_t /*currentHeight*/)
                -> evmc::bytes32 { return blockHashes->get_block_hash(blockNumber); })
    {}

    void configureFork(std::string const& forkName)
    {
        auto const rev = test::forkNameToRevision(forkName);
        m_currentRevision = rev;
        m_ledgerConfig.setEVMCRevision(rev);
        m_forkTransition.reset();

        // Detect fork transition names like "CancunToPragueAtTime15k"
        auto toPos = forkName.find("To");
        auto atPos = forkName.find("AtTime");
        if (toPos != std::string::npos && atPos != std::string::npos && toPos < atPos)
        {
            auto fromFork = forkName.substr(0, toPos);
            auto toFork = forkName.substr(toPos + 2, atPos - toPos - 2);
            auto timeStr = forkName.substr(atPos + 6);  // "AtTime" = 6 chars
            try
            {
                // Handle 'k' suffix meaning ×1000 (e.g., "15k" = 15000)
                int64_t transitionTime = 0;
                if (!timeStr.empty() && timeStr.back() == 'k')
                {
                    transitionTime = std::stoll(timeStr.substr(0, timeStr.size() - 1)) * 1000;
                }
                else
                {
                    transitionTime = std::stoll(timeStr);
                }
                m_forkTransition = ForkTransition{test::forkNameToRevision(fromFork),
                    test::forkNameToRevision(toFork), transitionTime};
            }
            catch (...)
            {
                m_forkTransition.reset();
            }
        }
    }

    /// Get the EVMC revision for a block given its timestamp (handles fork transitions).
    evmc_revision revisionForBlock(int64_t blockTimestampSec) const
    {
        if (!m_forkTransition.has_value())
        {
            return m_currentRevision;
        }
        return (blockTimestampSec >= m_forkTransition->transitionTimestamp) ?
                   m_forkTransition->toRev :
                   m_forkTransition->fromRev;
    }

    void configureEnvironment(test::EESTEnvironment const& env)
    {
        if (!env.baseFee.empty())
        {
            auto baseFeeVal = test::hexToU256(env.baseFee);
            auto hexStr = "0x" + baseFeeVal.str(0, std::ios_base::hex);
            m_ledgerConfig.setGasPrice({hexStr, 0});
        }
        else
        {
            m_ledgerConfig.setGasPrice({"0x0", 0});
        }
        // Set gas limit from env (fixes GASLIMIT opcode returning the BCOS
        // default instead of the fixture's currentGasLimit).
        if (!env.gasLimit.empty())
        {
            auto gasLimit = static_cast<uint64_t>(test::hexToU256(env.gasLimit));
            m_ledgerConfig.setGasLimit({gasLimit, 0});
        }
        // EIP-4399: PREVRANDAO (Paris+). State-test fixtures carry it in
        // "currentRandom" (the block mixHash).
        if (!env.random.empty())
        {
            auto mixHashBytes = test::hexToBytes(env.random);
            evmc::bytes32 prevRandao{};
            if (mixHashBytes.size() == sizeof(evmc_bytes32))
            {
                std::copy_n(mixHashBytes.begin(), sizeof(evmc_bytes32), prevRandao.bytes);
            }
            else if (mixHashBytes.size() < sizeof(evmc_bytes32))
            {
                std::copy_n(mixHashBytes.begin(), mixHashBytes.size(),
                    prevRandao.bytes + sizeof(evmc_bytes32) - mixHashBytes.size());
            }
            m_ledgerConfig.setPrevRandao(prevRandao);
        }
        else
        {
            m_ledgerConfig.setPrevRandao({});
        }
        // EIP-4844 blob gas parameters (Cancun+).
        if (!env.excessBlobGas.empty())
        {
            m_ledgerConfig.setExcessBlobGas(
                static_cast<uint64_t>(test::hexToU256(env.excessBlobGas)));
        }
        else
        {
            m_ledgerConfig.setExcessBlobGas(std::nullopt);
        }
        if (!env.blobGasUsed.empty())
        {
            m_ledgerConfig.setBlobGasUsed(static_cast<uint64_t>(test::hexToU256(env.blobGasUsed)));
        }
        else
        {
            m_ledgerConfig.setBlobGasUsed(std::nullopt);
        }
    }

    void setupPreState(MutableStorage& storage, std::map<std::string, test::EESTAccount> const& pre)
    {
        for (auto const& [addrHex, acc] : pre)
        {
            auto addrBytes = test::hexToBytes(addrHex);
            if (addrBytes.size() != sizeof(evmc_address))
            {
                continue;
            }

            evmc_address addr;
            std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);

            ledger::account::EVMAccount<MutableStorage> evmAccount(storage, addr, false);

            task::tbb::syncWait([&]() -> task::Task<void> {
                if (!co_await evmAccount.exists())
                {
                    co_await evmAccount.create();
                }
            }());

            if (!acc.nonce.empty() && acc.nonce != "0x" && acc.nonce != "0x0")
            {
                auto nonceStr = test::hexToU256(acc.nonce).str(0, std::ios_base::dec);
                task::tbb::syncWait(
                    [&]() -> task::Task<void> { co_await evmAccount.setNonce(nonceStr); }());
            }

            if (!acc.balance.empty() && acc.balance != "0x0")
            {
                auto bal = test::hexToU256(acc.balance);
                if (bal > 0)
                {
                    task::tbb::syncWait(
                        [&]() -> task::Task<void> { co_await evmAccount.setBalance(bal); }());
                }
            }

            if (!acc.code.empty() && acc.code != "0x")
            {
                auto codeBytes = test::hexToBytes(acc.code);
                if (!codeBytes.empty())
                {
                    auto codeHash = cryptoSuite->hashImpl()->hash(
                        bytesConstRef(codeBytes.data(), codeBytes.size()));
                    task::tbb::syncWait([&]() -> task::Task<void> {
                        co_await evmAccount.setCode(std::move(codeBytes), std::string{}, codeHash);
                    }());
                }
            }

            for (auto const& [key, val] : acc.storage)
            {
                auto keyBytes = test::hexToBytes(key);
                auto valBytes = test::hexToBytes(val);
                if (keyBytes.size() <= 32 && valBytes.size() <= 32)
                {
                    evmc_bytes32 storageKey{};
                    evmc_bytes32 storageValue{};
                    std::copy(
                        keyBytes.begin(), keyBytes.end(), storageKey.bytes + 32 - keyBytes.size());
                    std::copy(valBytes.begin(), valBytes.end(),
                        storageValue.bytes + 32 - valBytes.size());

                    task::tbb::syncWait([&]() -> task::Task<void> {
                        co_await evmAccount.setStorage(storageKey, storageValue);
                    }());
                }
            }
        }
    }

    bcostars::protocol::BlockHeaderImpl buildBlockHeader(test::EESTEnvironment const& env)
    {
        bcostars::protocol::BlockHeaderImpl header;
        uint32_t blockVer;
        if (m_currentRevision >= EVMC_CANCUN)
        {
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        }
        else if (m_currentRevision >= EVMC_PARIS)
        {
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION);
        }
        else
        {
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        }
        header.setVersion(blockVer);
        header.setNumber(test::hexToInt64(env.number));

        auto tsSec = test::hexToTimestamp(env.timestamp);
        header.setTimestamp(tsSec * 1000);
        header.setSealer(0);

        if (!env.coinbase.empty())
        {
            auto cbBytes = test::hexToBytes(env.coinbase);
            bcos::Address coinbaseAddr;
            if (cbBytes.size() >= bcos::Address::SIZE)
            {
                std::memcpy(coinbaseAddr.data(), cbBytes.data(), bcos::Address::SIZE);
            }
            header.setCoinbase(coinbaseAddr);
        }

        // EIP-4399 (Paris+): opcode 0x44 is PREVRANDAO, returning the block
        // mixHash. State-test fixtures carry it in env.currentRandom. The
        // executor resolves PREVRANDAO from the block header (blockHeaderToBlockInfo
        // reads header.prevRandao()), NOT from the ledger config, so the value
        // MUST be placed on the header here. Without this, Cancun/Prague
        // fixtures whose contract stores PREVRANDAO (static stRandom /
        // mergeTest / blockInfo) observe 0x0 — and if that 0 is SSTORE'd into
        // an already-zero slot, evmone charges only the no-op SSTORE cost (100)
        // instead of SSTORE_SET (20000), under-charging gas by exactly 19900
        // and corrupting the coinbase/sender balances too.
        if (!env.random.empty())
        {
            auto mixHashBytes = test::hexToBytes(env.random);
            if (!mixHashBytes.empty())
            {
                // bcos::h256 from raw bytes (right-aligned, big-endian) so a
                // short currentRandom is left-zero-padded to 32 bytes.
                header.setPrevRandao(bcos::h256(mixHashBytes));
            }
            else
            {
                header.setPrevRandao(bcos::h256{});
            }
        }
        else
        {
            header.setPrevRandao(bcos::h256{});
        }
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    std::shared_ptr<bcostars::protocol::TransactionImpl> buildTransaction(
        test::EESTTransaction const& tx, int dataIndex, int gasIndex, int valueIndex)
    {
        std::string txData;
        if (dataIndex >= 0 && static_cast<size_t>(dataIndex) < tx.data.size())
        {
            txData = tx.data[dataIndex];
        }

        std::string gasLimit = "0x0";
        if (gasIndex >= 0 && static_cast<size_t>(gasIndex) < tx.gasLimit.size())
        {
            gasLimit = tx.gasLimit[gasIndex];
        }

        std::string value = "0x0";
        if (valueIndex >= 0 && static_cast<size_t>(valueIndex) < tx.value.size())
        {
            value = tx.value[valueIndex];
        }

        auto tarsTx = std::make_shared<bcostars::Transaction>();
        auto& data = tarsTx->data;

        data.version = 0;
        data.blockLimit = 0;
        data.nonce = test::strip0x(tx.nonce);
        data.gasLimit = test::hexToInt64(gasLimit);
        data.value = test::strip0x(value);

        if (!tx.to.empty() && tx.to != "0x")
        {
            auto toBytes = test::hexToBytes(tx.to);
            data.to.assign(toBytes.begin(), toBytes.end());
        }

        auto inputBytes = test::hexToBytes(txData);
        data.input.assign(inputBytes.begin(), inputBytes.end());

        if (!tx.gasPrice.empty())
        {
            data.gasPrice = test::strip0x(tx.gasPrice);
        }
        if (!tx.maxFeePerGas.empty())
        {
            data.maxFeePerGas = test::strip0x(tx.maxFeePerGas);
        }
        if (!tx.maxPriorityFeePerGas.empty())
        {
            data.maxPriorityFeePerGas = test::strip0x(tx.maxPriorityFeePerGas);
        }

        if (!tx.sender.empty())
        {
            auto senderBytes = test::hexToBytes(tx.sender);
            tarsTx->sender.assign(senderBytes.begin(), senderBytes.end());
        }
        data.chainID = tx.chainId.empty() ? std::to_string(m_chainId) : test::strip0x(tx.chainId);
        tarsTx->type = 1;
        tarsTx->extraTransactionHash.assign(32, 0);

        bool hasAccessList = false;
        if (dataIndex >= 0 && static_cast<size_t>(dataIndex) < tx.accessLists.size())
        {
            auto const& al = tx.accessLists[dataIndex];
            if (al.has_value())
            {
                hasAccessList = true;
                for (auto const& [addr, keys] : *al)
                {
                    bcostars::Web3AccessListEntry entry;
                    entry.account = test::strip0x(addr);
                    for (auto const& k : keys)
                    {
                        auto keyBytes = test::hexToBytes(k);
                        entry.storageKeys.emplace_back(keyBytes.begin(), keyBytes.end());
                    }
                    data.accessList.push_back(std::move(entry));
                }
            }
        }

        // authorizationList field present = type 4 (set_code), even if empty.
        // evmone's validate_transaction will reject empty lists for type 4.
        bool hasAuthList = tx.authorizationList.has_value();
        if (hasAuthList)
        {
            tarsTx->web3TypedTxKind = 4;
        }
        else if (!tx.maxFeePerGas.empty() || !tx.maxPriorityFeePerGas.empty())
        {
            tarsTx->web3TypedTxKind = 2;
        }
        else if (hasAccessList)
        {
            tarsTx->web3TypedTxKind = 1;
        }
        else
        {
            tarsTx->web3TypedTxKind = 0;
        }

        if (hasAuthList)
        {
            // Helper: convert a hex string to a signed 64-bit by taking the low
            // 64 bits of the unsigned value (preserves bit patterns for values
            // >= 2^63, e.g. nonce=2**64-1 or 2**64-2 in EIP-7702 tests, which
            // hexToInt64 would otherwise saturate to INT64_MAX).
            auto hexToTarsLong = [](std::string const& hex) {
                return static_cast<int64_t>(static_cast<uint64_t>(test::hexToU256(hex)));
            };
            for (auto const& auth : *tx.authorizationList)
            {
                bcostars::AuthorizationEntry entry;
                entry.chainID = hexToTarsLong(test::readHexField(auth, "chainId"));
                entry.nonce = hexToTarsLong(test::readHexField(auth, "nonce"));
                entry.v = static_cast<uint8_t>(test::hexToInt64(test::readHexField(auth, "v")));
                entry.address = test::strip0x(test::readHexField(auth, "address"));
                entry.signer = test::strip0x(test::readHexField(auth, "signer"));
                entry.r = test::strip0x(test::readHexField(auth, "r"));
                entry.s = test::strip0x(test::readHexField(auth, "s"));
                data.authorizationList.push_back(std::move(entry));
            }
        }

        // Detect blob transaction (type 3) by presence of maxFeePerBlobGas
        // or non-empty blobVersionedHashes.  EEST fixtures that test blob-tx
        // rejection (zero blobs, pre-fork) set maxFeePerBlobGas but leave
        // blobVersionedHashes empty.
        bool isBlobTx = !tx.maxFeePerBlobGas.empty() || !tx.blobVersionedHashes.empty();
        if (isBlobTx)
        {
            tarsTx->web3TypedTxKind = 3;
            data.maxFeePerBlobGas = test::strip0x(tx.maxFeePerBlobGas);
            for (auto const& h : tx.blobVersionedHashes)
            {
                auto hashBytes = test::hexToBytes(h);
                data.blobVersionedHashes.emplace_back(hashBytes.begin(), hashBytes.end());
            }
        }

        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(tarsTx)]() mutable { return tarsTx.get(); });
    }

    /// Verify post-state. Returns a string with mismatch details (empty = all good).
    std::string verifyPostState(
        MutableStorage& storage, std::map<std::string, test::EESTAccount> const& expected)
    {
        std::ostringstream errors;
        int passed = 0, failed = 0;
        (void)passed;  // not used for return, only local tracking

        for (auto const& [addrHex, expectedAcc] : expected)
        {
            auto addrBytes = test::hexToBytes(addrHex);
            if (addrBytes.size() != sizeof(evmc_address))
            {
                continue;
            }

            evmc_address addr;
            std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);

            ledger::account::EVMAccount<MutableStorage> evmAccount(storage, addr, false);

            task::tbb::syncWait([&]() -> task::Task<void> {
                // nonce
                if (!expectedAcc.nonce.empty() && expectedAcc.nonce != "0x")
                {
                    auto storedNonce = co_await evmAccount.nonce();
                    auto expNonce = test::hexToU256(expectedAcc.nonce);
                    auto actualNonce = bcos::u256(storedNonce.value_or("0"));
                    if (expNonce != actualNonce)
                    {
                        errors << "    NONCE " << addrHex << ": expected " << expectedAcc.nonce
                               << " (" << test::hexToU256(expectedAcc.nonce) << "), got "
                               << actualNonce << "\n";
                        ++g_nonceMismatch;
                        ++failed;
                        co_return;
                    }
                }

                // balance
                if (!expectedAcc.balance.empty())
                {
                    auto storedBal = co_await evmAccount.balance();
                    auto expBal = test::hexToU256(expectedAcc.balance);
                    if (expBal != storedBal)
                    {
                        errors << "    BALANCE " << addrHex << ": expected " << expectedAcc.balance
                               << " (" << expBal << "), got " << storedBal << "\n";
                        ++g_balanceMismatch;
                        ++failed;
                        co_return;
                    }
                }

                // code
                if (!expectedAcc.code.empty() && expectedAcc.code != "0x")
                {
                    auto codeEntry = co_await evmAccount.code();
                    auto expCode = test::hexToBytes(expectedAcc.code);
                    bool codeMatch = codeEntry.has_value();
                    if (codeMatch)
                    {
                        auto codeView = codeEntry->get();
                        codeMatch = bcos::bytes(codeView.begin(), codeView.end()) == expCode;
                    }
                    if (!codeMatch)
                    {
                        errors << "    CODE " << addrHex << ": expected " << expectedAcc.code
                               << "\n";
                        ++g_codeMismatch;
                        ++failed;
                        co_return;
                    }
                }

                // storage
                for (auto const& [key, val] : expectedAcc.storage)
                {
                    auto keyBytes = test::hexToBytes(key);
                    evmc_bytes32 storageKey{};
                    if (keyBytes.size() <= 32)
                    {
                        std::copy(keyBytes.begin(), keyBytes.end(),
                            storageKey.bytes + 32 - keyBytes.size());
                    }
                    else
                    {
                        // Defensive: never copy more than 32 bytes into the
                        // 32-byte evmc_bytes32 (would overflow the stack).
                        std::copy_n(keyBytes.end() - 32, 32, storageKey.bytes);
                    }

                    auto storedVal = co_await evmAccount.storage(storageKey);
                    auto expValBytes = test::hexToBytes(val);
                    evmc_bytes32 expVal{};
                    if (expValBytes.size() <= 32)
                    {
                        std::copy(expValBytes.begin(), expValBytes.end(),
                            expVal.bytes + 32 - expValBytes.size());
                    }
                    else
                    {
                        std::copy_n(expValBytes.end() - 32, 32, expVal.bytes);
                    }

                    if (!::ranges::equal(storedVal.bytes, expVal.bytes))
                    {
                        std::string actualHex;
                        boost::algorithm::hex_lower(
                            storedVal.bytes, storedVal.bytes + 32, std::back_inserter(actualHex));
                        errors << "    STORAGE " << addrHex << " key=" << key << ": expected "
                               << val << ", got 0x" << actualHex << "\n";
                        ++g_storageMismatch;
                        ++failed;
                    }
                }
            }());

        }  // end for loop over expected accounts

        return errors.str();
    }

    /// Run a single fixture test case. Returns true if passed.
    bool runFixture(::test::EESTFixture const& fixture, std::string const& forkName,
        ::test::EESTForkPost const& post)
    {
        configureFork(forkName);
        m_chainId = fixture.chainId;
        m_ledgerConfig.setChainId(evmc::bytes32{static_cast<uint64_t>(m_chainId)});
        configureEnvironment(fixture.env);

        MutableStorage storage;
        setupPreState(storage, fixture.pre);

        // Ensure sender exists with correct initial nonce.
        // EEST fixtures may not include sender in "pre" — evmone creates it
        // with default nonce=0, then bumps to 1. But expected nonce may be 2
        // (tx nonce 1 → bumped to 2), so we must set the correct initial nonce.
        if (!fixture.transaction.sender.empty())
        {
            auto senderBytes = test::hexToBytes(fixture.transaction.sender);
            if (senderBytes.size() == sizeof(evmc_address))
            {
                evmc_address senderAddr{};
                std::copy(senderBytes.begin(), senderBytes.end(), senderAddr.bytes);
                ledger::account::EVMAccount<MutableStorage> senderAcct(storage, senderAddr, false);
                auto txNonce = test::hexToU256(fixture.transaction.nonce);
                auto nonceStr = txNonce.str(0, std::ios_base::dec);
                task::tbb::syncWait([&]() -> task::Task<void> {
                    if (!co_await senderAcct.exists())
                    {
                        co_await senderAcct.create();
                        co_await senderAcct.setNonce(nonceStr);
                    }
                }());
            }
        }

        auto blockHeader = buildBlockHeader(fixture.env);
        auto tx =
            buildTransaction(fixture.transaction, post.dataIndex, post.gasIndex, post.valueIndex);

        protocol::TransactionReceipt::Ptr receipt;
        std::string evmError;
        bool executionThrew = false;

        try
        {
            receipt = task::tbb::syncWait(
                executor.executeTransaction(storage, blockHeader, *tx, 0, m_ledgerConfig, false));
        }
        catch (std::exception const& e)
        {
            evmError = e.what();
            executionThrew = true;
        }

        // Determine expected outcome.
        // EEST v5.4.0 fixtures encode expected-success/failure in the test name
        // via "successful_True"/"successful_False" parameters. The JSON
        // "expectException" field is absent for both cases.
        bool expectsSuccess = post.expectException.empty();
        if (expectsSuccess && fixture.name.find("successful_False") != std::string::npos)
        {
            expectsSuccess = false;
        }
        bool gotSuccess =
            !executionThrew && receipt &&
            (receipt->status() == 0 ||
                receipt->status() == static_cast<int32_t>(protocol::TransactionStatus::None));

        // When expectException is empty but the transaction failed, the test
        // may still be correct if the post-state matches (transaction reverted
        // cleanly and only gas was deducted).  EEST v5.4.0 fixtures often omit
        // expectException for expected-failure tests.
        if (expectsSuccess && !gotSuccess)
        {
            auto errStr = verifyPostState(storage, post.state);
            if (errStr.empty())
            {
                return true;  // State matches → test passes (tx reverted correctly)
            }

            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "] expected success, got failure"
                << (executionThrew ? " (exception: " + evmError + ")" : "")
                << (receipt ? " status=" + std::to_string(receipt->status()) : "");
            printLine(oss.str());
            g_failureDetails.push_back({fixture.name, forkName, "unexpected failure"});
            ++g_unexpectedFailure;
            return false;
        }

        if (!expectsSuccess && gotSuccess)
        {
            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "] expected exception '"
                << post.expectException << "', got success";
            printLine(oss.str());
            g_failureDetails.push_back(
                {fixture.name, forkName, "expected exception '" + post.expectException + "'"});
            ++g_expectedException;
            return false;
        }

        // Both expect and got failure → pass only if the post-state matches.
        // The expected-failure path must still verify the resulting state so
        // that a wrong-reason failure (e.g. a pre-check exception instead of
        // the intended revert) with a divergent state is caught rather than
        // silently scored as "failed as expected".
        if (!expectsSuccess && !gotSuccess)
        {
            auto errStr = verifyPostState(storage, post.state);
            if (!errStr.empty())
            {
                std::ostringstream oss;
                oss << "FAIL: " << fixture.name << " [" << forkName
                    << "] expected failure, got failure but state mismatch\n"
                    << errStr;
                printLine(oss.str());
                g_failureDetails.push_back(
                    {fixture.name, forkName, "state mismatch (expected failure)"});
                ++g_unexpectedFailure;
                return false;
            }
            return true;
        }

        // Success: verify post-state
        auto errStr = verifyPostState(storage, post.state);
        if (!errStr.empty())
        {
            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "]\n" << errStr;
            printLine(oss.str());
            g_failureDetails.push_back({fixture.name, forkName, "state mismatch"});
            return false;
        }

        return true;
    }

    // ---- Blockchain test support ----

    /// Extract fork name from a blockchain test fixture name.
    /// EEST blockchain test names look like:
    ///   tests/shanghai/.../test_xxx[fork_Cancun-blockchain_test-...]
    /// Returns "Cancun" or empty string if not found.
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

    /// Build a BCOS BlockHeader from a blockchain test block header.
    bcostars::protocol::BlockHeaderImpl buildBlockHeader(test::EESTBlockHeader const& bh)
    {
        bcostars::protocol::BlockHeaderImpl header;
        uint32_t blockVer;
        if (m_currentRevision >= EVMC_CANCUN)
        {
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        }
        else if (m_currentRevision >= EVMC_PARIS)
        {
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION);
        }
        else
        {
            blockVer = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION);
        }
        header.setVersion(blockVer);

        header.setNumber(test::hexToInt64(bh.number));
        auto tsSec = test::hexToTimestamp(bh.timestamp);
        header.setTimestamp(tsSec * 1000);
        header.setSealer(0);
        if (!bh.coinbase.empty())
        {
            auto cbBytes = test::hexToBytes(bh.coinbase);
            bcos::Address coinbaseAddr;
            if (cbBytes.size() >= bcos::Address::SIZE)
            {
                std::memcpy(coinbaseAddr.data(), cbBytes.data(), bcos::Address::SIZE);
            }
            header.setCoinbase(coinbaseAddr);
        }

        // EIP-4399 (Paris+): PREVRANDAO (opcode 0x44) returns the block mixHash.
        // The executor resolves it from header.prevRandao() (blockHeaderToBlockInfo),
        // so the mixHash parsed from the fixture block header MUST be placed on
        // the BCOS header too — configuring m_ledgerConfig alone is not enough
        // (same fix as the state-test buildBlockHeader(env)). Without this,
        // Cancun/Prague blockchain tests (static stRandom / mergeTest / blockInfo)
        // read PREVRANDAO == 0, and an SSTORE of that 0 into an empty slot is
        // charged as a no-op (100) instead of SSTORE_SET (20000) → -19900 gas.
        if (!bh.random.empty())
        {
            auto mixHashBytes = test::hexToBytes(bh.random);
            if (!mixHashBytes.empty())
            {
                // bcos::h256 from raw bytes (right-aligned, big-endian).
                header.setPrevRandao(bcos::h256(mixHashBytes));
            }
            else
            {
                header.setPrevRandao(bcos::h256{});
            }
        }
        else
        {
            header.setPrevRandao(bcos::h256{});
        }
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    /// Configure environment from blockchain test block header.
    void configureEnvironment(test::EESTBlockHeader const& bh)
    {
        if (!bh.baseFee.empty())
        {
            auto baseFeeVal = test::hexToU256(bh.baseFee);
            auto hexStr = "0x" + baseFeeVal.str(0, std::ios_base::hex);
            m_ledgerConfig.setGasPrice({hexStr, 0});
        }
        else
        {
            m_ledgerConfig.setGasPrice({"0x0", 0});
        }
        // Set gas limit from block header (fixes GASLIMIT opcode returning
        // the hardcoded default instead of the fixture's block gas limit)
        if (!bh.gasLimit.empty())
        {
            auto gasLimit = static_cast<uint64_t>(test::hexToU256(bh.gasLimit));
            m_ledgerConfig.setGasLimit({gasLimit, 0});
        }
        // Set difficulty from block header (fixes DIFFICULTY opcode)
        if (!bh.difficulty.empty())
        {
            auto diff = static_cast<int64_t>(test::hexToU256(bh.difficulty));
            m_ledgerConfig.setDifficulty(diff);
        }
        else
        {
            m_ledgerConfig.setDifficulty(0);
        }
        // Set prev_randao from mixHash (EIP-4399: PREVRANDAO opcode on Paris+)
        if (!bh.random.empty())
        {
            auto mixHashBytes = test::hexToBytes(bh.random);
            evmc::bytes32 prevRandao{};
            if (mixHashBytes.size() == sizeof(evmc_bytes32))
            {
                std::copy_n(mixHashBytes.begin(), sizeof(evmc_bytes32), prevRandao.bytes);
            }
            else if (mixHashBytes.size() < sizeof(evmc_bytes32))
            {
                std::copy_n(mixHashBytes.begin(), mixHashBytes.size(),
                    prevRandao.bytes + sizeof(evmc_bytes32) - mixHashBytes.size());
            }
            m_ledgerConfig.setPrevRandao(prevRandao);
        }
        else
        {
            m_ledgerConfig.setPrevRandao({});
        }
        // EIP-4844 blob gas parameters (Cancun+).
        if (!bh.excessBlobGas.empty())
        {
            m_ledgerConfig.setExcessBlobGas(
                static_cast<uint64_t>(test::hexToU256(bh.excessBlobGas)));
        }
        else
        {
            m_ledgerConfig.setExcessBlobGas(std::nullopt);
        }
        if (!bh.blobGasUsed.empty())
        {
            m_ledgerConfig.setBlobGasUsed(static_cast<uint64_t>(test::hexToU256(bh.blobGasUsed)));
        }
        else
        {
            m_ledgerConfig.setBlobGasUsed(std::nullopt);
        }
    }

    /// Run a blockchain test fixture: execute blocks sequentially,
    /// apply block rewards via finalize(), then verify postState.
    /// Returns true if passed.
    bool runBlockchainFixture(test::EESTBlockchainFixture const& fixture)
    {
        auto forkName = extractForkFromName(fixture.name);
        if (forkName.empty())
        {
            forkName = "Cancun";  // default
        }
        configureFork(forkName);
        m_chainId = fixture.chainId;
        m_ledgerConfig.setChainId(evmc::bytes32{static_cast<uint64_t>(m_chainId)});

        // Set up pre-state once
        MutableStorage storage;
        setupPreState(storage, fixture.pre);

        // Ensure sender accounts exist with correct initial nonce.
        // EEST fixtures may not include sender in "pre".
        for (auto const& block : fixture.blocks)
        {
            for (auto const& tx : block.transactions)
            {
                if (tx.sender.empty())
                {
                    continue;
                }
                auto senderBytes = test::hexToBytes(tx.sender);
                if (senderBytes.size() != sizeof(evmc_address))
                {
                    continue;
                }
                evmc_address senderAddr{};
                std::copy(senderBytes.begin(), senderBytes.end(), senderAddr.bytes);
                ledger::account::EVMAccount<MutableStorage> senderAcct(storage, senderAddr, false);
                auto txNonce = test::hexToU256(tx.nonce);
                auto nonceStr = txNonce.str(0, std::ios_base::dec);
                task::tbb::syncWait([&]() -> task::Task<void> {
                    if (!co_await senderAcct.exists())
                    {
                        co_await senderAcct.create();
                        co_await senderAcct.setNonce(nonceStr);
                    }
                }());
            }
        }

        // Configure initial environment from genesis header
        configureEnvironment(fixture.genesisBlockHeader);

        // Set up BlockHashes with genesis hash for HISTORY_STORAGE system call.
        // The provider was injected into the executor's constructor; start
        // each fixture run from an empty mapping.
        auto& blockHashes = *m_blockHashes;
        blockHashes.hashes.clear();
        if (!fixture.genesisBlockHeader.hash.empty())
        {
            auto genesisHashBytes = test::hexToBytes(fixture.genesisBlockHeader.hash);
            if (genesisHashBytes.size() == sizeof(evmc_bytes32))
            {
                std::copy_n(
                    genesisHashBytes.begin(), sizeof(evmc_bytes32), blockHashes.hashes[0].bytes);
            }
        }

        // Populate fork transition mapping in LedgerConfig for dynamic revision switching.
        // For transition forks (e.g., CancunToPragueAtTime15k), the revision changes
        // mid-test based on block timestamp.
        m_ledgerConfig.clearForkTransitions();
        if (m_forkTransition.has_value())
        {
            for (auto const& block : fixture.blocks)
            {
                auto tsSec = test::hexToTimestamp(block.blockHeader.timestamp);
                auto blockRev = (tsSec >= m_forkTransition->transitionTimestamp) ?
                                    m_forkTransition->toRev :
                                    m_forkTransition->fromRev;
                auto blockNum = test::hexToInt64(block.blockHeader.number);
                m_ledgerConfig.addForkTransition(blockNum, blockRev);
            }
        }

        // Execute each block's transactions sequentially
        int txIndex = 0;
        for (auto const& block : fixture.blocks)
        {
            // Determine per-block revision (for fork transitions)
            auto tsSec = test::hexToTimestamp(block.blockHeader.timestamp);
            auto blockRev = revisionForBlock(tsSec);
            m_ledgerConfig.setEVMCRevision(blockRev);

            // Per-fork block rewards (wei).
            constexpr static uint64_t BLOCK_REWARD_5_ETH = 5000000000000000000ULL;
            constexpr static uint64_t BLOCK_REWARD_3_ETH = 3000000000000000000ULL;
            constexpr static uint64_t BLOCK_REWARD_2_ETH = 2000000000000000000ULL;

            // Compute per-block reward (changes at fork boundaries)
            std::optional<uint64_t> blockReward;
            if (blockRev < EVMC_BYZANTIUM)
            {
                blockReward = BLOCK_REWARD_5_ETH;  // 5 ETH
            }
            else if (blockRev < EVMC_PETERSBURG)
            {
                blockReward = BLOCK_REWARD_3_ETH;  // 3 ETH
            }
            else if (blockRev < EVMC_PARIS)
            {
                blockReward = BLOCK_REWARD_2_ETH;  // 2 ETH
            }
            // else: Paris+ → 0

            configureEnvironment(block.blockHeader);
            auto blockHdr = buildBlockHeader(block.blockHeader);

            // EIP-4788 & EIP-2935: Execute system contracts at block start
            if (blockRev >= EVMC_CANCUN)
            {
                evmone::state::BlockInfo bi{};
                bi.number = test::hexToInt64(block.blockHeader.number);
                bi.timestamp = tsSec;
                bi.gas_limit = test::hexToInt64(block.blockHeader.gasLimit);
                if (!block.blockHeader.coinbase.empty())
                {
                    auto cbBytes = test::hexToBytes(block.blockHeader.coinbase);
                    if (cbBytes.size() == sizeof(evmc_address))
                    {
                        std::copy_n(cbBytes.begin(), sizeof(evmc_address), bi.coinbase.bytes);
                    }
                }
                if (!block.blockHeader.baseFee.empty())
                {
                    bi.base_fee = static_cast<uint64_t>(test::hexToU256(block.blockHeader.baseFee));
                }
                if (!block.blockHeader.parentBeaconRoot.empty())
                {
                    auto pbr = test::hexToBytes(block.blockHeader.parentBeaconRoot);
                    if (pbr.size() == sizeof(evmc_bytes32))
                    {
                        std::copy_n(
                            pbr.begin(), sizeof(evmc_bytes32), bi.parent_beacon_block_root.bytes);
                    }
                }
                // blob_base_fee is left as nullopt — system contracts don't need it

                ::bcos::test::TestStorageStateView<MutableStorage> stateView(storage);
                auto sysDiff = evmone::state::system_call_block_start(
                    stateView, bi, blockHashes, blockRev, executor.vm());

                task::tbb::syncWait(
                    ::bcos::test::testApplyStateDiff(storage, sysDiff, *cryptoSuite->hashImpl()));
            }

            for (auto const& tx : block.transactions)
            {
                auto bcosTx =
                    buildTransaction(tx, 0 /*dataIndex*/, 0 /*gasIndex*/, 0 /*valueIndex*/);

                protocol::TransactionReceipt::Ptr receipt;
                std::string evmError;
                bool executionThrew = false;

                try
                {
                    receipt = task::tbb::syncWait(executor.executeTransaction(
                        storage, blockHdr, *bcosTx, txIndex, m_ledgerConfig, false));
                }
                catch (std::exception const& e)
                {
                    evmError = e.what();
                    executionThrew = true;
                }

                if (executionThrew || !receipt ||
                    (receipt->status() != 0 &&
                        receipt->status() !=
                            static_cast<int32_t>(protocol::TransactionStatus::None)))
                {
                    // Transaction failed. If expectException is set, the block
                    // is expected to be invalid — let post-state check catch it.
                    // If expectException is empty, we still continue to
                    // finalizeBlock and let the post-state check at the end
                    // determine pass/fail (handles EIP-7623 edge cases).
                    // Only abort immediately for executionThrew (state unchanged).
                    if (executionThrew)
                    {
                        std::ostringstream oss;
                        oss << "FAIL: " << fixture.name << " [" << forkName << "] tx#" << txIndex
                            << " execution threw: " << evmError;
                        printLine(oss.str());
                        g_failureDetails.push_back({fixture.name, forkName,
                            "tx#" + std::to_string(txIndex) + " execution threw"});
                        ++g_unexpectedFailure;
                        return false;
                    }
                }
                ++txIndex;
            }

            // Convert withdrawals to the executor's EthWithdrawal format
            std::vector<eth::EthWithdrawal> evmWithdrawals;
            for (auto const& w : block.withdrawals)
            {
                eth::EthWithdrawal ew;
                ew.index = static_cast<uint64_t>(test::hexToInt64(w.index));
                ew.validator_index = static_cast<uint64_t>(test::hexToInt64(w.validatorIndex));
                auto addrBytes = test::hexToBytes(w.address);
                if (addrBytes.size() == sizeof(evmc_address))
                {
                    std::copy_n(addrBytes.begin(), sizeof(evmc_address), ew.recipient.bytes);
                }
                auto amountVal = test::hexToU256(w.amount);
                ew.amount_in_gwei = static_cast<uint64_t>(amountVal);
                evmWithdrawals.push_back(ew);
            }

            // Apply block reward via finalize() (uses bcos-evm's built-in logic)
            task::tbb::syncWait(executor.finalizeBlock(
                storage, blockHdr, m_ledgerConfig, blockRev, blockReward, evmWithdrawals));

            // Accumulate block hash BEFORE system_call_block_end so the current
            // block's hash is available for BLOCKHASH lookups within system contracts.
            if (!block.blockHeader.hash.empty())
            {
                auto hashBytes = test::hexToBytes(block.blockHeader.hash);
                if (hashBytes.size() == sizeof(evmc_bytes32))
                {
                    auto blockNum = test::hexToInt64(block.blockHeader.number);
                    std::copy_n(hashBytes.begin(), sizeof(evmc_bytes32),
                        blockHashes.hashes[blockNum].bytes);
                }
            }

            // EIP-2935 / EIP-7002 / EIP-7251: Execute system contracts at block end.
            // These store historical block hashes, process withdrawal requests,
            // and process consolidation requests respectively.
            if (blockRev >= EVMC_CANCUN)
            {
                evmone::state::BlockInfo biEnd{};
                biEnd.number = test::hexToInt64(block.blockHeader.number);
                biEnd.timestamp = tsSec;
                biEnd.gas_limit = test::hexToInt64(block.blockHeader.gasLimit);
                if (!block.blockHeader.coinbase.empty())
                {
                    auto cbBytes = test::hexToBytes(block.blockHeader.coinbase);
                    if (cbBytes.size() == sizeof(evmc_address))
                    {
                        std::copy_n(cbBytes.begin(), sizeof(evmc_address), biEnd.coinbase.bytes);
                    }
                }
                if (!block.blockHeader.baseFee.empty())
                {
                    biEnd.base_fee =
                        static_cast<uint64_t>(test::hexToU256(block.blockHeader.baseFee));
                }
                if (!block.blockHeader.parentBeaconRoot.empty())
                {
                    auto pbr = test::hexToBytes(block.blockHeader.parentBeaconRoot);
                    if (pbr.size() == sizeof(evmc_bytes32))
                    {
                        std::copy_n(pbr.begin(), sizeof(evmc_bytes32),
                            biEnd.parent_beacon_block_root.bytes);
                    }
                }
                // Set the current block's hash for EIP-2935 history storage
                if (!block.blockHeader.hash.empty())
                {
                    auto hashBytes = test::hexToBytes(block.blockHeader.hash);
                    if (hashBytes.size() == sizeof(evmc_bytes32))
                    {
                        std::copy_n(hashBytes.begin(), sizeof(evmc_bytes32), biEnd.hash.bytes);
                    }
                }

                ::bcos::test::TestStorageStateView<MutableStorage> stateViewEnd(storage);
                auto endResult = evmone::state::system_call_block_end(
                    stateViewEnd, biEnd, blockHashes, blockRev, executor.vm());
                if (endResult.has_value())
                {
                    task::tbb::syncWait(::bcos::test::testApplyStateDiff(
                        storage, endResult->state_diff, *cryptoSuite->hashImpl()));
                }
            }
        }

        // Verify postState
        auto errStr = verifyPostState(storage, fixture.postState);
        if (!errStr.empty())
        {
            std::ostringstream oss;
            oss << "FAIL: " << fixture.name << " [" << forkName << "]\n" << errStr;
            printLine(oss.str());
            g_failureDetails.push_back({fixture.name, forkName, "state mismatch"});
            return false;
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
        return "unsupported";
    }
    if (str.find("/blockchain_tests_engine/") != std::string::npos)
    {
        return "unsupported";
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
    if (format == "blockchain_test")
    {
        std::vector<test::EESTBlockchainFixture> bcFixtures;
        try
        {
            bcFixtures = test::loadEESTBlockchainFixtures(filePath.string());
        }
        catch (std::exception const& e)
        {
            // One unreadable/malformed file must not abort the whole run.
            std::cerr << "Warning: failed to load fixture file " << filePath.string() << ": "
                      << e.what() << "\n";
            ++g_loadFailures;
            ++g_skippedFiles;
            return result;
        }
        for (auto const& fixture : bcFixtures)
        {
            ++g_totalTests;
            bool passed = runner.runBlockchainFixture(fixture);
            if (passed)
            {
                ++g_totalPassed;
                ++result.passed;
            }
            else
            {
                ++g_totalFailed;
                ++result.failed;
            }
        }
        return result;
    }

    // --- transaction_test / unsupported (skip) ---
    if (format == "transaction_test" || format == "unsupported")
    {
        ++g_skippedFiles;
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
        ++g_skippedFiles;
        return result;
    }
    for (auto const& fixture : fixtures)
    {
        for (auto const& [forkName, posts] : fixture.post)
        {
            for (size_t i = 0; i < posts.size(); ++i)
            {
                ++g_totalTests;
                auto const& post = posts[i];
                bool passed = runner.runFixture(fixture, forkName, post);

                if (passed)
                {
                    ++g_totalPassed;
                    ++result.passed;
                }
                else
                {
                    ++g_totalFailed;
                    ++result.failed;
                }
            }
        }
    }
    return result;
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
            if (entry.is_regular_file() && entry.path().extension().string() == ".json")
            {
                fileList.push_back(entry.path());
            }
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

    EESTRunner runner;
    for (size_t i = 0; i < fileList.size(); ++i)
    {
        auto& p = fileList[i];
        if (!g_opts.quiet)
        {
            fs::path base = fs::absolute(fixtureDir);
            auto rel = fs::relative(p, base);
            std::cout << "  " << rel.string() << " ... " << std::flush;
        }
        auto fr = processFixtureFile(runner, p);
        if (!g_opts.quiet)
        {
            int total = fr.passed + fr.failed;
            if (fr.failed == 0)
            {
                std::cout << "OK (" << total << " tests)" << '\n';
            }
            else
            {
                std::cout << fr.failed << "/" << total << " FAILED" << '\n';
            }
        }
        printProgress(i + 1, fileList.size());
    }

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
    std::cout << "  Skipped files: " << g_skippedFiles.load() << "\n";
    std::cout << "  Load failures: " << g_loadFailures.load() << "\n";
    std::cout << "  Elapsed:       " << elapsed << "s\n";

    std::cout << "\n  Error breakdown:\n";
    std::cout << "    Balance mismatch:   " << g_balanceMismatch.load() << "\n";
    std::cout << "    Storage mismatch:   " << g_storageMismatch.load() << "\n";
    std::cout << "    Nonce mismatch:     " << g_nonceMismatch.load() << "\n";
    std::cout << "    Code mismatch:      " << g_codeMismatch.load() << "\n";
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

    std::cout << "=================================================\n";

    return g_totalFailed > 0 ? 1 : 0;
}
