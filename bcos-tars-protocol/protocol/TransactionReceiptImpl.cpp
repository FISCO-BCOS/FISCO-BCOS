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
    auto hashContext = hashImpl->init();
    auto const& hashFields = m_inner()->data;
    // int version: 1
    long version = boost::asio::detail::socket_ops::host_to_network_long(hashFields.version);
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)(&version), sizeof(version) / sizeof(uint8_t)));
    // string gasUsed: 2
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.gasUsed.data(), hashFields.gasUsed.size()));
    // string contractAddress: 3
    hashImpl->update(
        hashContext, bcos::bytesConstRef((bcos::byte*)hashFields.contractAddress.data(),
                         hashFields.contractAddress.size()));
    // int status: 4
    long status = boost::asio::detail::socket_ops::host_to_network_long(hashFields.status);
    hashImpl->update(
        hashContext, bcos::bytesConstRef((bcos::byte*)(&status), sizeof(status) / sizeof(uint8_t)));
    // vector<byte> output: 5
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.output.data(), hashFields.output.size()));
    // vector<LogEntry> logEntries: 6
    for (auto const& log : hashFields.logEntries)
    {
        // string address: 1
        hashImpl->update(
            hashContext, bcos::bytesConstRef((bcos::byte*)log.address.data(), log.address.size()));
        // vector<vector<byte>> topic: 2
        for (auto const& topicItem : log.topic)
        {
            hashImpl->update(
                hashContext, bcos::bytesConstRef((bcos::byte*)topicItem.data(), topicItem.size()));
        }
        // vector<byte> data: 3
        hashImpl->update(
            hashContext, bcos::bytesConstRef((bcos::byte*)log.data.data(), log.data.size()));
    }
    // long blockNumber: 7
    long blockNumber =
        boost::asio::detail::socket_ops::host_to_network_long(hashFields.blockNumber);
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)(&blockNumber), sizeof(blockNumber) / sizeof(uint8_t)));
    // string stateRoot: 8
    if (hashFields.version >= (int32_t)bcos::protocol::Version::V3_0_VERSION)
    {
        hashImpl->update(hashContext, bcos::bytesConstRef((bcos::byte*)hashFields.stateRoot.data(),
                                          hashFields.stateRoot.size()));
    }
    // calculate the hash
    auto hashResult = hashImpl->final(hashContext);
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


void TransactionReceiptImpl::setStateRoot(bcos::crypto::HashType const& _stateRoot)
{
    m_inner()->data.stateRoot.assign(_stateRoot.begin(), _stateRoot.end());
    std::vector<tars::Char> emptyBuffer;
    m_inner()->dataHash.swap(emptyBuffer);
}

void TransactionReceiptImpl::setVersion(int32_t _version)
{
    m_inner()->data.version = _version;
    std::vector<tars::Char> emptyBuffer;
    m_inner()->dataHash.swap(emptyBuffer);
}