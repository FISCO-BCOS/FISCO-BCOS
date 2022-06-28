/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief tars implementation for ExecutionMessage
 * @file ExecutionMessageImpl.h
 * @author: yujiechen
 * @date 2022-05-09
 */

#pragma once

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "bcos-tars-protocol/tars/ExecutionMessage.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-framework/interfaces/executor/ExecutionMessage.h>
#include <bcos-framework/interfaces/protocol/LogEntry.h>
namespace bcostars
{
namespace protocol
{
class ExecutionMessageImpl : public bcos::protocol::ExecutionMessage
{
public:
    using Ptr = std::shared_ptr<ExecutionMessageImpl>;
    using UniquePtr = std::unique_ptr<ExecutionMessageImpl>;
    using UniqueConstPtr = std::unique_ptr<const ExecutionMessageImpl>;
    ExecutionMessageImpl()
      : m_inner([m_executionMessage = bcostars::ExecutionMessage()]() mutable {
            return &m_executionMessage;
        })
    {
        decodeLogEntries();
        decodeKeyLocks();
    }
    ExecutionMessageImpl(std::function<bcostars::ExecutionMessage*()> _inner) : m_inner(_inner)
    {
        decodeLogEntries();
        decodeKeyLocks();
    }

    ~ExecutionMessageImpl() override {}

    Type type() const override { return (Type)m_inner()->type; }
    void setType(Type _type) override { m_inner()->type = _type; }

    bcos::crypto::HashType transactionHash() const override
    {
        if (m_inner()->transactionHash.size() < bcos::crypto::HashType::size)
        {
            return bcos::crypto::HashType();
        }
        return *(reinterpret_cast<bcos::crypto::HashType*>(m_inner()->transactionHash.data()));
    }
    void setTransactionHash(bcos::crypto::HashType hash) override
    {
        m_inner()->transactionHash.assign(hash.begin(), hash.end());
    }

    int64_t contextID() const override { return m_inner()->contextID; }
    void setContextID(int64_t contextID) override { m_inner()->contextID = contextID; }

    int64_t seq() const override { return m_inner()->seq; }
    void setSeq(int64_t seq) override { m_inner()->seq = seq; }

    std::string_view origin() const override { return m_inner()->origin; }
    void setOrigin(std::string origin) override { m_inner()->origin = origin; }

    std::string_view from() const override { return m_inner()->from; }
    void setFrom(std::string from) override { m_inner()->from = from; }

    std::string_view to() const override { return m_inner()->to; }
    void setTo(std::string to) override { m_inner()->to = to; }

    std::string_view abi() const override { return m_inner()->abi; }
    void setABI(std::string abi) override { m_inner()->abi = abi; }

    int32_t depth() const override { return m_inner()->depth; }
    void setDepth(int32_t depth) override { m_inner()->depth = depth; }

    bool create() const override { return m_inner()->create; }
    void setCreate(bool create) override { m_inner()->create = create; }

    bool internalCreate() const override { return m_inner()->internalCreate; }
    void setInternalCreate(bool internalCreate) override
    {
        m_inner()->internalCreate = internalCreate;
    }

    bool internalCall() const override { return m_inner()->internalCall; }
    void setInternalCall(bool internalCall) override { m_inner()->internalCall = internalCall; }


    int64_t gasAvailable() const override { return m_inner()->gasAvailable; }
    void setGasAvailable(int64_t gasAvailable) override { m_inner()->gasAvailable = gasAvailable; }

    bcos::bytesConstRef data() const override
    {
        return bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(m_inner()->data.data()), m_inner()->data.size());
    }

    bcos::bytes takeData() override
    {
        return bcos::bytes(m_inner()->data.begin(), m_inner()->data.end());
    }
    void setData(bcos::bytes data) override { m_inner()->data.assign(data.begin(), data.end()); }

    bool staticCall() const override { return m_inner()->staticCall; }
    void setStaticCall(bool staticCall) override { m_inner()->staticCall = staticCall; }

    // for evm
    std::optional<bcos::u256> createSalt() const override
    {
        std::optional<bcos::u256> emptySalt;
        if (m_inner()->salt.size() == 0)
        {
            return emptySalt;
        }
        try
        {
            return std::optional<bcos::u256>(boost::lexical_cast<bcos::u256>(m_inner()->salt));
        }
        catch (std::exception const& e)
        {
            return emptySalt;
        }
    }

    void setCreateSalt(bcos::u256 createSalt) override
    {
        auto salt = boost::lexical_cast<std::string>(createSalt);
        m_inner()->salt = salt;
    }

    int32_t status() const override { return m_inner()->status; }
    void setStatus(int32_t status) override { m_inner()->status = status; }

    std::string_view message() const override { return m_inner()->message; }
    void setMessage(std::string message) override { m_inner()->message = message; }

    // for evm
    std::string_view newEVMContractAddress() const override
    {
        return m_inner()->newEVMContractAddress;
    }
    void setNewEVMContractAddress(std::string newEVMContractAddress) override
    {
        m_inner()->newEVMContractAddress = newEVMContractAddress;
    }
    std::string_view keyLockAcquired() const override { return m_inner()->keyLockAcquired; }
    void setKeyLockAcquired(std::string keyLock) override { m_inner()->keyLockAcquired = keyLock; }

    gsl::span<std::string const> keyLocks() const override;
    std::vector<std::string> takeKeyLocks() override;
    void setKeyLocks(std::vector<std::string> keyLocks) override;
    gsl::span<bcos::protocol::LogEntry const> const logEntries() const override;
    std::vector<bcos::protocol::LogEntry> takeLogEntries() override;
    void setLogEntries(std::vector<bcos::protocol::LogEntry> logEntries) override;

    bcostars::ExecutionMessage inner() const { return *(m_inner()); }

protected:
    virtual void decodeLogEntries();
    virtual void decodeKeyLocks();

private:
    std::function<bcostars::ExecutionMessage*()> m_inner;
    mutable std::vector<bcos::protocol::LogEntry> m_logEntries;
    std::vector<std::string> m_keyLocks;
};

class ExecutionMessageFactoryImpl : public bcos::protocol::ExecutionMessageFactory
{
public:
    using Ptr = std::shared_ptr<ExecutionMessageFactoryImpl>;
    using ConstPtr = std::shared_ptr<const ExecutionMessageFactoryImpl>;
    ExecutionMessageFactoryImpl() = default;
    ~ExecutionMessageFactoryImpl() override {}

    bcos::protocol::ExecutionMessage::UniquePtr createExecutionMessage() override
    {
        return std::make_unique<ExecutionMessageImpl>();
    }
};
}  // namespace protocol
}  // namespace bcostars