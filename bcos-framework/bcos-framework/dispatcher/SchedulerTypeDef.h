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
 * @brief typedef for scheduler module
 * @file SchedulerTypeDef.h
 * @author: yujiechen
 * @date: 2021-04-9
 */
#pragma once
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>

namespace bcos
{

namespace protocol
{
struct Session
{
    using Ptr = std::shared_ptr<Session>;
    using ConstPtr = std::shared_ptr<const Session>;

    enum Status
    {
        STARTED = 0,
        DIRTY,
        COMMITTED,
        ROLLBACKED
    };

    long sessionID;
    Status status;
    bcos::protocol::BlockNumber beginNumber;  // [
    bcos::protocol::BlockNumber endNumber;    // )
};
}  // namespace protocol
namespace scheduler
{
enum SchedulerError
{
    UnknownError = -70000,
    InvalidStatus,
    InvalidBlockNumber,
    InvalidBlocks,
    NextBlockError,
    PrewriteBlockError,
    CommitError,
    RollbackError,
    UnexpectedKeyLockError,
    BatchError,
    SerialExecuteError,
    DMCError,
    DAGError,
    CallError,
    ExecutorNotEstablishedError,
    fetchGasLimitError,
    Stopped,
    InvalidBlockVersion,
    BlockIsCommitting,
    InvalidTransactionVersion,
    BuildBlockError,
};
}
}  // namespace bcos
