/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file ReceiptResponse.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once
#include "Bloom.h"


#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/web3jsonrpc/model/Log.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

#include <ostream>

namespace bcos::rpc
{
struct ReceiptResponse
{
    ReceiptResponse() = default;
    explicit ReceiptResponse(bcos::protocol::TransactionReceipt::ConstPtr&& receipt)
    {
        status = (receipt->status() == 0 ? 1 : 0);
        txHash = receipt->hash();
        effectiveGasPrice = std::string(receipt->effectiveGasPrice());
        gasUsed = receipt->gasUsed();
        blockNumber = receipt->blockNumber();
        if (!receipt->contractAddress().empty())
        {
            contractAddress = Address(std::string(receipt->contractAddress()));
        }
        output = receipt->output().toBytes();
        auto mutableReceipt = const_cast<bcos::protocol::TransactionReceipt*>(receipt.get());
        auto receiptLog = mutableReceipt->takeLogEntris();
        this->logs.reserve(receiptLog.size());
        for (size_t i = 0; i < receiptLog.size(); i++)
        {
            rpc::Log log{.address = std::move(receiptLog[i].takeAddress()),
                .topics = std::move(receiptLog[i].takeTopics()),
                .data = std::move(receiptLog[i].takeData())};
            log.logIndex = i;
            log.number = blockNumber;
            this->logs.push_back(std::move(log));
        }
        logsBloom = getLogsBloom(logs);
    }
    int32_t status{0};
    uint32_t txIndex{0};
    crypto::HashType txHash{};
    crypto::HashType blockHash{};
    // Quantity
    protocol::BlockNumber blockNumber{0};
    std::string from{};
    std::string to{};
    u256 cumulativeGasUsed{0};
    std::string effectiveGasPrice{};
    u256 gasUsed{0};
    // only has value in create tx
    std::optional<Address> contractAddress{};
    TransactionType type = TransactionType::Legacy;
    Logs logs{};

    Bloom logsBloom{};
    bcos::bytes output{};

    void updateTxInfo(bcos::protocol::Transaction const& tx)
    {
        // txHash = tx.hash();
        // from = std::string(tx.sender());
        // to = tx.to().empty() ? "" : std::string(tx.to());
        // if (!tx.extraTransactionBytes().empty())
        // {
        //     if (auto firstByte = tx.extraTransactionBytes()[0];
        //         firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
        //     {
        //         type = static_cast<TransactionType>(firstByte);
        //     }
        // }
    }

    // TODO: update block info
    void updateBlockInfo(bcos::protocol::Block const& block) {}

    // TODO: to json
    void toJson(Json::Value&) {}
};

}  // namespace bcos::rpc