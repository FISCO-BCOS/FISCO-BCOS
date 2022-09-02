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
 * @file TxsSync.h
 * @author: yujiechen
 * @date 2021-05-11
 */
#pragma once
#include <bcos-txpool/sync/interfaces/TxsSyncMsgInterface.h>
#include <bcos-txpool/sync/protocol/proto/TxsSync.pb.h>

namespace bcos
{
namespace sync
{
class TxsSyncMsg : public TxsSyncMsgInterface
{
public:
    TxsSyncMsg()
      : m_rawSyncMessage(std::make_shared<TxsSyncMessage>()),
        m_txsHash(std::make_shared<bcos::crypto::HashList>())
    {}

    explicit TxsSyncMsg(bytesConstRef _data) : TxsSyncMsg() { decode(_data); }
    ~TxsSyncMsg() override {}

    bytesPointer encode() const override;
    void decode(bytesConstRef _data) override;

    int32_t version() const override;
    int32_t type() const override;
    bytesConstRef txsData() const override;
    bcos::crypto::HashList const& txsHash() const override;

    void setVersion(int32_t _version) override;
    void setType(int32_t _type) override;
    void setTxsData(bytes const& _txsData) override;
    void setTxsData(bytes&& _txsData) override;
    void setTxsHash(bcos::crypto::HashList const& _txsHash) override;

protected:
    virtual void deserializeObject();

private:
    std::shared_ptr<TxsSyncMessage> m_rawSyncMessage;
    bcos::crypto::HashListPtr m_txsHash;
};
}  // namespace sync
}  // namespace bcos