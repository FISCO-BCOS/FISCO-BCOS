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
 * @file ExecutionMessageImpl.cpp
 * @author: yujiechen
 * @date 2022-05-09
 */
#include "ExecutionMessageImpl.h"
#include <stdexcept>

bcostars::protocol::ExecutionMessageImpl::ExecutionMessageImpl()
  : m_inner([m_executionMessage = bcostars::ExecutionMessage()]() mutable {
        return &m_executionMessage;
    })
{
    decodeLogEntries();
    decodeKeyLocks();
}

bcostars::protocol::ExecutionMessageImpl::ExecutionMessageImpl(
    std::function<bcostars::ExecutionMessage*()> _inner)
  : m_inner(std::move(_inner))
{
    decodeLogEntries();
    decodeKeyLocks();
}

bcostars::protocol::ExecutionMessageImpl::~ExecutionMessageImpl() = default;

bcos::protocol::ExecutionMessage::Type bcostars::protocol::ExecutionMessageImpl::type() const
{
    return (Type)m_inner()->type;
}

void bcostars::protocol::ExecutionMessageImpl::setType(Type _type)
{
    m_inner()->type = _type;
}

bcos::crypto::HashType bcostars::protocol::ExecutionMessageImpl::transactionHash() const
{
    if (m_inner()->transactionHash.size() < bcos::crypto::HashType::SIZE)
    {
        return bcos::crypto::HashType();
    }
    return *(reinterpret_cast<bcos::crypto::HashType*>(m_inner()->transactionHash.data()));
}

void bcostars::protocol::ExecutionMessageImpl::setTransactionHash(bcos::crypto::HashType hash)
{
    m_inner()->transactionHash.assign(hash.begin(), hash.end());
}

int64_t bcostars::protocol::ExecutionMessageImpl::contextID() const
{
    return m_inner()->contextID;
}

void bcostars::protocol::ExecutionMessageImpl::setContextID(int64_t contextID)
{
    m_inner()->contextID = contextID;
}

int64_t bcostars::protocol::ExecutionMessageImpl::seq() const
{
    return m_inner()->seq;
}

void bcostars::protocol::ExecutionMessageImpl::setSeq(int64_t seq)
{
    m_inner()->seq = seq;
}

std::string_view bcostars::protocol::ExecutionMessageImpl::origin() const
{
    return m_inner()->origin;
}

void bcostars::protocol::ExecutionMessageImpl::setOrigin(std::string origin)
{
    m_inner()->origin = std::move(origin);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::from() const
{
    return m_inner()->from;
}

void bcostars::protocol::ExecutionMessageImpl::setFrom(std::string from)
{
    m_inner()->from = std::move(from);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::to() const
{
    return m_inner()->to;
}

void bcostars::protocol::ExecutionMessageImpl::setTo(std::string _to)
{
    m_inner()->to = std::move(_to);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::abi() const
{
    return m_inner()->abi;
}

void bcostars::protocol::ExecutionMessageImpl::setABI(std::string abi)
{
    m_inner()->abi = std::move(abi);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::value() const
{
    return m_inner()->value;
}

void bcostars::protocol::ExecutionMessageImpl::setValue(std::string value)
{
    m_inner()->value = std::move(value);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::gasPrice() const
{
    return m_inner()->gasPrice;
}

void bcostars::protocol::ExecutionMessageImpl::setGasPrice(std::string gasPrice)
{
    m_inner()->gasPrice = std::move(gasPrice);
}

int64_t bcostars::protocol::ExecutionMessageImpl::gasLimit() const
{
    return m_inner()->gasLimit;
}

void bcostars::protocol::ExecutionMessageImpl::setGasLimit(int64_t gasLimit)
{
    m_inner()->gasLimit = gasLimit;
}

std::string_view bcostars::protocol::ExecutionMessageImpl::maxFeePerGas() const
{
    return m_inner()->maxFeePerGas;
}

void bcostars::protocol::ExecutionMessageImpl::setMaxFeePerGas(std::string maxFeePerGas)
{
    m_inner()->maxFeePerGas = std::move(maxFeePerGas);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::maxPriorityFeePerGas() const
{
    return m_inner()->maxPriorityFeePerGas;
}

void bcostars::protocol::ExecutionMessageImpl::setMaxPriorityFeePerGas(
    std::string maxPriorityFeePerGas)
{
    m_inner()->maxPriorityFeePerGas = std::move(maxPriorityFeePerGas);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::effectiveGasPrice() const
{
    return m_inner()->effectiveGasPrice;
}

void bcostars::protocol::ExecutionMessageImpl::setEffectiveGasPrice(std::string effectiveGasPrice)
{
    m_inner()->effectiveGasPrice = std::move(effectiveGasPrice);
}

int32_t bcostars::protocol::ExecutionMessageImpl::depth() const
{
    return m_inner()->depth;
}

void bcostars::protocol::ExecutionMessageImpl::setDepth(int32_t depth)
{
    m_inner()->depth = depth;
}

bool bcostars::protocol::ExecutionMessageImpl::create() const
{
    return m_inner()->create;
}

void bcostars::protocol::ExecutionMessageImpl::setCreate(bool create)
{
    m_inner()->create = create;
}

bool bcostars::protocol::ExecutionMessageImpl::internalCreate() const
{
    return m_inner()->internalCreate;
}

void bcostars::protocol::ExecutionMessageImpl::setInternalCreate(bool internalCreate)
{
    m_inner()->internalCreate = internalCreate;
}

bool bcostars::protocol::ExecutionMessageImpl::internalCall() const
{
    return m_inner()->internalCall;
}

void bcostars::protocol::ExecutionMessageImpl::setInternalCall(bool internalCall)
{
    m_inner()->internalCall = internalCall;
}

int64_t bcostars::protocol::ExecutionMessageImpl::gasAvailable() const
{
    return m_inner()->gasAvailable;
}

void bcostars::protocol::ExecutionMessageImpl::setGasAvailable(int64_t gasAvailable)
{
    m_inner()->gasAvailable = gasAvailable;
}

bcos::bytesConstRef bcostars::protocol::ExecutionMessageImpl::data() const
{
    return bcos::bytesConstRef(
        reinterpret_cast<const bcos::byte*>(m_inner()->data.data()), m_inner()->data.size());
}

bcos::bytes bcostars::protocol::ExecutionMessageImpl::takeData()
{
    return bcos::bytes(m_inner()->data.begin(), m_inner()->data.end());
}

void bcostars::protocol::ExecutionMessageImpl::setData(bcos::bytes data)
{
    m_inner()->data.assign(data.begin(), data.end());
}

bool bcostars::protocol::ExecutionMessageImpl::staticCall() const
{
    return m_inner()->staticCall;
}

void bcostars::protocol::ExecutionMessageImpl::setStaticCall(bool staticCall)
{
    m_inner()->staticCall = staticCall;
}

std::optional<bcos::u256> bcostars::protocol::ExecutionMessageImpl::createSalt() const
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
    catch (std::exception const&)
    {
        return emptySalt;
    }
}

void bcostars::protocol::ExecutionMessageImpl::setCreateSalt(bcos::u256 createSalt)
{
    auto salt = boost::lexical_cast<std::string>(createSalt);
    m_inner()->salt = std::move(salt);
}

int32_t bcostars::protocol::ExecutionMessageImpl::status() const
{
    return m_inner()->status;
}

void bcostars::protocol::ExecutionMessageImpl::setStatus(int32_t status)
{
    m_inner()->status = status;
}

int32_t bcostars::protocol::ExecutionMessageImpl::evmStatus() const
{
    return m_inner()->evmStatus;
}

void bcostars::protocol::ExecutionMessageImpl::setEvmStatus(int32_t evmStatus)
{
    m_inner()->evmStatus = evmStatus;
}

std::string_view bcostars::protocol::ExecutionMessageImpl::message() const
{
    return m_inner()->message;
}

void bcostars::protocol::ExecutionMessageImpl::setMessage(std::string message)
{
    m_inner()->message = std::move(message);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::newEVMContractAddress() const
{
    return m_inner()->newEVMContractAddress;
}

void bcostars::protocol::ExecutionMessageImpl::setNewEVMContractAddress(
    std::string newEVMContractAddress)
{
    m_inner()->newEVMContractAddress = std::move(newEVMContractAddress);
}

std::string_view bcostars::protocol::ExecutionMessageImpl::keyLockAcquired() const
{
    return m_inner()->keyLockAcquired;
}

void bcostars::protocol::ExecutionMessageImpl::setKeyLockAcquired(std::string keyLock)
{
    m_inner()->keyLockAcquired = std::move(keyLock);
}

bool bcostars::protocol::ExecutionMessageImpl::delegateCall() const
{
    return m_inner()->delegateCall;
}

void bcostars::protocol::ExecutionMessageImpl::setDelegateCall(bool delegateCall)
{
    m_inner()->delegateCall = delegateCall;
}

std::string_view bcostars::protocol::ExecutionMessageImpl::delegateCallAddress() const
{
    return m_inner()->delegateCallAddress;
}

void bcostars::protocol::ExecutionMessageImpl::setDelegateCallAddress(
    std::string delegateCallAddress)
{
    m_inner()->delegateCallAddress = std::move(delegateCallAddress);
}

bcos::bytesConstRef bcostars::protocol::ExecutionMessageImpl::delegateCallCode() const
{
    return bcos::bytesConstRef(
        reinterpret_cast<const bcos::byte*>(m_inner()->delegateCallCode.data()),
        m_inner()->delegateCallCode.size());
}

bcos::bytes bcostars::protocol::ExecutionMessageImpl::takeDelegateCallCode()
{
    return bcos::bytes(m_inner()->delegateCallCode.begin(), m_inner()->delegateCallCode.end());
}

void bcostars::protocol::ExecutionMessageImpl::setDelegateCallCode(bcos::bytes delegateCallCode)
{
    m_inner()->delegateCallCode.assign(delegateCallCode.begin(), delegateCallCode.end());
}

std::string_view bcostars::protocol::ExecutionMessageImpl::delegateCallSender() const
{
    return m_inner()->delegateCallSender;
}

void bcostars::protocol::ExecutionMessageImpl::setDelegateCallSender(
    std::string delegateCallSender)
{
    m_inner()->delegateCallSender = std::move(delegateCallSender);
}

bool bcostars::protocol::ExecutionMessageImpl::hasContractTableChanged() const
{
    return m_inner()->hasContractTableChanged;
}

void bcostars::protocol::ExecutionMessageImpl::setHasContractTableChanged(bool hasChanged)
{
    m_inner()->hasContractTableChanged = hasChanged;
}

std::string_view bcostars::protocol::ExecutionMessageImpl::nonceView() const
{
    throw std::runtime_error("not implemented");
}

void bcostars::protocol::ExecutionMessageImpl::setNonce(std::string nonce)
{
    (void)nonce;
    throw std::runtime_error("not implemented");
}

std::string bcostars::protocol::ExecutionMessageImpl::nonce() const
{
    throw std::runtime_error("not implemented");
}

uint8_t bcostars::protocol::ExecutionMessageImpl::txType() const
{
    throw std::runtime_error("not implemented");
}

void bcostars::protocol::ExecutionMessageImpl::setTxType(uint8_t txType)
{
    (void)txType;
    throw std::runtime_error("not implemented");
}

bcostars::ExecutionMessage bcostars::protocol::ExecutionMessageImpl::inner() const
{
    return *(m_inner());
}

bcos::protocol::ExecutionMessage::UniquePtr
bcostars::protocol::ExecutionMessageFactoryImpl::createExecutionMessage()
{
    return std::make_unique<ExecutionMessageImpl>();
}

bcostars::protocol::ExecutionMessageFactoryImpl::~ExecutionMessageFactoryImpl() = default;

void bcostars::protocol::ExecutionMessageImpl::decodeLogEntries()
{
    m_logEntries.reserve(m_inner()->logEntries.size());
    for (auto& it : m_inner()->logEntries)
    {
        auto bcosLogEntry = toBcosLogEntry(it);
        m_logEntries.emplace_back(std::move(bcosLogEntry));
    }
}

void bcostars::protocol::ExecutionMessageImpl::decodeKeyLocks()
{
    for (auto const& _keyLock : m_inner()->keyLocks)
    {
        m_keyLocks.emplace_back(_keyLock);
    }
}

gsl::span<bcos::protocol::LogEntry const> const
bcostars::protocol::ExecutionMessageImpl::logEntries() const
{
    return m_logEntries;
}
std::vector<bcos::protocol::LogEntry> bcostars::protocol::ExecutionMessageImpl::takeLogEntries()
{
    return std::move(m_logEntries);
}

void bcostars::protocol::ExecutionMessageImpl::setLogEntries(
    std::vector<bcos::protocol::LogEntry> _logEntries)
{
    m_logEntries = _logEntries;
    m_inner()->logEntries.clear();
    m_inner()->logEntries.reserve(_logEntries.size());

    for (auto& it : _logEntries)
    {
        auto tarsLogEntry = toTarsLogEntry(it);
        m_inner()->logEntries.emplace_back(std::move(tarsLogEntry));
    }
}

gsl::span<std::string const> bcostars::protocol::ExecutionMessageImpl::keyLocks() const
{
    return m_keyLocks;
}
std::vector<std::string> bcostars::protocol::ExecutionMessageImpl::takeKeyLocks()
{
    // Note: must clear the tars-container here when takeKeyLocks
    m_inner()->keyLocks.clear();
    return std::move(m_keyLocks);
}

void bcostars::protocol::ExecutionMessageImpl::setKeyLocks(std::vector<std::string> keyLocks)
{
    m_keyLocks = std::move(keyLocks);
    // Note: must clear the tars-container here before set new keyLocks
    m_inner()->keyLocks.clear();
    for (auto const& keyLock : m_keyLocks)
    {
        m_inner()->keyLocks.emplace_back(keyLock);
    }
}