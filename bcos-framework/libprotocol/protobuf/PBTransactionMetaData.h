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
 * @brief ProtoBuf implementation for TransactionMetaData
 * @file PBTransactionMetaData.h
 * @author: yujiechen
 * @date: 2021-08-20
 */
#pragma once
#include "../../interfaces/protocol/TransactionMetaData.h"
#include "libprotocol/bcos-proto/Block.pb.h"
namespace bcos
{
namespace protocol
{
class PBTransactionMetaData : public TransactionMetaData
{
public:
    PBTransactionMetaData() : m_pbTxMetaData(std::make_shared<PBRawTransactionMetaData>()) {}
    PBTransactionMetaData(bcos::crypto::HashType const _hash, std::string const& _to)
      : PBTransactionMetaData()
    {
        setHash(_hash);
        setTo(_to);
    }
    explicit PBTransactionMetaData(std::shared_ptr<PBRawTransactionMetaData> _txMetaData)
      : m_pbTxMetaData(_txMetaData)
    {}

    ~PBTransactionMetaData() override {}

    std::shared_ptr<PBRawTransactionMetaData> pbTxMetaData() { return m_pbTxMetaData; }
    bcos::crypto::HashType hash() const override
    {
        if (m_hash != bcos::crypto::HashType())
        {
            return m_hash;
        }
        auto const& hash = m_pbTxMetaData->hash();
        if (hash.size() >= bcos::crypto::HashType::size)
        {
            m_hash =
                bcos::crypto::HashType((byte const*)hash.c_str(), bcos::crypto::HashType::size);
        }
        return m_hash;
    }
    std::string_view to() const override { return m_pbTxMetaData->to(); }

    void setHash(bcos::crypto::HashType _hash) override
    {
        m_hash = _hash;
        m_pbTxMetaData->set_hash(_hash.data(), bcos::crypto::HashType::size);
    }
    void setTo(std::string _to) override { m_pbTxMetaData->mutable_to()->swap(_to); }

    uint32_t attribute() const override { return m_pbTxMetaData->attribute(); }
    void setAttribute(uint32_t _attribute) override { m_pbTxMetaData->set_attribute(_attribute); }

    std::string_view source() const override { return m_pbTxMetaData->source(); }
    void setSource(std::string _source) override { m_pbTxMetaData->set_source(_source); }

private:
    std::shared_ptr<PBRawTransactionMetaData> m_pbTxMetaData;
    mutable bcos::crypto::HashType m_hash = bcos::crypto::HashType();
};

}  // namespace protocol
}  // namespace bcos