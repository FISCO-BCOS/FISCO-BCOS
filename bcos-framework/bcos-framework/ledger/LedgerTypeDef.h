/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file LedgerTypeDef.h
 * @author: kyonRay
 * @date 2021-04-30
 */

#pragma once
#include "../consensus/ConsensusNode.h"
#include "../protocol/ProtocolTypeDef.h"
#include "SystemConfigs.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <bcos-utilities/Common.h>
#include <string_view>
#include <optional>
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <boost/lexical_cast.hpp>
#include <magic_enum/magic_enum.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>

namespace bcos::ledger
{
using MerkleProof = std::vector<crypto::HashType>;

/// Parse the web3 chain-id system-config string as u256. nullopt on a corrupted value (non
/// numeric, empty, negative, or an unparsed remainder) — the single shared parse for the three
/// config consumers (TxValidator::validateChainId — exists in base (reads the tars mirror);
/// its envelope-keyed implementation migrates with part 4b, PR #5477 — EthEndpoint's
/// chainId gate, LedgerMethods::loadChainConfig [part 4b]) so width semantics and error
/// behavior cannot drift. Note: the envelope side (web3ChainIdFromEnvelope / Web3TxHandler
/// decodes) is uint64-capped with the RLP width gate, so in practice chainIds > 2^64-1 are
/// rejected at decode (fail-closed) — the u256 parse here exists so a misconfigured
/// over-wide value never silently matches; real chains are far below the cap. Also note: boost::
/// multiprecision's u256 stream-in ACCEPTS a negative sign and wraps modulo 2^256 (probe-
/// verified: lexical_cast<u256>("-5") returns 2^256-5, no throw); a leading '-' is rejected
/// explicitly here so a mistyped negative config surfaces as a clear per-config error instead
/// of a wrapped value that fail-closes confusingly at every tx gate.
[[nodiscard]] inline std::optional<u256> parseWeb3ChainId(std::string_view chainIdStr)
{
    if (!chainIdStr.empty() && chainIdStr[0] == '-') [[unlikely]]
    {
        return std::nullopt;
    }
    try
    {
        return boost::lexical_cast<u256>(chainIdStr);
    }
    catch (boost::bad_lexical_cast const&)
    {
        return std::nullopt;
    }
}
using MerkleProofPtr = std::shared_ptr<const MerkleProof>;

// get block flag
constexpr static int32_t FULL_BLOCK = 0xFFFE;
constexpr static int32_t HEADER = 0x0008;
constexpr static int32_t TRANSACTIONS_HASH = 0x0001;
constexpr static int32_t TRANSACTIONS = 0x0004;
constexpr static int32_t RECEIPTS = 0x0002;

// clang-format off
// get system config key
constexpr static std::string_view SYSTEM_KEY_WEB3_CHAIN_ID = magic_enum::enum_name(SystemConfig::web3_chain_id);
constexpr static std::string_view SYSTEM_KEY_TX_GAS_LIMIT = magic_enum::enum_name(SystemConfig::tx_gas_limit);
constexpr static std::string_view SYSTEM_KEY_TX_GAS_PRICE = magic_enum::enum_name(SystemConfig::tx_gas_price);
constexpr static std::string_view SYSTEM_KEY_TX_COUNT_LIMIT = magic_enum::enum_name(SystemConfig::tx_count_limit);
constexpr static std::string_view SYSTEM_KEY_CONSENSUS_LEADER_PERIOD = magic_enum::enum_name(SystemConfig::consensus_leader_period);
constexpr static std::string_view SYSTEM_KEY_AUTH_CHECK_STATUS = magic_enum::enum_name(SystemConfig::auth_check_status);
// for compatibility
constexpr static std::string_view SYSTEM_KEY_COMPATIBILITY_VERSION = magic_enum::enum_name(SystemConfig::compatibility_version);
// system configuration for RPBFT
constexpr static std::string_view SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM = magic_enum::enum_name(SystemConfig::feature_rpbft_epoch_sealer_num);
constexpr static std::string_view SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM = magic_enum::enum_name(SystemConfig::feature_rpbft_epoch_block_num);
constexpr static std::string_view SYSTEM_KEY_RPBFT_SWITCH = magic_enum::enum_name(SystemConfig::feature_rpbft);
// system configuration for balance
constexpr static std::string_view SYSTEM_KEY_BALANCE_PRECOMPILED_SWITCH = magic_enum::enum_name(SystemConfig::feature_balance_precompiled);
// notify rotate key for rpbft
constexpr static std::string_view INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE = "feature_rpbft_notify_rotate";
constexpr static std::string_view ENABLE_BALANCE_TRANSFER = magic_enum::enum_name(SystemConfig::balance_transfer);
// system configuration for ethereum-executor EVM revision (executor_version=2)
constexpr static std::string_view SYSTEM_KEY_EVMC_REVISION = magic_enum::enum_name(SystemConfig::evmc_revision);
// system configuration for ethereum-executor excess blob gas (EIP-4844 blob base fee)
constexpr static std::string_view SYSTEM_KEY_EXCESS_BLOB_GAS = magic_enum::enum_name(SystemConfig::excess_blob_gas);
// A3: eth-genesis (L2) chains store the FISCO genesis pin here instead of in B0's
// extraData — B0's extraData carries the genesis artifact bytes and enters the RLP hash.
constexpr static std::string_view INTERNAL_SYSTEM_KEY_ETH_GENESIS_DATA = "eth_genesis_data";
// clang-format on
constexpr static std::string_view PBFT_CONSENSUS_TYPE = "pbft";
constexpr static std::string_view RPBFT_CONSENSUS_TYPE = "rpbft";

// system config struct
using SystemConfigEntry = std::tuple<std::string, bcos::protocol::BlockNumber>;

// Minimum block gas limit for node-local startup / genesis validation. Lowered to
// go-ethereum's MinGasLimit (5000) so EEST state tests with small block gas limits
// (lowGasLimit: 80000) can be built at genesis; the previous 100000 floor rejected valid
// sub-100k Ethereum blocks. This is node-local only — the consensus-side runtime floor
// enforced by SystemConfigPrecompiled (bcos::precompiled::TX_GAS_LIMIT_MIN) deliberately
// stays at 100000 so existing chains are unaffected.
const unsigned TX_GAS_LIMIT_MIN = 5000;
const unsigned RPBFT_EPOCH_SEALER_NUM_MIN = 1;
const unsigned RPBFT_EPOCH_BLOCK_NUM_MIN = 1;
// get consensus node list type
constexpr static std::string_view CONSENSUS_SEALER =
    magic_enum::enum_name(consensus::Type::consensus_sealer);
constexpr static std::string_view CONSENSUS_OBSERVER =
    magic_enum::enum_name(consensus::Type::consensus_observer);
constexpr static std::string_view CONSENSUS_CANDIDATE_SEALER =
    magic_enum::enum_name(consensus::Type::consensus_candidate_sealer);

// get current state key
constexpr static std::string_view SYS_KEY_CURRENT_NUMBER = "current_number";
constexpr static std::string_view SYS_KEY_TOTAL_TRANSACTION_COUNT = "total_transaction_count";
constexpr static std::string_view SYS_KEY_ARCHIVED_NUMBER = "archived_block_number";
constexpr static std::string_view SYS_KEY_TOTAL_FAILED_TRANSACTION =
    "total_failed_transaction_count";

// sys table name
constexpr static std::string_view SYS_TABLES{"s_tables"};
constexpr static std::string_view SYS_CONSENSUS{"s_consensus"};
constexpr static std::string_view SYS_CONFIG{"s_config"};
constexpr static std::string_view SYS_CURRENT_STATE{"s_current_state"};
constexpr static std::string_view SYS_HASH_2_NUMBER{"s_hash_2_number"};
constexpr static std::string_view SYS_NUMBER_2_HASH{"s_number_2_hash"};
constexpr static std::string_view SYS_BLOCK_NUMBER_2_NONCES{"s_block_number_2_nonces"};
constexpr static std::string_view SYS_NUMBER_2_BLOCK_HEADER{"s_number_2_header"};
constexpr static std::string_view SYS_NUMBER_2_TXS{"s_number_2_txs"};
constexpr static std::string_view SYS_HASH_2_TX{"s_hash_2_tx"};
constexpr static std::string_view SYS_HASH_2_RECEIPT{"s_hash_2_receipt"};
constexpr static std::string_view DAG_TRANSFER{"/tables/dag_transfer"};
constexpr static std::string_view SMALLBANK_TRANSFER{"/tables/smallbank_transfer"};
constexpr static std::string_view SYS_CODE_BINARY{"s_code_binary"};
constexpr static std::string_view SYS_CONTRACT_ABI{"s_contract_abi"};
constexpr static std::string_view SYS_BALANCE_CALLER{"s_balance_caller"};

struct SYS_DIRECTORY
{
    static constexpr std::string_view USER_APPS = "/apps/";
    static constexpr std::string_view SYS_APPS = "/sys/";
};
// Table fields
struct ACCOUNT_TABLE_FIELDS
{
    static constexpr std::string_view CODE_HASH = "codeHash";
    static constexpr std::string_view CODE = "code";
    static constexpr std::string_view BALANCE = "balance";
    static constexpr std::string_view ABI = "abi";
    static constexpr std::string_view NONCE = "nonce";
    static constexpr std::string_view ALIVE = "alive";
    static constexpr std::string_view FROZEN = "frozen";
    static constexpr std::string_view SHARD = "shard";
};

enum ConsensusType : uint32_t
{
    PBFT_TYPE = 1,
    RPBFT_TYPE = 2,
};
struct CurrentState
{
    bcos::protocol::BlockNumber latestBlockNumber;
    bcos::protocol::BlockNumber archivedNumber;
    int64_t totalTransactionCount;
    int64_t totalFailedTransactionCount;
};

// parent=>children
using Parent2ChildListMap = std::map<std::string, std::vector<std::string>>;
// child=>parent
using Child2ParentMap = tbb::concurrent_unordered_map<std::string, std::string>;

constexpr static const char* const SYS_VALUE = "value";
constexpr static const char* const SYS_CONFIG_ENABLE_BLOCK_NUMBER = "enable_number";
constexpr static const char* const SYS_VALUE_AND_ENABLE_BLOCK_NUMBER = "value,enable_number";

enum LedgerError : int32_t
{
    SUCCESS = 0,
    OpenTableFailed = 3001,
    CallbackError = 3002,
    ErrorArgument = 3003,
    DecodeError = 3004,
    ErrorCommitBlock = 3005,
    CollectAsyncCallbackError = 3006,
    LedgerLockError = 3007,
    GetStorageError = 3008,
    EmptyEntry = 3009,
    UnknownError = 3010,
};
struct StorageState
{
    std::string nonce;
    std::string balance;
};
inline task::Task<void> readFromStorage(
    SystemConfigs& configs, auto& storage, protocol::BlockNumber blockNumber = INT64_MAX)
{
    auto keys = bcos::ledger::SystemConfigs::supportConfigs();
    auto entries = co_await storage2::readSome(
        storage, keys | ::ranges::views::transform([](std::string_view key) {
            return executor_v1::StateKeyView(ledger::SYS_CONFIG, key);
        }));

    for (auto&& [key, entry] : ::ranges::views::zip(keys, entries))
    {
        if (entry)
        {
            auto [value, enableNumber] =
                storage::serialize::decode<ledger::SystemConfigEntry>(entry->get());
            if (blockNumber >= enableNumber)
            {
                configs.set(key, value, enableNumber);
            }
        }
    }
}
}  // namespace bcos::ledger
