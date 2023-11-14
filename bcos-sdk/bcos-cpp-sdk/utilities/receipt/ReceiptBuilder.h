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
 * @file ReceiptBuilder.h
 * @author: kyonGuo
 * @date 2022/12/7
 */

#pragma once
#include "ReceiptBuilderInterface.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>

namespace bcos::cppsdk::utilities
{
class ReceiptBuilder : public ReceiptBuilderInterface
{
public:
    ~ReceiptBuilder() override = default;
    bcostars::ReceiptDataUniquePtr createReceiptData(const std::string& _gasUsed,
        const std::string& _contractAddress, const bcos::bytes& _output,
        int64_t _blockNumber) override;
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
    bcostars::ReceiptDataUniquePtr createReceiptDataWithJson(const std::string& _json) override;

    crypto::HashType calculateReceiptDataHashWithJson(
        bcos::cppsdk::utilities::CryptoType _cryptoType, const std::string& _json) override;

    crypto::HashType calculateReceiptDataHash(
        CryptoType _cryptoType, const bcostars::TransactionReceiptData& _receiptData) override;
    bytesConstPtr encodeReceipt(const bcostars::TransactionReceiptData& _receipt) override;
    std::string decodeReceiptDataToJsonObj(const bytes& _receiptBytes) override;

private:
    bcos::crypto::CryptoSuite::UniquePtr m_ecdsaCryptoSuite =
        std::make_unique<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);

    bcos::crypto::CryptoSuite::UniquePtr m_smCryptoSuite =
        std::make_unique<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::SM3>(),
            std::make_shared<bcos::crypto::SM2Crypto>(), nullptr);
};
}  // namespace bcos::cppsdk::utilities
