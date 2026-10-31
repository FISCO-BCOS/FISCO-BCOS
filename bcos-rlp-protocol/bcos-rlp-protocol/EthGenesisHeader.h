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
 * @file EthGenesisHeader.h
 * @brief Project the [eth_genesis_header] artifact onto the pure Ethereum header domain
 * @date 2026/9/14
 */
#pragma once

#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include <algorithm>

namespace bcos::protocol
{
/// Project a chain-genesis anchor header ([eth_genesis_header] artifact) onto the pure
/// Ethereum header domain. Single source of truth for the field mapping: used by the
/// EL-sync initializer (download anchor + RLPx handshake genesis pin) and by the
/// eth-sync-check genesis-hash verification. Ledger::applyEthGenesisHeader maps the
/// same fields onto the internal (millisecond) BlockHeader domain and must stay
/// field-aligned with this function. Fork-gated fields copy through only when the
/// genesis header carries them (nullopt stays nullopt), so the result re-encodes to
/// the byte-exact genesis RLP.
inline EthBlockHeaderData toEthBlockHeaderData(bcos::ledger::EthGenesisHeader const& _genesis)
{
    EthBlockHeaderData h;
    h.parentInfo.blockHash = _genesis.m_parentHash;
    h.uncleHash = _genesis.m_sha3Uncles;
    h.coinbase = _genesis.m_miner;
    h.stateRoot = _genesis.m_stateRoot;
    h.txsRoot = _genesis.m_transactionsRoot;
    h.receiptsRoot = _genesis.m_receiptsRoot;
    std::copy(_genesis.m_logsBloom.begin(), _genesis.m_logsBloom.end(), h.logsBloom.begin());
    h.difficulty = _genesis.m_difficulty;
    h.gasLimit = _genesis.m_gasLimit;
    h.gasUsed = _genesis.m_gasUsed;
    // NodeConfig's [eth_genesis_header] parser forces m_number to 0; copy it through
    // instead of hard-coding so the mapping stays honest if that ever changes.
    h.number = _genesis.m_number;
    h.timestamp = _genesis.m_timestamp;  // seconds — the Ethereum header domain
    h.extraData = _genesis.m_extraData;
    std::copy(_genesis.m_mixHash.begin(), _genesis.m_mixHash.end(), h.prevRandao.begin());
    std::copy(_genesis.m_nonce.begin(), _genesis.m_nonce.end(), h.nonce.begin());
    h.baseFee = _genesis.m_baseFeePerGas;
    h.withdrawalsHash = _genesis.m_withdrawalsRoot;
    h.blobGasUsed = _genesis.m_blobGasUsed;
    h.excessBlobGas = _genesis.m_excessBlobGas;
    h.parentBeaconRoot = _genesis.m_parentBeaconBlockRoot;
    h.requestsHash = _genesis.m_requestsHash;
    return h;
}
}  // namespace bcos::protocol
