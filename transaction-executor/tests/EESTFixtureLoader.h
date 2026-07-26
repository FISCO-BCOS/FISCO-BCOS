#pragma once

#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <json/json.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/throw_exception.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcos::test
{

// --- Parsed EEST state test structures ---

/// A single pre-state account.
struct EESTAccount
{
    std::string nonce;                           // hex string
    std::string balance;                         // hex string
    std::string code;                            // hex string (with 0x prefix)
    std::map<std::string, std::string> storage;  // key → value, hex strings
};

/// Block environment from the "env" section.
struct EESTEnvironment
{
    std::string coinbase;          // "currentCoinbase"
    std::string gasLimit;          // "currentGasLimit"
    std::string number;            // "currentNumber"
    std::string timestamp;         // "currentTimestamp"
    std::string difficulty;        // "currentDifficulty" (optional)
    std::string baseFee;           // "currentBaseFee" (optional, London+)
    std::string random;            // "currentRandom" (optional, Paris+)
    std::string excessBlobGas;     // "currentExcessBlobGas" (optional, Cancun+)
    std::string blobGasUsed;       // "currentBlobGasUsed" (optional, Cancun+)
    std::string parentBeaconRoot;  // "parentBeaconBlockRoot" (optional, Cancun+)
};

/// Transaction from the "transaction" section.
struct EESTTransaction
{
    std::string nonce;
    std::string gasPrice;
    std::string maxPriorityFeePerGas;
    std::string maxFeePerGas;
    std::vector<std::string> gasLimit;  // array of hex values
    std::string to;                     // hex address, empty = create
    std::vector<std::string> value;     // array of hex values
    std::vector<std::string> data;      // array of hex strings
    std::string sender;
    std::string secretKey;
    // accessLists: per-index access list (null = none)
    std::vector<std::optional<std::vector<std::pair<std::string, std::vector<std::string>>>>>
        accessLists;
    // authorizationList (EIP-7702): null if not present
    std::optional<std::vector<Json::Value>> authorizationList;
    std::string maxFeePerBlobGas;
    std::vector<std::string> blobVersionedHashes;  // EIP-4844 blob versioned hashes
};

/// A single post-state entry for a fork.
struct EESTForkPost
{
    int dataIndex = 0;
    int gasIndex = 0;
    int valueIndex = 0;
    std::string stateRoot;                     // "hash" — expected state root
    std::string logsHash;                      // "logs"
    std::string txBytes;                       // "txbytes" — RLP-encoded transaction
    std::string expectException;               // exception name (empty = success expected)
    std::map<std::string, EESTAccount> state;  // expected post-state accounts
};

/// One complete state test fixture.
struct EESTFixture
{
    std::string name;
    EESTEnvironment env;
    std::map<std::string, EESTAccount> pre;
    EESTTransaction transaction;
    std::map<std::string, std::vector<EESTForkPost>> post;  // forkName → post states
    int64_t chainId = 1;
};

// === Parsing helpers ===

/// Read an optional hex string from a JSON object field.
/// Returns empty string if field is missing or null.
inline std::string readHexField(Json::Value const& obj, std::string const& key)
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

/// Read a required hex string field. Throws if missing.
inline std::string readRequiredHex(Json::Value const& obj, std::string const& key)
{
    auto val = readHexField(obj, key);
    if (val.empty() && !obj.isMember(key))
        BOOST_THROW_EXCEPTION(std::runtime_error("Missing required field: " + key));
    return val;
}

/// Parse a hex string to bytes (returns empty if input is empty or "0x").
inline bcos::bytes hexToBytes(std::string const& hex)
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

/// Convert a hex string to u256.
inline bcos::u256 hexToU256(std::string const& hex)
{
    if (hex.empty() || hex == "0x")
        return 0;
    return bcos::u256(hex);
}

/// Convert a hex string to int64_t.
inline int64_t hexToInt64(std::string const& hex)
{
    if (hex.empty() || hex == "0x")
        return 0;
    return static_cast<int64_t>(hexToU256(hex));
}

/// Strip 0x prefix from a hex string.
inline std::string strip0x(std::string const& hex)
{
    if (hex.size() >= 2 && hex[0] == '0' && hex[1] == 'x')
        return hex.substr(2);
    return hex;
}

// === Fixture loader ===

/// Parse the "env" section.
inline EESTEnvironment parseEnvironment(Json::Value const& envJson)
{
    EESTEnvironment env;
    env.coinbase = readHexField(envJson, "currentCoinbase");
    env.gasLimit = readRequiredHex(envJson, "currentGasLimit");
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

/// Parse a single account from the "pre" or post-state.
inline EESTAccount parseAccount(Json::Value const& accJson)
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

/// Parse the "transaction" section.
inline EESTTransaction parseTransaction(Json::Value const& txJson)
{
    EESTTransaction tx;
    tx.nonce = readHexField(txJson, "nonce");
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

/// Parse a single post-state entry.
inline EESTForkPost parseForkPost(Json::Value const& postJson)
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

/// Parse a single state test fixture from a JSON object.
inline EESTFixture parseFixture(std::string const& name, Json::Value const& fixtureJson)
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

/// Load all state test fixtures from a JSON file.
/// The file is expected to be in EEST state test format:
///   { "fixture_name": { "env":..., "pre":..., "transaction":..., "post":{...}, "config":... }, ...
///   }
inline std::vector<EESTFixture> loadEESTFixtures(std::string const& filePath)
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
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (it.key().asString().rfind("//", 0) == 0 ||  // comment entry
            it.key().asString().rfind("_", 0) == 0)     // meta entry like _info
            continue;

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

    return fixtures;
}

/// Map an EEST fork name to an EVMC revision.
/// Returns EVMC_CANCUN as a reasonable default.
inline evmc_revision forkNameToRevision(std::string const& forkName)
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
    if (lower == "muir glacier" || lower == "muirglacier")
        return EVMC_BERLIN;
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

    // Unknown fork → default to latest
    return EVMC_CANCUN;
}

}  // namespace bcos::test
