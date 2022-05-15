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
 * @file ExecutorServiceServer.h
 * @author: yujiechen
 * @date 2022-5-10
 */
#include "ExecutorServiceServer.h"
#include <bcos-tars-protocol/Common.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/ExecutionMessageImpl.h>

using namespace bcostars;

bcostars::Error ExecutorServiceServer::nextBlockHeader(
    bcostars::BlockHeader const& _blockHeader, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        m_cryptoSuite, [m_header = _blockHeader]() mutable { return &m_header; });
    m_executor->nextBlockHeader(header, [_current](bcos::Error::UniquePtr _error) {
        async_response_nextBlockHeader(_current, toTarsError(std::move(_error)));
    });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::executeTransaction(bcostars::ExecutionMessage const& _input,
    bcostars::ExecutionMessage&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto executionMessageImpl = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
        [m_message = _input]() mutable { return &m_message; });
    m_executor->executeTransaction(
        std::move(executionMessageImpl), [_current](bcos::Error::UniquePtr _error,
                                             bcos::protocol::ExecutionMessage::UniquePtr _output) {
            auto executionMsgImpl =
                std::move((bcostars::protocol::ExecutionMessageImpl::UniquePtr&)_output);
            async_response_executeTransaction(
                _current, toTarsError(std::move(_error)), executionMsgImpl->inner());
        });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::dmcExecuteTransactions(std::string const& _contractAddress,
    std::vector<bcostars::ExecutionMessage> const& _inputs,
    std::vector<bcostars::ExecutionMessage>&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto executionMessages =
        std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();
    for (auto const& input : _inputs)
    {
        auto msg = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
            [m_message = input]() mutable { return &m_message; });
        executionMessages->emplace_back(std::move(msg));
    }
    m_executor->dmcExecuteTransactions(_contractAddress, *executionMessages,
        [_current](bcos::Error::UniquePtr _error,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr> _outputs) {
            std::vector<bcostars::ExecutionMessage> tarsOutputs;
            for (auto const& it : _outputs)
            {
                auto executionMsgImpl =
                    std::move((bcostars::protocol::ExecutionMessageImpl::UniquePtr&)it);
                tarsOutputs.emplace_back(executionMsgImpl->inner());
            }
            async_response_dmcExecuteTransactions(
                _current, toTarsError(std::move(_error)), std::move(tarsOutputs));
        });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::dagExecuteTransactions(
    std::vector<bcostars::ExecutionMessage> const& _inputs,
    std::vector<bcostars::ExecutionMessage>&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> executionMessages;
    for (auto const& input : _inputs)
    {
        auto msg = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
            [m_message = input]() mutable { return &m_message; });
        executionMessages.emplace_back(std::move(msg));
    }
    m_executor->dagExecuteTransactions(
        executionMessages, [_current](bcos::Error::UniquePtr _error,
                               std::vector<bcos::protocol::ExecutionMessage::UniquePtr> _outputs) {
            std::vector<bcostars::ExecutionMessage> tarsOutputs;
            for (auto const& it : _outputs)
            {
                auto executionMsgImpl =
                    std::move((bcostars::protocol::ExecutionMessageImpl::UniquePtr&)it);
                tarsOutputs.emplace_back(executionMsgImpl->inner());
            }
            async_response_dagExecuteTransactions(
                _current, toTarsError(std::move(_error)), std::move(tarsOutputs));
        });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::call(bcostars::ExecutionMessage const& _input,
    bcostars::ExecutionMessage& _output, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto msg = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
        [m_message = _input]() mutable { return &m_message; });
    m_executor->call(std::move(msg), [_current](bcos::Error::UniquePtr _error,
                                         bcos::protocol::ExecutionMessage::UniquePtr _output) {
        auto executionMsgImpl =
            std::move((bcostars::protocol::ExecutionMessageImpl::UniquePtr&)_output);
        async_response_call(_current, toTarsError(std::move(_error)), executionMsgImpl->inner());
    });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::getHash(
    tars::Int64 _blockNumber, std::vector<tars::Char>&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_executor->getHash(
        _blockNumber, [_current](bcos::Error::UniquePtr _error, bcos::crypto::HashType _hash) {
            std::vector<tars::Char> hashData(_hash.begin(), _hash.end());
            async_response_getHash(_current, toTarsError(std::move(_error)), std::move(hashData));
        });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::prepare(
    bcostars::TwoPCParams const& _params, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto bcosTwoPCParams = toBcosTwoPCParams(_params);
    m_executor->prepare(bcosTwoPCParams, [_current](bcos::Error::Ptr _error) {
        async_response_prepare(_current, toTarsError(_error));
    });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::commit(
    bcostars::TwoPCParams const& _params, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto bcosTwoPCParams = toBcosTwoPCParams(_params);
    m_executor->commit(bcosTwoPCParams, [_current](bcos::Error::Ptr _error) {
        async_response_commit(_current, toTarsError(_error));
    });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::rollback(
    bcostars::TwoPCParams const& _params, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto bcosTwoPCParams = toBcosTwoPCParams(_params);
    m_executor->rollback(bcosTwoPCParams, [_current](bcos::Error::Ptr _error) {
        async_response_rollback(_current, toTarsError(_error));
    });
    return bcostars::Error();
}

bcostars::Error ExecutorServiceServer::getCode(
    std::string const& _contractAddress, std::vector<tars::Char>&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_executor->getCode(
        _contractAddress, [_current](bcos::Error::Ptr _error, bcos::bytes _codeBytes) {
            std::vector<tars::Char> code(_codeBytes.begin(), _codeBytes.end());
            async_response_getCode(_current, toTarsError(_error), std::move(code));
        });
    return bcostars::Error();
}
bcostars::Error ExecutorServiceServer::getABI(
    std::string const& _contractAddress, std::string& _abi, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_executor->getABI(_contractAddress, [_current](bcos::Error::Ptr _error, std::string _abi) {
        async_response_getABI(_current, toTarsError(_error), std::move(_abi));
    });
    return bcostars::Error();
}
bcostars::Error ExecutorServiceServer::reset(tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_executor->reset([_current](bcos::Error::Ptr _error) {
        async_response_reset(_current, toTarsError(_error));
    });
    return bcostars::Error();
}