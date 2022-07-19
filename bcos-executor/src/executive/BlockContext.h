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
 * @brief block level context
 * @file BlockContext.h
 * @author: xingqiangbai
 * @date: 2021-05-26
 */

#pragma once

#include "../Common.h"
#include "ExecutiveFactory.h"
#include "ExecutiveFlowInterface.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <memory>
#include <stack>
#include <string_view>

namespace bcos
{
namespace executor
{
class TransactionExecutive;
class PrecompiledContract;

class BlockContext : public std::enable_shared_from_this<BlockContext>
{
public:
    typedef std::shared_ptr<BlockContext> Ptr;

    BlockContext(std::shared_ptr<storage::StateStorageInterface> storage,
        crypto::Hash::Ptr _hashImpl, bcos::protocol::BlockNumber blockNumber, h256 blockHash,
        uint64_t timestamp, uint32_t blockVersion, const VMSchedule& _schedule, bool _isWasm,
        bool _isAuthCheck);

    BlockContext(std::shared_ptr<storage::StateStorageInterface> storage,
        crypto::Hash::Ptr _hashImpl, protocol::BlockHeader::ConstPtr _current,
        const VMSchedule& _schedule, bool _isWasm, bool _isAuthCheck);

    using getTxCriticalsHandler = std::function<std::shared_ptr<std::vector<std::string>>(
        const protocol::Transaction::ConstPtr& _tx)>;
    virtual ~BlockContext(){};

    std::shared_ptr<storage::StateStorageInterface> storage() { return m_storage; }

    uint64_t txGasLimit() const { return m_txGasLimit; }
    void setTxGasLimit(uint64_t _txGasLimit) { m_txGasLimit = _txGasLimit; }

    auto getTxCriticals(const protocol::Transaction::ConstPtr& _tx)
        -> std::shared_ptr<std::vector<std::string>>;

    crypto::Hash::Ptr hashHandler() const { return m_hashImpl; }
    bool isWasm() const { return m_isWasm; }
    bool isAuthCheck() const { return m_isAuthCheck; }
    int64_t number() const { return m_blockNumber; }
    h256 hash() const { return m_blockHash; }
    uint64_t timestamp() const { return m_timeStamp; }
    uint32_t blockVersion() const { return m_blockVersion; }
    u256 const& gasLimit() const { return m_gasLimit; }

    VMSchedule const& vmSchedule() const { return m_schedule; }

    ExecutiveFlowInterface::Ptr getExecutiveFlow(std::string codeAddress);
    void setExecutiveFlow(std::string codeAddress, ExecutiveFlowInterface::Ptr executiveFlow);

    void stop()
    {
        bcos::ReadGuard l(x_executiveFlows);
        for (auto it : m_executiveFlows)
        {
            it.second->stop();
        }
    }
    void clear()
    {
        bcos::WriteGuard l(x_executiveFlows);
        m_executiveFlows.clear();
    }

private:
    mutable bcos::SharedMutex x_executiveFlows;
    tbb::concurrent_unordered_map<std::string, ExecutiveFlowInterface::Ptr> m_executiveFlows;

    bcos::protocol::BlockNumber m_blockNumber;
    h256 m_blockHash;
    uint64_t m_timeStamp;
    uint32_t m_blockVersion;

    VMSchedule m_schedule;
    u256 m_gasLimit;
    bool m_isWasm = false;
    bool m_isAuthCheck = false;

    uint64_t m_txGasLimit = 3000000000;
    std::shared_ptr<storage::StateStorageInterface> m_storage;
    crypto::Hash::Ptr m_hashImpl;
};

}  // namespace executor

}  // namespace bcos
