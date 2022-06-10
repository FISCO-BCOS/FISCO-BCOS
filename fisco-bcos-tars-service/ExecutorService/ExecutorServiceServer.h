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
#pragma once
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework//executor/ParallelTransactionExecutorInterface.h>
#include <bcos-tars-protocol/tars/ExecutorService.h>
namespace bcostars
{
struct ExecutorServiceParam
{
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
};
class ExecutorServiceServer : public ExecutorService
{
public:
    using Ptr = std::shared_ptr<ExecutorServiceServer>;
    ExecutorServiceServer(ExecutorServiceParam const& _param)
      : m_executor(_param.executor), m_cryptoSuite(_param.cryptoSuite)
    {}
    ~ExecutorServiceServer() override {}

    void initialize() override {}
    void destroy() override {}

    bcostars::Error nextBlockHeader(tars::Int64 schedulerTermId,
        bcostars::BlockHeader const& _blockHeader, tars::TarsCurrentPtr _current) override;
    bcostars::Error executeTransaction(bcostars::ExecutionMessage const& _input,
        bcostars::ExecutionMessage& _output, tars::TarsCurrentPtr _current) override;
    bcostars::Error dmcExecuteTransactions(std::string const& _contractAddress,
        std::vector<bcostars::ExecutionMessage> const& _inputs,
        std::vector<bcostars::ExecutionMessage>& _ouptputs, tars::TarsCurrentPtr _current) override;
    bcostars::Error dagExecuteTransactions(std::vector<bcostars::ExecutionMessage> const& _inputs,
        std::vector<bcostars::ExecutionMessage>& _ouptputs, tars::TarsCurrentPtr _current) override;
    bcostars::Error call(bcostars::ExecutionMessage const& _input,
        bcostars::ExecutionMessage& _output, tars::TarsCurrentPtr _current) override;

    bcostars::Error getHash(tars::Int64 _blockNumber, std::vector<tars::Char>& _hash,
        tars::TarsCurrentPtr _current) override;

    bcostars::Error prepare(
        bcostars::TwoPCParams const& _params, tars::TarsCurrentPtr _current) override;
    bcostars::Error commit(
        bcostars::TwoPCParams const& _params, tars::TarsCurrentPtr _current) override;
    bcostars::Error rollback(
        bcostars::TwoPCParams const& _params, tars::TarsCurrentPtr _current) override;

    bcostars::Error getCode(std::string const& _contractAddress, std::vector<tars::Char>& _code,
        tars::TarsCurrentPtr _current) override;
    bcostars::Error getABI(std::string const& _contractAddress, std::string& _abi,
        tars::TarsCurrentPtr _current) override;
    bcostars::Error reset(tars::TarsCurrentPtr _current) override;

private:
    bcos::executor::ParallelTransactionExecutorInterface::Ptr m_executor;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace bcostars