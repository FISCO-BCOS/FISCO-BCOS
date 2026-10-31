/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file ThrowingTxPool.h
 * @author: kyonGuo
 * @date 2026/9/10
 */

#pragma once
#include <bcos-framework/testutils/faker/FakeTxPool.h>
#include <bcos-utilities/Error.h>
#include <string>
#include <utility>

namespace bcos::test
{
/// A pool whose submitTransaction always throws the Error it was built with, so a case about what
/// an RPC face does with a throwing pool takes microseconds instead of the pool's own timing. The
/// code is the point of the parameter: submitTransaction answers a refusal and a fault with the
/// same type and the same int field -- a TransactionStatus for the first, a MAX/TARS client's
/// "No value!" or a transport code for the second -- and the faces answer the two differently.
class ThrowingTxPool : public FakeTxPool
{
public:
    ThrowingTxPool(int32_t code, std::string message) : m_code(code), m_message(std::move(message))
    {}

    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr, bool waitForReceipt) override
    {
        m_waitedForReceipt = waitForReceipt;
        BOOST_THROW_EXCEPTION(BCOS_ERROR(m_code, m_message));
        co_return nullptr;
    }
    // Each face broadcasts before it submits -- the Web3 face the transaction
    // (EthEndpoint::sendRawTransaction), the BCOS face its buffer (JsonRpcImpl_2_0's
    // sendTransaction) -- and TxPoolInterface's defaults for both throw "Unimplemented!".
    task::Task<void> broadcastTransaction(protocol::Transaction const&) override { co_return; }
    task::Task<void> broadcastTransactionBuffer(bytesConstRef) override { co_return; }

    int32_t m_code;
    std::string m_message;
    bool m_waitedForReceipt = false;
};
}  // namespace bcos::test
