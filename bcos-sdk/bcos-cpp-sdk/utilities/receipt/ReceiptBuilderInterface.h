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
 * @file TransactionBuilderInterface.h
 * @author: octopus
 * @date 2022-01-13
 */
#pragma once
#include <bcos-cpp-sdk/utilities/crypto/Common.h>
#include <bcos-cpp-sdk/utilities/receipt/ReceiptUtils.h>
#include <bcos-crypto/signature/key/KeyPair.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-utilities/Common.h>

namespace bcos::cppsdk::utilities
{
using CryptoType = crypto::KeyPairType;

class ReceiptBuilderInterface
{
public:
    using Ptr = std::shared_ptr<ReceiptBuilderInterface>;
    using ConstPtr = std::shared_ptr<const ReceiptBuilderInterface>;
    using UniquePtr = std::unique_ptr<ReceiptBuilderInterface>;

public:
    virtual ~ReceiptBuilderInterface() = default;

public:
    /**
     * @brief Create a Transaction Data object
     * @return bcostars::TransactionDataUniquePtr
     */
    virtual bcostars::ReceiptDataUniquePtr createReceiptData(const std::string& _gasUsed,
        const std::string& _contractAddress, const bcos::bytes& _output, int64_t _blockNumber) = 0;

    /**
     * @brief Create a Transaction Data object
     * @param _json
     *              version: number
     *              gasUsed: string
     *              contractAddress: string
     *              status: number
     *              output: hex string
     *              logEntries: array<LogEntry>
     *              blockNumber: number
     *              logEntry:
     *                       address: string
     *                       topic: array<hex string>
     *                       data: hex string
     * @return bcostars::TransactionDataUniquePtr
     */
    virtual bcostars::ReceiptDataUniquePtr createReceiptDataWithJson(const std::string& _json) = 0;

    /**
     * @brief calculate receipt data hash with json
     *
     * @param _cryptoType
     * @param _json
     * @return crypto::HashType
     */
    virtual crypto::HashType calculateReceiptDataHashWithJson(
        bcos::cppsdk::utilities::CryptoType _cryptoType, const std::string& _json) = 0;

    /**
     * @brief
     *
     * @param _cryptoType
     * @param _transactionData
     * @return crypto::HashType
     */
    virtual crypto::HashType calculateReceiptDataHash(
        CryptoType _cryptoType, const bcostars::TransactionReceiptData& _receiptData) = 0;

    /**
     * @brief
     *
     * @param _transaction
     * @return bytesConstPtr
     */
    virtual bytesConstPtr encodeReceipt(const bcostars::TransactionReceiptData& _receipt) = 0;

    /**
     * @brief decode receipt data from encoded bytes
     *
     * @param _receiptBytes encoded bytes
     * @return receipt data json string
     */
    virtual std::string decodeReceiptDataToJsonObj(const bcos::bytes& _receiptBytes) = 0;
};
}  // namespace bcos::cppsdk::utilities