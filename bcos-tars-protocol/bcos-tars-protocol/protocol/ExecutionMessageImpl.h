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

#include <bcos-tars-protocol/Common.h>
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include <bcos-framework/executor/ExecutionMessage.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-tars-protocol/tars/ExecutionMessage.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
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
    ExecutionMessageImpl();
    ExecutionMessageImpl(std::function<bcostars::ExecutionMessage*()> _inner);

    ~ExecutionMessageImpl() override;

    Type type() const override;
    void setType(Type _type) override;

    bcos::crypto::HashType transactionHash() const override;
    void setTransactionHash(bcos::crypto::HashType hash) override;

    int64_t contextID() const override;
    void setContextID(int64_t contextID) override;

    int64_t seq() const override;
    void setSeq(int64_t seq) override;

    std::string_view origin() const override;
    void setOrigin(std::string origin) override;

    std::string_view from() const override;
    void setFrom(std::string from) override;

    std::string_view to() const override;
    void setTo(std::string _to) override;

    std::string_view abi() const override;
    void setABI(std::string abi) override;

    // balance transfer
    std::string_view value() const override;
    void setValue(std::string value) override;

    std::string_view gasPrice() const override;
    void setGasPrice(std::string gasPrice) override;

    int64_t gasLimit() const override;
    void setGasLimit(int64_t gasLimit) override;

    std::string_view maxFeePerGas() const override;
    void setMaxFeePerGas(std::string maxFeePerGas) override;

    std::string_view maxPriorityFeePerGas() const override;
    void setMaxPriorityFeePerGas(std::string maxPriorityFeePerGas) override;

    std::string_view effectiveGasPrice() const override;
    void setEffectiveGasPrice(std::string effectiveGasPrice) override;

    int32_t depth() const override;
    void setDepth(int32_t depth) override;

    bool create() const override;
    void setCreate(bool create) override;

    bool internalCreate() const override;
    void setInternalCreate(bool internalCreate) override;

    bool internalCall() const override;
    void setInternalCall(bool internalCall) override;


    int64_t gasAvailable() const override;
    void setGasAvailable(int64_t gasAvailable) override;

    bcos::bytesConstRef data() const override;

    bcos::bytes takeData() override;
    void setData(bcos::bytes data) override;

    bool staticCall() const override;
    void setStaticCall(bool staticCall) override;

    // for evm
    std::optional<bcos::u256> createSalt() const override;

    void setCreateSalt(bcos::u256 createSalt) override;

    int32_t status() const override;
    void setStatus(int32_t status) override;

    int32_t evmStatus() const override;
    void setEvmStatus(int32_t evmStatus) override;

    std::string_view message() const override;
    void setMessage(std::string message) override;

    // for evm
    std::string_view newEVMContractAddress() const override;
    void setNewEVMContractAddress(std::string newEVMContractAddress) override;
    std::string_view keyLockAcquired() const override;
    void setKeyLockAcquired(std::string keyLock) override;

    gsl::span<std::string const> keyLocks() const override;
    std::vector<std::string> takeKeyLocks() override;
    void setKeyLocks(std::vector<std::string> keyLocks) override;
    gsl::span<bcos::protocol::LogEntry const> const logEntries() const override;
    std::vector<bcos::protocol::LogEntry> takeLogEntries() override;
    void setLogEntries(std::vector<bcos::protocol::LogEntry> logEntries) override;

    bool delegateCall() const override;
    void setDelegateCall(bool delegateCall) override;

    std::string_view delegateCallAddress() const override;
    void setDelegateCallAddress(std::string delegateCallAddress) override;


    bcos::bytesConstRef delegateCallCode() const override;

    bcos::bytes takeDelegateCallCode() override;
    void setDelegateCallCode(bcos::bytes delegateCallCode) override;

    std::string_view delegateCallSender() const override;
    void setDelegateCallSender(std::string delegateCallSender) override;

    bool hasContractTableChanged() const override;
    void setHasContractTableChanged(bool hasChanged) override;

    // TODO)): should implement web3 nonce logic in max?
    std::string_view nonceView() const override;
    void setNonce(std::string nonce) override;
    std::string nonce() const override;

    uint8_t txType() const override;
    void setTxType(uint8_t txType) override;

    bcostars::ExecutionMessage inner() const;

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
    ~ExecutionMessageFactoryImpl() override;

    bcos::protocol::ExecutionMessage::UniquePtr createExecutionMessage() override;
};
}  // namespace protocol
}  // namespace bcostars