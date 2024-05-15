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
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>

using namespace bcostars;
using namespace bcostars::protocol;

struct EmptyReceiptHash : public bcos::error::Exception
{
};

void TransactionReceiptImpl::decode(bcos::bytesConstRef _receiptData)
{
    bcos::concepts::serialize::decode(_receiptData, *m_inner());
}

void TransactionReceiptImpl::encode(bcos::bytes& _encodedData) const
{
    bcos::concepts::serialize::encode(*m_inner(), _encodedData);
}

bcos::crypto::HashType TransactionReceiptImpl::hash() const
{
    if (m_inner()->dataHash.empty())
    {
        BOOST_THROW_EXCEPTION(EmptyReceiptHash{});
    }

    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

    return hashResult;
}
void bcostars::protocol::TransactionReceiptImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    bcos::concepts::hash::calculate(*m_inner(), hashImpl.hasher(), m_inner()->dataHash);
}

bcos::u256 TransactionReceiptImpl::gasUsed() const
{
    if (!m_inner()->data.gasUsed.empty())
    {
        return boost::lexical_cast<bcos::u256>(m_inner()->data.gasUsed);
    }
    return {};
}
int32_t bcostars::protocol::TransactionReceiptImpl::version() const
{
    return m_inner()->data.version;
}
std::string_view bcostars::protocol::TransactionReceiptImpl::contractAddress() const
{
    return m_inner()->data.contractAddress;
}
int32_t bcostars::protocol::TransactionReceiptImpl::status() const
{
    return m_inner()->data.status;
}
bcos::bytesConstRef bcostars::protocol::TransactionReceiptImpl::output() const
{
    return {(const unsigned char*)m_inner()->data.output.data(), m_inner()->data.output.size()};
}
gsl::span<const bcos::protocol::LogEntry> bcostars::protocol::TransactionReceiptImpl::logEntries()
    const
{
    if (m_logEntries.empty())
    {
        m_logEntries.reserve(m_inner()->data.logEntries.size());
        for (auto& it : m_inner()->data.logEntries)
        {
            auto bcosLogEntry = toBcosLogEntry(it);
            m_logEntries.emplace_back(std::move(bcosLogEntry));
        }
    }

    return {m_logEntries.data(), m_logEntries.size()};
}
bcos::protocol::LogEntries&& bcostars::protocol::TransactionReceiptImpl::takeLogEntries()
{
    if (m_logEntries.empty())
    {
        auto& innter = mutableInner();
        m_logEntries.reserve(innter.data.logEntries.size());
        for (auto& it : innter.data.logEntries)
        {
            auto bcosLogEntry = takeToBcosLogEntry(std::move(it));
            m_logEntries.push_back(std::move(bcosLogEntry));
        }
        return std::move(m_logEntries);
    }
    return std::move(m_logEntries);
}
bcos::protocol::BlockNumber bcostars::protocol::TransactionReceiptImpl::blockNumber() const
{
    return m_inner()->data.blockNumber;
}
std::string_view bcostars::protocol::TransactionReceiptImpl::effectiveGasPrice() const
{
    return m_inner()->data.effectiveGasPrice;
}
void bcostars::protocol::TransactionReceiptImpl::setEffectiveGasPrice(std::string effectiveGasPrice)
{
    m_inner()->data.effectiveGasPrice = std::move(effectiveGasPrice);
}
const bcostars::TransactionReceipt& bcostars::protocol::TransactionReceiptImpl::inner() const
{
    return *m_inner();
}
bcostars::TransactionReceipt& bcostars::protocol::TransactionReceiptImpl::mutableInner()
{
    return *m_inner();
}
void bcostars::protocol::TransactionReceiptImpl::setInner(const bcostars::TransactionReceipt& inner)
{
    *m_inner() = inner;
}
void bcostars::protocol::TransactionReceiptImpl::setInner(bcostars::TransactionReceipt&& inner)
{
    *m_inner() = std::move(inner);
}
std::function<bcostars::TransactionReceipt*()> const&
bcostars::protocol::TransactionReceiptImpl::innerGetter()
{
    return m_inner;
}
void bcostars::protocol::TransactionReceiptImpl::setLogEntries(
    std::vector<bcos::protocol::LogEntry> const& _logEntries)
{
    m_logEntries.clear();
    m_inner()->data.logEntries.clear();
    m_inner()->data.logEntries.reserve(_logEntries.size());

    for (const auto& it : _logEntries)
    {
        auto tarsLogEntry = toTarsLogEntry(it);
        m_inner()->data.logEntries.emplace_back(std::move(tarsLogEntry));
    }
}
std::string const& bcostars::protocol::TransactionReceiptImpl::message() const
{
    return m_inner()->message;
}
void bcostars::protocol::TransactionReceiptImpl::setMessage(std::string message)
{
    m_inner()->message = std::move(message);
}
std::string bcostars::protocol::TransactionReceiptImpl::toString() const
{
    std::stringstream ss;
    m_inner()->display(ss);
    return ss.str();
}
