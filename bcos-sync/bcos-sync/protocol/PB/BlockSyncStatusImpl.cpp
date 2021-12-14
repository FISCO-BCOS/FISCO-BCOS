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
 * @brief implementation for the block sync status packet
 * @file BlockSyncStatusImpl.cpp
 * @author: yujiechen
 * @date 2021-05-23
 */
#include "BlockSyncStatusImpl.h"

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::protocol;
using namespace bcos::crypto;

void BlockSyncStatusImpl::decode(bytesConstRef _data)
{
    BlockSyncMsgImpl::decode(_data);
    deserializeObject();
}

void BlockSyncStatusImpl::deserializeObject()
{
    auto const& hashData = m_syncMessage->hash();
    if (hashData.size() >= HashType::size)
    {
        m_hash = HashType((byte const*)hashData.data(), HashType::size);
    }
    auto const& genesisHashData = m_syncMessage->genesishash();
    if (genesisHashData.size() >= HashType::size)
    {
        m_genesisHash = HashType((byte const*)genesisHashData.data(), HashType::size);
    }
}
void BlockSyncStatusImpl::setHash(HashType const& _hash)
{
    m_hash = _hash;
    m_syncMessage->set_hash(_hash.data(), HashType::size);
}

void BlockSyncStatusImpl::setGenesisHash(HashType const& _gensisHash)
{
    m_genesisHash = _gensisHash;
    m_syncMessage->set_genesishash(_gensisHash.data(), HashType::size);
}