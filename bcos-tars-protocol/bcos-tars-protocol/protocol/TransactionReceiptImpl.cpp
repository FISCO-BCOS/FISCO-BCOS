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
 * @brief tars implementation for TransactionReceipt
 * @file TransactionReceiptImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */
#include "TransactionReceiptImpl.h"

using namespace bcostars;
using namespace bcostars::protocol;

void TransactionReceiptImpl::decode(bcos::bytesConstRef _receiptData)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_receiptData.data(), _receiptData.size());

    m_inner()->readFrom(input);
}

void TransactionReceiptImpl::encode(bcos::bytes& _encodedData) const
{
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    m_inner()->writeTo(output);
    output.getByteBuffer().swap(_encodedData);
}

// Note: not thread-safe
bcos::crypto::HashType TransactionReceiptImpl::hash() const
{
    if (!m_inner()->dataHash.empty())
    {
        return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner()->dataHash.data()));
    }
    auto hashImpl = m_cryptoSuite->hashImpl();
    auto hasher = hashImpl->hasher();
    auto const& hashFields = m_inner()->data;
    long version = boost::asio::detail::socket_ops::host_to_network_long(hashFields.version);
    hasher.update(version);
    hasher.update(hashFields.gasUsed);
    hasher.update(hashFields.contractAddress);
    long status = boost::asio::detail::socket_ops::host_to_network_long(hashFields.status);
    hasher.update(status);
    hasher.update(hashFields.output);
    // vector<LogEntry> logEntries: 6
    for (auto const& log : hashFields.logEntries)
    {
        hasher.update(log.address);
        for (auto const& topicItem : log.topic)
        {
            hasher.update(topicItem);
        }
        hasher.update(log.data);
    }
    long blockNumber =
        boost::asio::detail::socket_ops::host_to_network_long(hashFields.blockNumber);
    hasher.update(blockNumber);
    // calculate the hash
    bcos::crypto::HashType hashResult;
    hasher.final(hashResult);
    m_inner()->dataHash.assign(hashResult.begin(), hashResult.end());
    return hashResult;
}

bcos::u256 TransactionReceiptImpl::gasUsed() const
{
    if (!m_inner()->data.gasUsed.empty())
    {
        return boost::lexical_cast<bcos::u256>(m_inner()->data.gasUsed);
    }
    return {};
}
