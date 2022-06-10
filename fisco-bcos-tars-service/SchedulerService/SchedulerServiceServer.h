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
 * @brief server for Scheduler
 * @file SchedulerServiceServer.h
 * @author: yujiechen
 * @date 2021-10-18
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework//dispatcher/SchedulerInterface.h>
#include <bcos-framework//protocol/BlockFactory.h>
#include <bcos-tars-protocol/tars/SchedulerService.h>
namespace bcostars
{
struct SchedulerServiceParam
{
    bcos::scheduler::SchedulerInterface::Ptr scheduler;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcos::protocol::BlockFactory::Ptr blockFactory;
};
class SchedulerServiceServer : public SchedulerService
{
public:
    SchedulerServiceServer(SchedulerServiceParam const& _param)
      : m_scheduler(_param.scheduler),
        m_cryptoSuite(_param.cryptoSuite),
        m_blockFactory(_param.blockFactory)
    {}
    ~SchedulerServiceServer() override {}

    void initialize() override {}
    void destroy() override {}

    bcostars::Error call(const bcostars::Transaction& _tx, bcostars::TransactionReceipt& _receipt,
        tars::TarsCurrentPtr current) override;

    bcostars::Error getCode(const std::string& contract, vector<tars::Char>& code,
        tars::TarsCurrentPtr current) override;

    bcostars::Error getABI(
        const std::string& contract, std::string& code, tars::TarsCurrentPtr current) override;

    bcostars::Error executeBlock(bcostars::Block const& _block, tars::Bool _verify,
        bcostars::BlockHeader& _executedHeader, tars::Bool& _sysBlock,
        tars::TarsCurrentPtr _current) override;
    bcostars::Error commitBlock(bcostars::BlockHeader const& _header,
        bcostars::LedgerConfig& _config, tars::TarsCurrentPtr _current) override;

    bcostars::Error registerExecutor(
        std::string const& _name, tars::TarsCurrentPtr _current) override;

    bcostars::Error preExecuteBlock(
        const bcostars::Block& _block, tars::Bool _verify, tars::TarsCurrentPtr current) override;

private:
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};
}  // namespace bcostars
