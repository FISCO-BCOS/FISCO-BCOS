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
 * @brief interface of Executor
 * @file ParallelExecutorInterface.h
 * @author: ancelmo
 * @date: 2021-07-27
 */

#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/ProtocolTypeDef.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "ExecutionMessage.h"
#include "ExecutorStatus.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/range/any_range.hpp>
#include <memory>

namespace bcos
{
namespace executor
{
class ParallelTransactionExecutorInterface
{
public:
    using Ptr = std::shared_ptr<ParallelTransactionExecutorInterface>;
    ParallelTransactionExecutorInterface() = default;
    virtual ~ParallelTransactionExecutorInterface() = default;

    virtual void status(
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr)>
            callback)
    {
        // TODO: use pure virtual function
        auto status = std::make_unique<bcos::protocol::ExecutorStatus>();
        status->setSeq(m_seq);
        callback(nullptr, std::move(status));
    };

    virtual void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) = 0;

    virtual void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) = 0;

    virtual void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,

        // called every time at all tx stop( pause or finish)
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) = 0;

    virtual void dagExecuteTransactions(
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) = 0;

    virtual void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) = 0;

    virtual void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) = 0;

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    virtual void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) = 0;

    // Commit uncommitted data
    virtual void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) = 0;

    // Rollback the changes
    virtual void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) = 0;

    /* ----- XA Transaction interface End ----- */

    // drop all status
    virtual void reset(std::function<void(bcos::Error::Ptr)> callback) = 0;

    virtual void getCode(
        std::string_view contract, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) = 0;

    virtual void getABI(
        std::string_view contract, std::function<void(bcos::Error::Ptr, std::string)> callback) = 0;

    virtual void start(){};

    virtual void stop(){};

private:
    int64_t m_seq = utcTime();
};
}  // namespace executor
}  // namespace bcos
