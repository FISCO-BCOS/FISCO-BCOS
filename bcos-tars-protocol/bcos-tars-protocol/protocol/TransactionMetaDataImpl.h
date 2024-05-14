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
 * @brief implement for TransactionMetaData
 * @file TransactionMetaDataImpl.h
 * @author: yujiechen
 * @date: 2021-09-07
 */
#pragma once

#include "bcos-framework/protocol/TransactionMetaData.h"
#include "bcos-tars-protocol/tars/TransactionMetaData.h"

namespace bcostars::protocol
{
class TransactionMetaDataImpl : public bcos::protocol::TransactionMetaData
{
public:
    using Ptr = std::shared_ptr<TransactionMetaDataImpl>;
    using ConstPtr = std::shared_ptr<const TransactionMetaDataImpl>;

    TransactionMetaDataImpl();
    TransactionMetaDataImpl(bcos::crypto::HashType hash, std::string to);
    explicit TransactionMetaDataImpl(std::function<bcostars::TransactionMetaData*()> inner);
    ~TransactionMetaDataImpl() override = default;

    bcos::crypto::HashType hash() const override;
    void setHash(bcos::crypto::HashType _hash) override;

    std::string_view to() const override;
    void setTo(std::string _to) override;

    uint32_t attribute() const override;
    void setAttribute(uint32_t attribute) override;

    std::string_view source() const override;
    void setSource(std::string source) override;

    const bcostars::TransactionMetaData& inner() const;
    bcostars::TransactionMetaData& mutableInner();
    bcostars::TransactionMetaData takeInner();
    void setInner(bcostars::TransactionMetaData inner);

private:
    std::function<bcostars::TransactionMetaData*()> m_inner;
};
}  // namespace bcostars::protocol
