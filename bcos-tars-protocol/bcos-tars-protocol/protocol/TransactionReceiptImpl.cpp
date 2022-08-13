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
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"

#include "TransactionReceiptImpl.h"
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>

using namespace bcostars;
using namespace bcostars::protocol;

void TransactionReceiptImpl::decode(bcos::bytesConstRef _receiptData)
{
    bcos::concepts::serialize::decode(_receiptData, *m_inner());
}

void TransactionReceiptImpl::encode(bcos::bytes& _encodedData) const
{
    bcos::concepts::serialize::encode(*m_inner(), _encodedData);
}

// Note: not thread-safe
bcos::crypto::HashType TransactionReceiptImpl::hash() const
{
    if (!m_inner()->dataHash.empty())
    {
        return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner()->dataHash.data()));
    }
    auto hashImpl = m_cryptoSuite->hashImpl();
    auto anyHasher = hashImpl->hasher();

    bcos::crypto::HashType hashResult;
    std::visit(
        [this, &hashResult](auto& hasher) {
            using Hasher = std::remove_cvref_t<decltype(hasher)>;
            bcos::concepts::hash::calculate<Hasher>(*m_inner(), hashResult);

            m_inner()->dataHash.assign(hashResult.begin(), hashResult.end());
        },
        anyHasher);
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
