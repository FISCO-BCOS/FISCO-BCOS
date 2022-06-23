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
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
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

void SchedulerServiceClient::getABI(
    std::string_view contract, std::function<void(bcos::Error::Ptr, std::string)> callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, std::string)> callback)
          : m_callback(std::move(callback))
        {}
        ~Callback() override {}

        void callback_getABI(const bcostars::Error& ret, const std::string& abi) override
        {
            std::string outCode(abi.begin(), abi.end());

            m_callback(toBcosError(ret), std::move(outCode));
        }

        void callback_getABI_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), {});
        }

    private:
        std::function<void(bcos::Error::Ptr, std::string)> m_callback;
    };

    m_prx->async_getABI(new Callback(std::move(callback)), std::string(contract));
}


void SchedulerServiceClient::executeBlock(bcos::protocol::Block::Ptr _block, bool _verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool)> _callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool)>
                     _callback,
            bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
          : m_callback(std::move(_callback)), m_cryptoSuite(_cryptoSuite)
        {}
        ~Callback() override {}

        void callback_executeBlock(const bcostars::Error& ret,
            const bcostars::BlockHeader& _executedHeader, tars::Bool _sysBlock) override
        {
            auto bcosBlockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
                m_cryptoSuite, [m_header = _executedHeader]() mutable { return &m_header; });
            m_callback(toBcosError(ret), std::move(bcosBlockHeader), _sysBlock);
        }

        void callback_executeBlock_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr, false);
        }

    private:
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool)>
            m_callback;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };
    auto tarsBlock = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(_block);
    m_prx->async_executeBlock(
        new Callback(std::move(_callback), m_cryptoSuite), tarsBlock->inner(), _verify);
}

void SchedulerServiceClient::commitBlock(bcos::protocol::BlockHeader::Ptr _blockHeader,
    std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> _callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(
            std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)>&& _callback,
            bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
          : m_callback(std::move(_callback)), m_cryptoSuite(_cryptoSuite)
        {}
        ~Callback() override {}

        void callback_commitBlock(
            const bcostars::Error& ret, const bcostars::LedgerConfig& _ledgerConfig) override
        {
            m_callback(
                toBcosError(ret), toLedgerConfig(_ledgerConfig, m_cryptoSuite->keyFactory()));
        }

        void callback_commitBlock_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr);
        }

    private:
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> m_callback;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };
    auto tarsBlockHeader =
        std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(_blockHeader);
    m_prx->async_commitBlock(
        new Callback(std::move(_callback), m_cryptoSuite), tarsBlockHeader->inner());
}

void SchedulerServiceClient::registerExecutor(std::string _name,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr,
    std::function<void(bcos::Error::Ptr&&)> _callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&)> _callback) : m_callback(_callback) {}
        ~Callback() override {}

        void callback_registerExecutor(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_registerExecutor_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr&&)> m_callback;
    };
    m_prx->async_registerExecutor(new Callback(_callback), _name);
}

void SchedulerServiceClient::preExecuteBlock(
    bcos::protocol::Block::Ptr block, bool verify, std::function<void(bcos::Error::Ptr&&)> callback)
{
    class Callback : public SchedulerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&)> _callback) : m_callback(_callback) {}
        ~Callback() override {}

        void callback_registerExecutor(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_registerExecutor_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr&&)> m_callback;
    };
    auto tarsBlock = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(block);
    m_prx->async_preExecuteBlock(new Callback(callback), tarsBlock->inner(), verify);
}