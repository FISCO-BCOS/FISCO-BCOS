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
 * @brief tars implementation for ExecutionResult
 * @file ExecutionResultImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#include "TransactionReceiptImpl.h"
#include "bcos-tars-protocol/tars/ExecutionResult.h"
#include <bcos-framework/interfaces/protocol/ExecutionResult.h>
#include <bcos-framework/libutilities/Common.h>

namespace bcostars
{
namespace protocol
{
class ExecutionResultImpl : public bcos::protocol::ExecutionResult
{
public:
    ~ExecutionResultImpl() override {}

    Status status() const noexcept override { return (Status)m_inner->status; }

    bcos::protocol::TransactionReceipt::ConstPtr receipt() const noexcept override
    {
        std::shared_ptr<const bcostars::protocol::TransactionReceiptImpl> receipt =
            std::make_shared<const TransactionReceiptImpl>(
                m_cryptoSuite, [m_inner = this->m_inner]() { return &m_inner->receipt; });

        return receipt;
    }

    bcos::bytesConstRef to() const noexcept override
    {
        return bcos::bytesConstRef((bcos::byte*)m_inner->to.data(), m_inner->to.size());
    }

private:
    std::shared_ptr<bcostars::ExecutionResult> m_inner;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcostars