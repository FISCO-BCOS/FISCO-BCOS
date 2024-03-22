/*
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
 * @file Common.h
 * @author: octopus
 * @date 2021-07-02
 */
#pragma once
#include <bcos-framework/Common.h>
#include <iostream>
#include <memory>

#define RPC_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("RPC")

namespace bcos::rpc
{
enum AMOPClientMessageType
{
    AMOP_SUBTOPIC = 0x110,   // 272
    AMOP_REQUEST = 0x111,    // 273
    AMOP_BROADCAST = 0x112,  // 274
    AMOP_RESPONSE = 0x113    // 275
};

namespace eth
{
constexpr std::string_view web3_clientVersion{"web3_clientVersion"};
constexpr std::string_view web3_sha3{"web3_sha3"};
constexpr std::string_view net_version{"net_version"};
constexpr std::string_view net_listening{"net_listening"};
constexpr std::string_view net_peerCount{"net_peerCount"};
constexpr std::string_view eth_protocolVersion{"eth_protocolVersion"};
constexpr std::string_view eth_syncing{"eth_syncing"};
constexpr std::string_view eth_coinbase{"eth_coinbase"};
constexpr std::string_view eth_chainId{"eth_chainId"};
constexpr std::string_view eth_mining{"eth_mining"};
constexpr std::string_view eth_hashrate{"eth_hashrate"};
constexpr std::string_view eth_gasPrice{"eth_gasPrice"};
constexpr std::string_view eth_accounts{"eth_accounts"};
constexpr std::string_view eth_blockNumber{"eth_blockNumber"};
constexpr std::string_view eth_getBalance{"eth_getBalance"};
constexpr std::string_view eth_getStorageAt{"eth_getStorageAt"};
constexpr std::string_view eth_getTransactionCount{"eth_getTransactionCount"};
constexpr std::string_view eth_getBlockTxCountByHash{"eth_getBlockTransactionCountByHash"};
constexpr std::string_view eth_getBlockTxCountByNumber{"eth_getBlockTransactionCountByNumber"};
constexpr std::string_view eth_getUncleCountByBlockHash{"eth_getUncleCountByBlockHash"};
constexpr std::string_view eth_getUncleCountByBlockNumber{"eth_getUncleCountByBlockNumber"};
constexpr std::string_view eth_getCode{"eth_getCode"};
constexpr std::string_view eth_sign{"eth_sign"};
constexpr std::string_view eth_sendTransaction{"eth_sendTransaction"};
constexpr std::string_view eth_signTransaction{"eth_signTransaction"};
constexpr std::string_view eth_sendRawTransaction{"eth_sendRawTransaction"};
constexpr std::string_view eth_call{"eth_call"};
constexpr std::string_view eth_estimateGas{"eth_estimateGas"};
constexpr std::string_view eth_getBlockByHash{"eth_getBlockByHash"};
constexpr std::string_view eth_getBlockByNumber{"eth_getBlockByNumber"};
constexpr std::string_view eth_getTransactionByHash{"eth_getTransactionByHash"};
constexpr std::string_view eth_getTxByBlockHashAndIndex{"eth_getTransactionByBlockHashAndIndex"};
constexpr std::string_view eth_getTxByBlockNumAndIndex{"eth_getTransactionByBlockNumberAndIndex"};
constexpr std::string_view eth_getTransactionReceipt{"eth_getTransactionReceipt"};
constexpr std::string_view eth_getUncleByBlockHashAndIndex{"eth_getUncleByBlockHashAndIndex"};
constexpr std::string_view eth_getUncleByBlockNumberAndIndex{"eth_getUncleByBlockNumberAndIndex"};
constexpr std::string_view eth_newFilter{"eth_newFilter"};
constexpr std::string_view eth_newBlockFilter{"eth_newBlockFilter"};
constexpr std::string_view eth_newPendingTransactionFilter{"eth_newPendingTransactionFilter"};
constexpr std::string_view eth_uninstallFilter{"eth_uninstallFilter"};
constexpr std::string_view eth_getFilterChanges{"eth_getFilterChanges"};
constexpr std::string_view eth_getFilterLogs{"eth_getFilterLogs"};
constexpr std::string_view eth_getLogs{"eth_getLogs"};

// constexpr std::string_view eth_createAccessList{"eth_createAccessList"};
// constexpr std::string_view eth_maxPriorityFeePerGas{"eth_maxPriorityFeePerGas"};
}  // namespace eth
}  // namespace bcos::rpc