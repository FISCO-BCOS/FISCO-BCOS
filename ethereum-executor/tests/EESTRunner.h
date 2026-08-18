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
    std::string chainId;  // EIP-155 chain id (used by EIP-7702 authorization matching)
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

// --- Blockchain test structures (EEST v5.4.0) ---

/// Simplified block header for blockchain tests.
struct EESTBlockHeader
{
    std::string coinbase;
    std::string gasLimit;
    std::string number;
    std::string timestamp;
    std::string difficulty;
    std::string baseFee;
    std::string random;
    std::string excessBlobGas;
    std::string blobGasUsed;
    std::string parentBeaconRoot;
    std::string hash;  // block hash (for EIP-2935 history storage)
};

/// A single withdrawal in a block (EIP-4895).
struct EESTWithdrawal
{
    std::string index;
    std::string validatorIndex;
    std::string address;
    std::string amount;  // in Gwei
};

/// A single block in a blockchain test.
struct EESTBlock
{
    EESTBlockHeader blockHeader;
    std::vector<EESTTransaction> transactions;
    std::vector<EESTWithdrawal> withdrawals;
};

/// A complete blockchain test fixture.
struct EESTBlockchainFixture
{
    std::string name;
    EESTBlockHeader genesisBlockHeader;
    std::map<std::string, EESTAccount> pre;
    std::vector<EESTBlock> blocks;
    std::map<std::string, EESTAccount> postState;  // expected state after all blocks
    int64_t chainId = 1;
};

// === Parsing helpers ===

/// Read an optional hex string from a JSON object field.
/// Returns empty string if field is missing or null.
std::string readHexField(Json::Value const& obj, std::string const& key);

/// Read a required hex string field. Throws if missing.
std::string readRequiredHex(Json::Value const& obj, std::string const& key);

/// Parse a hex string to bytes (returns empty if input is empty or "0x").
bcos::bytes hexToBytes(std::string const& hex);

/// Convert a hex string to u256.
bcos::u256 hexToU256(std::string const& hex);

/// Convert a hex string to int64_t.
int64_t hexToInt64(std::string const& hex);

/// Convert a hex string to a timestamp-preserving int64_t.
/// Block timestamps in EEST fixtures can exceed int64_t range (e.g.,
/// 0xfffffffffffffffe), but the EVM uses unsigned 256-bit math for TIMESTAMP.
/// This function preserves the low 64 bits so that the EVM sees the correct
/// unsigned value instead of a sign-extended negative one.
int64_t hexToTimestamp(std::string const& hex);

/// Strip 0x prefix from a hex string.
std::string strip0x(std::string const& hex);

// === Fixture loader ===

/// Detect fixture format from JSON object keys.
/// Returns: "state_test", "blockchain_test", or "unknown".
std::string detectFixtureFormat(Json::Value const& fixtureJson);

/// Parse the "env" section.
EESTEnvironment parseEnvironment(Json::Value const& envJson);

/// Parse a block header from blockchain test format.
EESTBlockHeader parseBlockHeader(Json::Value const& headerJson);

/// Parse a single account from the "pre" or post-state.
EESTAccount parseAccount(Json::Value const& accJson);

/// Parse the "transaction" section.
EESTTransaction parseTransaction(Json::Value const& txJson);

/// Parse a single post-state entry.
EESTForkPost parseForkPost(Json::Value const& postJson);

/// Parse a single state test fixture from a JSON object.
EESTFixture parseFixture(std::string const& name, Json::Value const& fixtureJson);

/// Load all state test fixtures from a JSON file.
/// The file is expected to be in EEST state test format:
///   { "fixture_name": { "env":..., "pre":..., "transaction":..., "post":{...}, "config":... }, ...
///   }
std::vector<EESTFixture> loadEESTFixtures(std::string const& filePath);

// =============================================================================
//  Blockchain test loading (EEST v5.4.0)
//  Format: { "name": { "genesisBlockHeader":..., "pre":..., "blocks":[...],
//                      "postState":..., "config":... }, ... }
// =============================================================================

/// Parse a blockchain test transaction (flat format, no index arrays).
EESTTransaction parseBlockchainTransaction(Json::Value const& txJson);

/// Parse a single blockchain test fixture.
EESTBlockchainFixture parseBlockchainFixture(
    std::string const& name, Json::Value const& fixtureJson);

/// Load all blockchain test fixtures from a JSON file.
std::vector<EESTBlockchainFixture> loadEESTBlockchainFixtures(std::string const& filePath);

/// Map an EEST fork name to an EVMC revision.
/// Returns std::nullopt for unknown forks so callers can skip the fixture
/// instead of silently executing it under a wrong (latest) revision.
std::optional<evmc_revision> forkNameToRevision(std::string const& forkName);

}  // namespace bcos::test
