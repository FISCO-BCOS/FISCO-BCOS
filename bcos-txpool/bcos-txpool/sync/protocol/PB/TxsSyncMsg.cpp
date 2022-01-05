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
 * @brief implementation for the txs-sync-message
 * @file TxsSync.cpp
 * @author: yujiechen
 * @date 2021-05-11
 */
#include "TxsSyncMsg.h"
#include "bcos-protocol/Common.h"

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::protocol;

bytesPointer TxsSyncMsg::encode() const
{
    return encodePBObject(m_rawSyncMessage);
}

void TxsSyncMsg::decode(bytesConstRef _data)
{
    decodePBObject(m_rawSyncMessage, _data);
    deserializeObject();
}

int32_t TxsSyncMsg::version() const
{
    return m_rawSyncMessage->version();
}

int32_t TxsSyncMsg::type() const
{
    return m_rawSyncMessage->type();
}

bytesConstRef TxsSyncMsg::txsData() const
{
    auto const& txsData = m_rawSyncMessage->txsdata();
    return bytesConstRef((byte const*)txsData.data(), txsData.size());
}

HashList const& TxsSyncMsg::txsHash() const
{
    return *m_txsHash;
}

void TxsSyncMsg::setVersion(int32_t _version)
{
    m_rawSyncMessage->set_version(_version);
}

void TxsSyncMsg::setType(int32_t _type)
{
    m_rawSyncMessage->set_type(_type);
}

void TxsSyncMsg::setTxsData(bytes const& _txsData)
{
    m_rawSyncMessage->set_txsdata(_txsData.data(), _txsData.size());
}
void TxsSyncMsg::setTxsData(bytes&& _txsData)
{
    auto dataSize = _txsData.size();
    m_rawSyncMessage->set_txsdata(std::move(_txsData).data(), dataSize);
}

void TxsSyncMsg::setTxsHash(HashList const& _txsHash)
{
    *m_txsHash = _txsHash;
    m_rawSyncMessage->clear_txshash();
    for (auto const& hash : _txsHash)
    {
        m_rawSyncMessage->add_txshash(hash.data(), HashType::size);
    }
}

void TxsSyncMsg::deserializeObject()
{
    m_txsHash->clear();
    for (int i = 0; i < m_rawSyncMessage->txshash_size(); i++)
    {
        auto const& hashData = m_rawSyncMessage->txshash(i);
        m_txsHash->emplace_back(
            HashType((byte const*)hashData.c_str(), bcos::crypto::HashType::size));
    }
}