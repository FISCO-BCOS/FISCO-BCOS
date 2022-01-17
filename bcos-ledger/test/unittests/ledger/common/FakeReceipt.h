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
 * @file FakeReceipt.h
 * @author: kyonRay
 * @date 2021-05-06
 */

#pragma once
#include "bcos-protocol/protobuf/PBTransactionReceiptFactory.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
inline LogEntriesPtr fakeLogEntries(Hash::Ptr _hashImpl, size_t _size)
{
    LogEntriesPtr logEntries = std::make_shared<LogEntries>();
    for (size_t i = 0; i < _size; i++)
    {
        auto topic = _hashImpl->hash(std::to_string(i));
        h256s topics;
        topics.push_back(topic);
        auto address = right160(topic).asBytes();
        bytes output = topic.asBytes();
        LogEntry logEntry(address, topics, output);
        logEntries->push_back(logEntry);
    }
    return logEntries;
}

inline TransactionReceipt::Ptr testPBTransactionReceipt(
    CryptoSuite::Ptr _cryptoSuite, BlockNumber _blockNumber)
{
    auto hashImpl = _cryptoSuite->hashImpl();
    u256 gasUsed = 12343242342 + random();
    auto contractAddress = "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f";
    auto logEntries = fakeLogEntries(hashImpl, 2);
    TransactionStatus status = TransactionStatus::BadJumpDestination;
    auto contractAddressBytes = toAddress(contractAddress);
    bytes output = contractAddressBytes.asBytes();
    for (int i = 0; i < (random() % 9); i++)
    {
        output += contractAddressBytes.asBytes();
    }
    auto factory = std::make_shared<PBTransactionReceiptFactory>(_cryptoSuite);
    auto receipt = factory->createReceipt(
        gasUsed, contractAddress, logEntries, (int32_t)status, output, _blockNumber);
    return receipt;
}

inline ReceiptsPtr fakeReceipts(int _size)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    ReceiptsPtr receipts = std::make_shared<Receipts>();
    for (int i = 0; i < _size; ++i)
    {
        receipts->emplace_back(testPBTransactionReceipt(cryptoSuite, i + 1));
    }
    return receipts;
}
}  // namespace test
}  // namespace bcos
