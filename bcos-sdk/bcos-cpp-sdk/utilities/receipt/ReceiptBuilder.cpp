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
 * @file ReceiptBuilder.cpp
 * @author: kyonGuo
 * @date 2022/12/7
 */

#include "ReceiptBuilder.h"
#include "bcos-tars-protocol/impl/TarsHashable.h"

bcostars::ReceiptDataUniquePtr bcos::cppsdk::utilities::ReceiptBuilder::createReceiptData(
    const std::string& _gasUsed, const std::string& _contractAddress, const bcos::bytes& _output,
    int64_t _blockNumber)
{
    auto _receipt = std::make_unique<bcostars::TransactionReceiptData>();
    _receipt->version = 0;
    _receipt->gasUsed = _gasUsed;
    _receipt->contractAddress = _contractAddress;
    _receipt->status = 0;
    _receipt->output.insert(_receipt->output.cbegin(), _output.begin(), _output.end());
    _receipt->logEntries.clear();
    _receipt->blockNumber = _blockNumber;
    return _receipt;
}

bcostars::ReceiptDataUniquePtr bcos::cppsdk::utilities::ReceiptBuilder::createReceiptDataWithJson(
    const std::string& _json)
{
    auto _receipt = TarsReceiptDataReadFromJsonString(_json);
    return _receipt;
}

bcos::crypto::HashType bcos::cppsdk::utilities::ReceiptBuilder::calculateReceiptDataHashWithJson(
    bcos::cppsdk::utilities::CryptoType _cryptoType, const std::string& _json)
{
    auto _receipt = createReceiptDataWithJson(_json);
    return calculateReceiptDataHash(_cryptoType, *_receipt);
}

bcos::crypto::HashType bcos::cppsdk::utilities::ReceiptBuilder::calculateReceiptDataHash(
    bcos::cppsdk::utilities::CryptoType _cryptoType,
    const bcostars::TransactionReceiptData& _receiptData)
{
    bcos::crypto::CryptoSuite* cryptoSuite = nullptr;
    if (_cryptoType == bcos::crypto::KeyPairType::SM2 ||
        _cryptoType == bcos::crypto::KeyPairType::HsmSM2)
    {
        cryptoSuite = &*m_smCryptoSuite;
    }
    else
    {
        cryptoSuite = &*m_ecdsaCryptoSuite;
    }
    crypto::HashType receiptHash;
    bcos::concepts::hash::calculate(_receiptData, cryptoSuite->hashImpl()->hasher(), receiptHash);
    return receiptHash;
}

bcos::bytesConstPtr bcos::cppsdk::utilities::ReceiptBuilder::encodeReceipt(
    const bcostars::TransactionReceiptData& _receipt)
{
    tars::TarsOutputStream<tars::BufferWriter> output;
    _receipt.writeTo(output);

    auto buffer = std::make_shared<bcos::bytes>();
    buffer->assign(output.getBuffer(), output.getBuffer() + output.getLength());
    return buffer;
}
std::string bcos::cppsdk::utilities::ReceiptBuilder::decodeReceiptDataToJsonObj(
    const bcos::bytes& _receiptBytes)
{
    tars::TarsInputStream<tars::BufferReader> inputStream;
    inputStream.setBuffer((const char*)_receiptBytes.data(), _receiptBytes.size());
    bcostars::TransactionReceiptData receiptData;
    receiptData.readFrom(inputStream);
    auto receiptDataJson = TarsReceiptDataWriteToJsonString(receiptData);
    return receiptDataJson;
}
