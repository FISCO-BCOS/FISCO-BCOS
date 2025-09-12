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
 * @brief tars implementation for TransactionReceiptFactory
 * @file TransactionReceiptFactoryImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */
#pragma once
#include "TransactionReceiptImpl.h"
#include <bcos-concepts/Hash.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>


namespace bcostars::protocol
{
class TransactionReceiptFactoryImpl : public bcos::protocol::TransactionReceiptFactory
{
public:
    TransactionReceiptFactoryImpl(const TransactionReceiptFactoryImpl&) = default;
    TransactionReceiptFactoryImpl(TransactionReceiptFactoryImpl&&) = default;
    TransactionReceiptFactoryImpl& operator=(const TransactionReceiptFactoryImpl&) = default;
    TransactionReceiptFactoryImpl& operator=(TransactionReceiptFactoryImpl&&) = default;
    TransactionReceiptFactoryImpl(const bcos::crypto::CryptoSuite::Ptr& cryptoSuite);
    ~TransactionReceiptFactoryImpl() override = default;

    TransactionReceiptImpl::Ptr createReceipt() const override;
    TransactionReceiptImpl::Ptr createReceipt(
        bcos::protocol::TransactionReceipt& input) const override;
    TransactionReceiptImpl::Ptr createReceipt(bcos::bytesConstRef _receiptData) const override;
    TransactionReceiptImpl::Ptr createReceipt(bcos::bytes const& _receiptData) const override;
    TransactionReceiptImpl::Ptr createReceipt(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output,
        bcos::protocol::BlockNumber blockNumber) const override;
    TransactionReceiptImpl::Ptr createReceipt2(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output, bcos::protocol::BlockNumber blockNumber,
        std::string effectiveGasPrice = "1",
        bcos::protocol::TransactionVersion version = bcos::protocol::TransactionVersion::V1_VERSION,
        bool withHash = true) const override;

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
};
}  // namespace bcostars::protocol
