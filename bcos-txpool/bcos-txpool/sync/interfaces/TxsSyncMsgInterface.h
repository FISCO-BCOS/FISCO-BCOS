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
 * @brief interfaces for the txs-sync-message
 * @file TxsSyncMsgInterface.h
 * @author: yujiechen
 * @date 2021-05-11
 */
#pragma once
#include <bcos-framework/interfaces/crypto/CommonType.h>
#include <bcos-framework//interfaces/crypto/KeyInterface.h>
namespace bcos
{
namespace sync
{
class TxsSyncMsgInterface
{
public:
    using Ptr = std::shared_ptr<TxsSyncMsgInterface>;
    TxsSyncMsgInterface() = default;
    virtual ~TxsSyncMsgInterface() {}

    virtual bytesPointer encode() const = 0;
    virtual void decode(bytesConstRef _data) = 0;

    virtual int32_t version() const = 0;
    virtual int32_t type() const = 0;
    virtual bytesConstRef txsData() const = 0;
    virtual bcos::crypto::HashList const& txsHash() const = 0;

    virtual void setVersion(int32_t _version) = 0;
    virtual void setType(int32_t _type) = 0;
    virtual void setTxsData(bytes const& _txsData) = 0;
    virtual void setTxsData(bytes&& _txsData) = 0;
    virtual void setTxsHash(bcos::crypto::HashList const& _txsHash) = 0;

    virtual void setFrom(bcos::crypto::NodeIDPtr _from) { m_from = _from; }
    virtual bcos::crypto::NodeIDPtr from() const { return m_from; }

protected:
    bcos::crypto::NodeIDPtr m_from;
};
using TxsSyncMsgList = std::vector<TxsSyncMsgInterface::Ptr>;
using TxsSyncMsgListPtr = std::shared_ptr<TxsSyncMsgList>;
}  // namespace sync
}  // namespace bcos