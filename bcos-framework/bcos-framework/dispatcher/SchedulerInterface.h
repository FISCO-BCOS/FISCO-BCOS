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
 * @brief interface of Scheduler
 * @file SchedulerInterface.h
 * @author: ancelmo
 * @date: 2021-07-27
 */

#pragma once
#include "../executor/ParallelTransactionExecutorInterface.h"
#include "../ledger/LedgerConfig.h"
#include "../protocol/Block.h"
#include "../protocol/ProtocolTypeDef.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Error.h>
#include <functional>
#include <memory>
#include <string_view>

namespace bcos::scheduler
{
class SchedulerInterface
{
public:
    using Ptr = std::shared_ptr<SchedulerInterface>;
    SchedulerInterface() = default;
    virtual ~SchedulerInterface() {}

    // by pbft & sync
    virtual void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool _sysBlock)>
            callback) = 0;

    // by pbft & sync
    virtual void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback) = 0;

    // by console, query committed committing executing
    virtual void status(
        std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)> callback) = 0;

    // by rpc
    virtual void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)>) = 0;

    // by executor
    virtual void registerExecutor(std::string name,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor,
        std::function<void(Error::Ptr&&)> callback) = 0;

    virtual void unregisterExecutor(
        const std::string& name, std::function<void(Error::Ptr&&)> callback) = 0;

    // clear all status
    virtual void reset(std::function<void(Error::Ptr&&)> callback) = 0;
    virtual void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) = 0;

    virtual void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) = 0;

    // for performance, do the things before executing block in executor.
    virtual void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr&&)> callback) = 0;

    virtual void stop() {};
};
}  // namespace bcos::scheduler
