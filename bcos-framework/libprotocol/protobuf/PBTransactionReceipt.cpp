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
 * @brief implementation of TransactionReceipt
 * @file PBTransactionReceipt.cpp
 * @author: yujiechen
 * @date: 2021-03-18
 */
#include "PBTransactionReceipt.h"
#include "../Common.h"
#include "bcos-codec/scale/Scale.h"
#include <gsl/span>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::codec::scale;

PBTransactionReceipt::PBTransactionReceipt(
    CryptoSuite::Ptr _cryptoSuite, bytesConstRef _receiptData)
  : TransactionReceipt(_cryptoSuite), m_receipt(std::make_shared<PBRawTransactionReceipt>())
{
    m_dataCache = std::make_shared<bytes>();
    decode(_receiptData);
}

PBTransactionReceipt::PBTransactionReceipt(CryptoSuite::Ptr _cryptoSuite, int32_t _version,
    u256 const& _gasUsed, const std::string_view& _contractAddress, LogEntriesPtr _logEntries,
    int32_t _status, BlockNumber _blockNumber)
  : TransactionReceipt(_cryptoSuite),
    m_receipt(std::make_shared<PBRawTransactionReceipt>()),
    m_gasUsed(_gasUsed),
    m_contractAddress(
        (byte*)_contractAddress.data(), (byte*)(_contractAddress.data() + _contractAddress.size())),
    m_logEntries(_logEntries),
    m_status(_status),
    m_blockNumber(_blockNumber)
{
    m_dataCache = std::make_shared<bytes>();
    m_receipt->set_version(_version);
}

PBTransactionReceipt::PBTransactionReceipt(CryptoSuite::Ptr _cryptoSuite, int32_t _version,
    u256 const& _gasUsed, const std::string_view& _contractAddress, LogEntriesPtr _logEntries,
    int32_t _status, bytes const& _ouptput, BlockNumber _blockNumber)
  : PBTransactionReceipt(
        _cryptoSuite, _version, _gasUsed, _contractAddress, _logEntries, _status, _blockNumber)
{
    m_output = _ouptput;
}

PBTransactionReceipt::PBTransactionReceipt(CryptoSuite::Ptr _cryptoSuite, int32_t _version,
    u256 const& _gasUsed, const std::string_view& _contractAddress, LogEntriesPtr _logEntries,
    int32_t _status, bytes&& _ouptput, BlockNumber _blockNumber)
  : PBTransactionReceipt(
        _cryptoSuite, _version, _gasUsed, _contractAddress, _logEntries, _status, _blockNumber)
{
    m_output = std::move(_ouptput);
}

void PBTransactionReceipt::decode(bytesConstRef _data)
{
    *m_dataCache = _data.toBytes();
    // decode receipt
    decodePBObject(m_receipt, _data);
    ScaleDecoderStream stream(gsl::span<const byte>(
        (byte*)m_receipt->hashfieldsdata().data(), m_receipt->hashfieldsdata().size()));
    stream >> m_status >> m_output >> m_contractAddress >> m_gasUsed >> m_logEntries >>
        m_blockNumber;
}

void PBTransactionReceipt::encode(bytes& _encodeReceiptData) const
{
    encodeHashFields();
    encodePBObject(_encodeReceiptData, m_receipt);
}

bytesConstRef PBTransactionReceipt::encode(bool _onlyHashFieldData) const
{
    if (_onlyHashFieldData)
    {
        if (m_receipt->hashfieldsdata().size() == 0)
        {
            encodeHashFields();
        }
        return bytesConstRef(
            (byte const*)m_receipt->hashfieldsdata().data(), m_receipt->hashfieldsdata().size());
    }
    if (m_dataCache->size() == 0)
    {
        encode(*m_dataCache);
    }
    return bytesConstRef((byte const*)m_dataCache->data(), m_dataCache->size());
}

void PBTransactionReceipt::encodeHashFields() const
{
    // the hash field has already been encoded
    if (m_receipt->hashfieldsdata().size() > 0)
    {
        return;
    }
    // encode the hashFieldsData
    ScaleEncoderStream stream;
    stream << m_status << m_output << m_contractAddress << m_gasUsed << m_logEntries
           << m_blockNumber;
    auto hashFieldsData = stream.data();
    m_receipt->set_version(m_version);
    m_receipt->set_hashfieldsdata(hashFieldsData.data(), hashFieldsData.size());
}