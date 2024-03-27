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
#include "../Common.h"
#include "../impl/TarsHashable.h"
#include "TransactionReceiptImpl.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-concepts/Hash.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <utility>


namespace bcostars::protocol
{
class TransactionReceiptFactoryImpl : public bcos::protocol::TransactionReceiptFactory
{
public:
    TransactionReceiptFactoryImpl(const TransactionReceiptFactoryImpl&) = default;
    TransactionReceiptFactoryImpl(TransactionReceiptFactoryImpl&&) = default;
    TransactionReceiptFactoryImpl& operator=(const TransactionReceiptFactoryImpl&) = default;
    TransactionReceiptFactoryImpl& operator=(TransactionReceiptFactoryImpl&&) = default;
    TransactionReceiptFactoryImpl(const bcos::crypto::CryptoSuite::Ptr& cryptoSuite)
      : m_hashImpl(cryptoSuite->hashImpl())
    {}
    ~TransactionReceiptFactoryImpl() override = default;

    TransactionReceiptImpl::Ptr createReceipt(bcos::bytesConstRef _receiptData) const override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });

        transactionReceipt->decode(_receiptData);

        auto& inner = transactionReceipt->mutableInner();
        if (inner.dataHash.empty())
        {
            // Update the hash field
            bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);

            BCOS_LOG(TRACE) << LOG_BADGE("createReceipt")
                            << LOG_DESC("recalculate receipt dataHash");
        }

        return transactionReceipt;
    }

    TransactionReceiptImpl::Ptr createReceipt(bcos::bytes const& _receiptData) const override
    {
        return createReceipt(bcos::ref(_receiptData));
    }

    TransactionReceiptImpl::Ptr createReceipt(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output,
        bcos::protocol::BlockNumber blockNumber) const override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
        auto& inner = transactionReceipt->mutableInner();
        inner.data.version = 0;
        // inner.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
        inner.data.gasUsed = gasUsed.backend().str({}, {});
        inner.data.contractAddress = std::move(contractAddress);
        inner.data.status = status;
        inner.data.output.assign(output.begin(), output.end());
        transactionReceipt->setLogEntries(logEntries);
        inner.data.blockNumber = blockNumber;

        bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);
        return transactionReceipt;
    }

    TransactionReceiptImpl::Ptr createReceipt2(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output, bcos::protocol::BlockNumber blockNumber,
        std::string effectiveGasPrice = "1",
        bcos::protocol::TransactionVersion version = bcos::protocol::TransactionVersion::V1_VERSION,
        bool withHash = true) const override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
        auto& inner = transactionReceipt->mutableInner();
        inner.data.version = static_cast<uint32_t>(version);
        inner.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
        inner.data.contractAddress = std::move(contractAddress);
        inner.data.status = status;
        inner.data.output.assign(output.begin(), output.end());
        transactionReceipt->setLogEntries(logEntries);
        inner.data.blockNumber = blockNumber;
        inner.data.effectiveGasPrice = std::move(effectiveGasPrice);

        // Update the hash field
        bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);
        return transactionReceipt;
    }

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
};
}  // namespace bcostars::protocol
