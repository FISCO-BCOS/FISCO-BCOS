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

#include "../impl/TarsHashable.h"

#include "TransactionImpl.h"
#include <bcos-concepts/Hash.h>
#include <boost/endian/conversion.hpp>

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
    auto anyHasher = hashImpl->hasher();

    bcos::crypto::HashType hashResult;
    std::visit(
        [this, &hashResult, &l](auto& hasher) {
            using Hasher = std::remove_cvref_t<decltype(hasher)>;
            bcos::concepts::hash::calculate<Hasher>(*m_inner(), hashResult);

            bcos::UpgradeGuard ul(l);
            m_inner()->dataHash.assign(hashResult.begin(), hashResult.end());
        },
        anyHasher);
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