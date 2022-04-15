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
 * @brief interface for TransactionMetaData
 * @file TransactionMetaData.h
 * @author: yujiechen
 * @date: 2021-08-20
 */
#pragma once
#include "Transaction.h"
#include "TransactionSubmitResult.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>

namespace bcos
{
namespace protocol
{
class TransactionMetaData
{
public:
    using Ptr = std::shared_ptr<TransactionMetaData>;
    using ConstPtr = std::shared_ptr<const TransactionMetaData>;

    virtual ~TransactionMetaData() = default;

    virtual bcos::crypto::HashType hash() const = 0;
    virtual void setHash(bcos::crypto::HashType _hash) = 0;

    virtual std::string_view to() const = 0;
    virtual void setTo(std::string _to) = 0;

    virtual uint32_t attribute() const = 0;
    virtual void setAttribute(uint32_t attribute) = 0;

    virtual std::string_view source() const = 0;
    virtual void setSource(std::string source) = 0;
    virtual void setAbort(bool _abort) = 0;
    virtual bool abort() const = 0;
};

using TransactionMetaDataList = std::vector<TransactionMetaData::Ptr>;
using TransactionMetaDataListPtr = std::shared_ptr<TransactionMetaDataList>;
}  // namespace protocol
}  // namespace bcos