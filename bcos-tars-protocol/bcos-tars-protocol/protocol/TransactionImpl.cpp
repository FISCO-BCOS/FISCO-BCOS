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
 * @brief tars implementation for Transaction
 * @file TransactionImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */

#include "TransactionImpl.h"
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include "bcos-concepts/Exception.h"
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>
#include <boost/endian/conversion.hpp>
#include <boost/throw_exception.hpp>

using namespace bcostars;
using namespace bcostars::protocol;

struct EmptyTransactionHash : public bcos::error::Exception
{
};

void TransactionImpl::decode(bcos::bytesConstRef _txData)
{
    bcos::concepts::serialize::decode(_txData, *m_inner());
}

void TransactionImpl::encode(bcos::bytes& txData) const
{
    bcos::concepts::serialize::encode(*m_inner(), txData);
}

bcos::crypto::HashType TransactionImpl::hash() const
{
    if (m_inner()->dataHash.empty() && m_inner()->extraTransactionHash.empty())
    {
        BOOST_THROW_EXCEPTION(EmptyTransactionHash{});
    }

    if (type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transacion))
    {
        bcos::crypto::HashType hashResult((bcos::byte*)m_inner()->extraTransactionHash.data(),
            m_inner()->extraTransactionHash.size());
        return hashResult;
    }
    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

    return hashResult;
}

void bcostars::protocol::TransactionImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    bcos::concepts::hash::calculate(*m_inner(), hashImpl.hasher(), m_inner()->dataHash);
}

const std::string& TransactionImpl::nonce() const
{
    return m_inner()->data.nonce;
}

bcos::bytesConstRef TransactionImpl::input() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.input.data()),
        m_inner()->data.input.size()};
}
int32_t bcostars::protocol::TransactionImpl::version() const
{
    return m_inner()->data.version;
}
std::string_view bcostars::protocol::TransactionImpl::chainId() const
{
    return m_inner()->data.chainID;
}
std::string_view bcostars::protocol::TransactionImpl::groupId() const
{
    return m_inner()->data.groupID;
}
int64_t bcostars::protocol::TransactionImpl::blockLimit() const
{
    return m_inner()->data.blockLimit;
}
void bcostars::protocol::TransactionImpl::setNonce(std::string _n)
{
    m_inner()->data.nonce = std::move(_n);
}
std::string_view bcostars::protocol::TransactionImpl::to() const
{
    return m_inner()->data.to;
}
std::string_view bcostars::protocol::TransactionImpl::abi() const
{
    return m_inner()->data.abi;
}

std::string_view bcostars::protocol::TransactionImpl::value() const
{
    return m_inner()->data.value;
}

std::string_view bcostars::protocol::TransactionImpl::gasPrice() const
{
    return m_inner()->data.gasPrice;
}

int64_t bcostars::protocol::TransactionImpl::gasLimit() const
{
    return m_inner()->data.gasLimit;
}

std::string_view bcostars::protocol::TransactionImpl::maxFeePerGas() const
{
    return m_inner()->data.maxFeePerGas;
}

std::string_view bcostars::protocol::TransactionImpl::maxPriorityFeePerGas() const
{
    return m_inner()->data.maxPriorityFeePerGas;
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::extension() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.extension.data()),
        m_inner()->data.extension.size()};
}

int64_t bcostars::protocol::TransactionImpl::importTime() const
{
    return m_inner()->importTime;
}
void bcostars::protocol::TransactionImpl::setImportTime(int64_t _importTime)
{
    m_inner()->importTime = _importTime;
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::signatureData() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->signature.data()),
        m_inner()->signature.size()};
}
std::string_view bcostars::protocol::TransactionImpl::sender() const
{
    return {m_inner()->sender.data(), m_inner()->sender.size()};
}
void bcostars::protocol::TransactionImpl::forceSender(const bcos::bytes& _sender) const
{
    m_inner()->sender.assign(_sender.begin(), _sender.end());
}
void bcostars::protocol::TransactionImpl::setSignatureData(bcos::bytes& signature)
{
    m_inner()->signature.assign(signature.begin(), signature.end());
}
int32_t bcostars::protocol::TransactionImpl::attribute() const
{
    return m_inner()->attribute;
}
void bcostars::protocol::TransactionImpl::setAttribute(int32_t attribute)
{
    m_inner()->attribute = attribute;
}
std::string_view bcostars::protocol::TransactionImpl::extraData() const
{
    return m_inner()->extraData;
}
void bcostars::protocol::TransactionImpl::setExtraData(std::string const& _extraData)
{
    m_inner()->extraData = _extraData;
}
uint8_t bcostars::protocol::TransactionImpl::type() const
{
    return static_cast<uint8_t>(m_inner()->type);
}
bcos::bytesConstRef TransactionImpl::extraTransactionBytes() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->extraTransactionBytes.data()),
        m_inner()->extraTransactionBytes.size()};
}

const bcostars::Transaction& bcostars::protocol::TransactionImpl::inner() const
{
    return *m_inner();
}
bcostars::Transaction& bcostars::protocol::TransactionImpl::mutableInner()
{
    return *m_inner();
}
void bcostars::protocol::TransactionImpl::setInner(bcostars::Transaction inner)
{
    *m_inner() = std::move(inner);
}
