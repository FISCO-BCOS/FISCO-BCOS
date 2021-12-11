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
 * @file SchedulerServiceClient.cpp
 * @author: yujiechen
 * @date 2021-10-17
 */
#include "SchedulerServiceClient.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"

using namespace bcostars;

void SchedulerServiceClient::call(bcos::protocol::Transaction::Ptr _tx,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::TransactionReceipt::Ptr&&)> _callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&, bcos::protocol::TransactionReceipt::Ptr&&)>
                     _callback,
            bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
          : m_callback(_callback), m_cryptoSuite(_cryptoSuite)
        {}
        ~Callback() override {}

        void callback_call(
            const bcostars::Error& ret, const bcostars::TransactionReceipt& _receipt) override
        {
            auto bcosReceipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                m_cryptoSuite, [m_receipt = std::move(_receipt)]() mutable { return &m_receipt; });
            m_callback(toBcosError(ret), bcosReceipt);
        }

        void callback_call_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr);
        }

    private:
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::TransactionReceipt::Ptr&&)>
            m_callback;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };
    auto tarsTx = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(_tx);
    m_prx->async_call(new Callback(_callback, m_cryptoSuite), tarsTx->inner());
}

void SchedulerServiceClient::getCode(
    std::string_view contract, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::bytes)> callback)
          : m_callback(std::move(callback))
        {}
        ~Callback() override {}

        void callback_getCode(const bcostars::Error& ret, const vector<tars::Char>& code) override
        {
            bcos::bytes outCode(code.begin(), code.end());

            m_callback(toBcosError(ret), std::move(outCode));
        }

        void callback_getCode_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), {});
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::bytes)> m_callback;
    };

    m_prx->async_getCode(new Callback(std::move(callback)), std::string(contract));
}
