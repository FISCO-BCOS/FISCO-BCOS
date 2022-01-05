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
 * @brief fake the txsSyncMsg
 * @file FakeTxsSyncMsg.h
 */
#pragma once
#include "bcos-txpool/sync/protocol/PB/TxsSyncMsg.h"
#include "bcos-txpool/sync/protocol/PB/TxsSyncMsgFactoryImpl.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
class FakeTxsSyncMsg
{
public:
    FakeTxsSyncMsg() { m_msgFactory = std::make_shared<TxsSyncMsgFactoryImpl>(); }
    TxsSyncMsgInterface::Ptr fakeTxsMsg(
        int32_t _type, int32_t _version, HashList const& _txsHash, bytes const& _txsData)
    {
        auto msg = m_msgFactory->createTxsSyncMsg();
        msg->setType(_type);
        msg->setVersion(_version);
        msg->setTxsHash(_txsHash);
        msg->setTxsData(_txsData);

        // check encode/decode
        auto encodedData = msg->encode();
        auto decodedMsg = m_msgFactory->createTxsSyncMsg(ref(*encodedData));
        BOOST_CHECK(msg->type() == decodedMsg->type());
        BOOST_CHECK(msg->version() == decodedMsg->version());
        BOOST_CHECK(msg->txsHash() == decodedMsg->txsHash());
        BOOST_CHECK(msg->txsData().toBytes() == decodedMsg->txsData().toBytes());

        // compare with the origin data
        BOOST_CHECK(_type == decodedMsg->type());
        BOOST_CHECK(_version == decodedMsg->version());
        BOOST_CHECK(_txsHash == decodedMsg->txsHash());
        BOOST_CHECK(_txsData == decodedMsg->txsData().toBytes());
        return msg;
    }

private:
    TxsSyncMsgFactory::Ptr m_msgFactory;
};
}  // namespace test
}  // namespace bcos