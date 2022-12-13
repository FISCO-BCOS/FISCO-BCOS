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
#include "../impl/TarsSerializable.h"

#include "TransactionImpl.h"
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
    if (m_inner()->dataHash.empty())
    {
        BOOST_THROW_EXCEPTION(EmptyTransactionHash{});
    }

    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

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