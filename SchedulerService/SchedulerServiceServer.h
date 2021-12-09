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
#include <bcos-framework/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/dispatcher/SchedulerInterface.h>
#include <bcos-tars-protocol/tars/SchedulerService.h>
namespace bcostars
{
struct SchedulerServiceParam
{
    bcos::scheduler::SchedulerInterface::Ptr scheduler;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
};
class SchedulerServiceServer : public SchedulerService
{
public:
    SchedulerServiceServer(SchedulerServiceParam const& _param)
      : m_scheduler(_param.scheduler), m_cryptoSuite(_param.cryptoSuite)
    {}
    ~SchedulerServiceServer() override {}

    void initialize() override {}
    void destroy() override {}

    bcostars::Error call(const bcostars::Transaction& _tx, bcostars::TransactionReceipt& _receipt,
        tars::TarsCurrentPtr current) override;

    bcostars::Error getCode(const std::string& contract, vector<tars::Char>& code,
        tars::TarsCurrentPtr current) override;

private:
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace bcostars
