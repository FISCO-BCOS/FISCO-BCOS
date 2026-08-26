/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file genesisData.h
 * @author: wenlinli
 * @date 2022-10-24
 */

#pragma once

#include "Features.h"
#include "LedgerConfig.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-tool/VersionConverter.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


namespace bcos::ledger
{

struct FeatureSet
{
    Features::Flag flag{};
    int enable{};
};

struct Alloc
{
    using State = std::pair<std::string, std::string>;

    std::string address;
    u256 balance;
    std::string nonce;
    std::string code;
    std::vector<State> storage;
};

// B0 full Ethereum genesis header, sourced field-by-field from the merged
// genesis artifact (op-deployer base + FISCO overlay). Parsed from the
// [eth_genesis_header] section of config.genesis; when the section is present
// the CORE fields are REQUIRED (NodeConfig fail-fasts on the first missing
// key) while the fork-gated fields are OPTIONAL (absent key = field does not
// exist on this chain's genesis, and is left off the RLP).
// m_hash is a checksum, not an input: Ledger::buildGenesisBlock recomputes
// keccak256(rlp(header)) from the other 21 fields and refuses to start if it
// differs. m_timestamp is in SECONDS (the Ethereum header domain);
// Ledger::applyEthGenesisHeader multiplies by 1000 when projecting it onto the
// internal BlockHeader, which stores milliseconds like every other block.
struct EthGenesisHeader
{
    crypto::HashType m_parentHash;
    crypto::HashType m_sha3Uncles;
    Address m_miner;
    crypto::HashType m_stateRoot;
    crypto::HashType m_transactionsRoot;
    crypto::HashType m_receiptsRoot;
    bytes m_logsBloom;  // exactly 256 bytes
    u256 m_difficulty;
    int64_t m_number{};
    u256 m_gasLimit;
    u256 m_gasUsed;
    int64_t m_timestamp{};  // seconds
    bytes m_extraData;      // artifact extraData (Jovian 17-byte format on our chain)
    h256 m_mixHash;
    h64 m_nonce;
    // Fork-gated fields are OPTIONAL: on a pre-Cancun chain (e.g. Sepolia's
    // London-era genesis) the corresponding header fields do not exist and the
    // keys are omitted from [eth_genesis_header]. A nullopt field is left off
    // the RLP re-encoding, which is what makes the computed genesis hash
    // byte-exact for chains whose genesis predates those forks. NodeConfig
    // parses each key with get_optional: present key -> has_value, absent key
    // -> nullopt. On an L2 chain every key is present (the artifact converter
    // emits all 21), so the 21-field encoding is unchanged.
    std::optional<u256> m_baseFeePerGas;
    std::optional<crypto::HashType> m_withdrawalsRoot;
    std::optional<u256> m_blobGasUsed;
    std::optional<u256> m_excessBlobGas;
    std::optional<crypto::HashType> m_parentBeaconBlockRoot;
    std::optional<crypto::HashType> m_requestsHash;
    crypto::HashType m_hash;  // expected keccak256(rlp(header)); checked, never trusted
};

class GenesisConfig
{
public:
    using Ptr = std::shared_ptr<GenesisConfig>;

    // chain config
    bool m_smCrypto{};
    std::string m_chainID;
    std::string m_groupID;

    // web3 chain config
    std::string m_web3ChainID;

    // consensus config
    std::string m_consensusType;
    uint64_t m_txCountLimit = 1000;
    uint64_t m_leaderSwitchPeriod = 1;

    // version config
    uint32_t m_compatibilityVersion = static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION);

    // tx config
    uint64_t m_txGasLimit = 3000000000;
    // tx gas price (base fee per gas); consumed by the v2 Ethereum executor as base_fee
    // (BCOS2Evmone::blockHeaderToBlockInfo). Seeded into SYS_CONFIG/tx_gas_price at genesis.
    std::string m_txGasPrice = "0x0";
    // excess blob gas (EIP-4844); consumed by the v2 Ethereum executor to derive the blob
    // base fee. Seeded into SYS_CONFIG/excess_blob_gas at genesis when set.
    std::optional<uint64_t> m_excessBlobGas;
    // executorConfig
    // WASM support was removed; this field is retained and permanently false because
    // generateGenesisData() serializes it into the genesis string. Dropping it would
    // change the genesis data of every existing chain and break node admission.
    // It is no longer parsed from executor.is_wasm.
    bool m_isWasm{};
    bool m_isAuthCheck = true;
    std::string m_authAdminAccount;
    bool m_isSerialExecute = true;
    int m_executorVersion = 0;

    // EVMC revision config (consumed by ethereum-executor, executor_version=2).
    // m_evmcRevision: explicit single revision applied to all blocks (when set).
    // m_evmcRevisionForks: block height -> revision fork transitions.
    std::optional<evmc_revision> m_evmcRevision;
    std::map<protocol::BlockNumber, evmc_revision> m_evmcRevisionForks;

    // rpbft config
    int64_t m_epochSealerNum = 4;
    int64_t m_epochBlockNum = 1000;

    std::vector<FeatureSet> m_features;
    std::vector<Alloc> m_allocs;

    // Present iff config.genesis carries an [eth_genesis_header] section
    // (L2 mode, B0 built as a full Ethereum header). Absent on every legacy
    // chain — Ledger then keeps the native Tars genesis-header path.
    std::optional<EthGenesisHeader> m_ethGenesisHeader;

};  // namespace genesisConfig
}  // namespace bcos::ledger
