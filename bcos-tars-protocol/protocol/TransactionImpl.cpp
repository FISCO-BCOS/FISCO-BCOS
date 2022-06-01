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
#include <boost/asio/detail/socket_ops.hpp>

using namespace bcostars;
using namespace bcostars::protocol;

void TransactionImpl::decode(bcos::bytesConstRef _txData)
{
    m_buffer.assign(_txData.begin(), _txData.end());

    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)m_buffer.data(), m_buffer.size());

    m_inner()->readFrom(input);
}

bcos::bytesConstRef TransactionImpl::encode() const
{
    if (m_buffer.empty())
    {
        tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
        m_inner()->writeTo(output);
        output.getByteBuffer().swap(m_buffer);
    }
    return bcos::ref(m_buffer);
}

bcos::crypto::HashType TransactionImpl::hash(bool _useCache) const
{
    bcos::UpgradableGuard l(x_hash);
    if (!m_inner()->dataHash.empty() && _useCache)
    {
        return *(reinterpret_cast<bcos::crypto::HashType*>(m_inner()->dataHash.data()));
    }
    auto hashImpl = m_cryptoSuite->hashImpl();
    auto hashContext = hashImpl->init();
    auto const& hashFields = m_inner()->data;
    // encode version
    long version =
        boost::asio::detail::socket_ops::host_to_network_long((int32_t)hashFields.version);
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)(&version), sizeof(version) / sizeof(uint8_t)));

    // encode chainID
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.chainID.data(), hashFields.chainID.size()));
    // encode groupID
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.groupID.data(), hashFields.groupID.size()));
    // encode blockLimit
    long blockLimit = boost::asio::detail::socket_ops::host_to_network_long(hashFields.blockLimit);
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)(&blockLimit), sizeof(blockLimit) / sizeof(uint8_t)));
    // encode nonce
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.nonce.data(), hashFields.nonce.size()));
    // encode to
    hashImpl->update(
        hashContext, bcos::bytesConstRef((bcos::byte*)hashFields.to.data(), hashFields.to.size()));
    // encode input
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.input.data(), hashFields.input.size()));

    // encode abi
    hashImpl->update(hashContext,
        bcos::bytesConstRef((bcos::byte*)hashFields.abi.data(), hashFields.abi.size()));

    auto hashResult = hashImpl->final(hashContext);
    bcos::UpgradeGuard ul(l);
    m_inner()->dataHash.assign(hashResult.begin(), hashResult.end());
    return hashResult;
}

bcos::u256 TransactionImpl::nonce() const
{
    if (!m_inner()->data.nonce.empty())
    {
        m_nonce = boost::lexical_cast<bcos::u256>(m_inner()->data.nonce);
    }
    return m_nonce;
}

bcos::bytesConstRef TransactionImpl::input() const
{
    return bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(m_inner()->data.input.data()),
        m_inner()->data.input.size());
}