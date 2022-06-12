/*
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
 * @brief Transaction coded in PB format
 * @file PBTransaction.cpp
 * @author: yujiechen
 * @date: 2021-03-16
 */
#include "PBTransaction.h"
#include "../Common.h"
#include <bcos-framework//protocol/Exceptions.h>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;

PBTransaction::PBTransaction(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, int32_t _version,
    const std::string_view& _to, bytes const& _input, u256 const& _nonce, int64_t _blockLimit,
    std::string const& _chainId, std::string const& _groupId, int64_t _importTime,
    std::string const& _abi)
  : PBTransaction(_cryptoSuite)
{
    m_transactionHashFields->set_version(_version);
    // set receiver address
    if (!_to.empty())
    {
        m_transactionHashFields->set_to(_to.data(), _to.size());
    }
    // set input data
    m_transactionHashFields->set_input(_input.data(), _input.size());
    // set nonce
    m_nonce = _nonce;
    bytes nonceBytes = toBigEndian(_nonce);
    m_transactionHashFields->set_nonce(nonceBytes.data(), nonceBytes.size());
    // set block limit
    m_transactionHashFields->set_blocklimit(_blockLimit);
    // set chainId
    m_transactionHashFields->set_chainid(_chainId);
    // set groupId
    m_transactionHashFields->set_groupid(_groupId);
    m_transactionHashFields->set_abi(_abi);

    // encode hashFields
    auto encodedHashFieldsData = encodePBObject(m_transactionHashFields);
    m_transaction->set_hashfieldsdata(encodedHashFieldsData->data(), encodedHashFieldsData->size());

    auto hash = m_cryptoSuite->hash(*encodedHashFieldsData);
    m_transaction->set_hashfieldshash(hash.data(), hash.SIZE);
    // set import time
    m_transaction->set_import_time(_importTime);
}

PBTransaction::PBTransaction(CryptoSuite::Ptr _cryptoSuite, bytesConstRef _txData, bool _checkSig)
  : PBTransaction(_cryptoSuite)
{
    decode(_txData);
    if (_checkSig)
    {
        verify();
    }
}

void PBTransaction::decode(bytesConstRef _txData)
{
    // cache data into dataCache
    *m_dataCache = _txData.toBytes();
    // decode transaction
    decodePBObject(m_transaction, _txData);
    // decode transactionHashFields
    auto transactionHashFields = m_transaction->mutable_hashfieldsdata();
    decodePBObject(m_transactionHashFields,
        bytesConstRef((byte const*)transactionHashFields->c_str(), transactionHashFields->size()));
    m_nonce =
        fromBigEndian<u256>(bytesConstRef((const byte*)m_transactionHashFields->nonce().data(),
            m_transactionHashFields->nonce().size()));
}

void PBTransaction::encode(bytes& _encodedData) const
{
    encodePBObject(_encodedData, m_transaction);
}

bytesConstRef PBTransaction::encode() const
{
    if (m_dataCache->empty())
    {
        encode(*m_dataCache);
    }
    return bytesConstRef((byte const*)m_dataCache->data(), m_dataCache->size());
}

bcos::crypto::HashType PBTransaction::hash(bool _useCache) const
{
    if (!m_transaction->hashfieldshash().empty() && _useCache)
    {
        return *(reinterpret_cast<const bcos::crypto::HashType*>(
            m_transaction->hashfieldshash().data()));
    }
    auto data = bytesConstRef((bcos::byte*)m_transaction->hashfieldsdata().data(),
        m_transaction->hashfieldsdata().size());
    auto hash = m_cryptoSuite->hash(data);
    m_transaction->set_hashfieldshash(hash.data(), HashType::SIZE);
    return hash;
}

void PBTransaction::updateSignature(bytesConstRef _signatureData, bytes const& _sender)
{
    m_transaction->set_signaturedata(_signatureData.data(), _signatureData.size());
    m_sender = _sender;
    m_dataCache->clear();
}

bytesConstRef PBTransaction::input() const
{
    auto const& inputData = m_transactionHashFields->input();
    return bytesConstRef((byte const*)(inputData.data()), inputData.size());
}