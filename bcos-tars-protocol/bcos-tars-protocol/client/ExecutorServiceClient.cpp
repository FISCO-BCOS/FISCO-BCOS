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
 * @file ExecutorServiceClient.cpp
 * @author: yujiechen
 * @date 2022-5-9
 */
#include "ExecutorServiceClient.h"
#include "../Common.h"
#include "../ErrorConverter.h"
#include "../protocol/BlockHeaderImpl.h"
#include "../protocol/ExecutionMessageImpl.h"
#include <memory>

using namespace bcostars;

template <typename... Args>
class AsyncCallback
{
public:
    AsyncCallback(std::weak_ptr<bcos::ThreadPool> threadPool, std::function<void(Args...)> callback)
      : m_pool(threadPool), m_callback(std::move(callback))
    {}

    void operator()(Args&&... args)
    {
        auto pool = m_pool.lock();
        if (pool)
        {
            // m_callback(std::move(args)...);
            pool->template enqueue(
                [callback = std::move(m_callback),
                    m_args = std::make_shared<std::tuple<Args...>>(
                        std::make_tuple(std::forward<Args>(args)...))]() mutable {
                    std::apply(callback, std::move(*m_args));
                });
        }
    }

private:
    std::weak_ptr<bcos::ThreadPool> m_pool;
    std::function<void(Args...)> m_callback;
};

void ExecutorServiceClient::status(
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr)>&&
                _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_status(
            const bcostars::Error& ret, const bcostars::ExecutorStatus& _output) override
        {
            auto error = toUniqueBcosError(ret);
            auto status = std::make_unique<bcos::protocol::ExecutorStatus>();
            if (!error)
            {
                status->setSeq(_output.seq);
            }
            m_callback(std::move(error), std::move(status));
        }

        void callback_status_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret), std::make_unique<bcos::protocol::ExecutorStatus>());
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr, bcos::protocol::ExecutorStatus::UniquePtr> m_callback;
    };
    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_status(new Callback(m_callbackPool, std::move(callback)));
}

void ExecutorServiceClient::nextBlockHeader(int64_t schedulerTermId,
    const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::UniquePtr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_nextBlockHeader(const bcostars::Error& ret) override
        {
            m_callback(toUniqueBcosError(ret));
        }

        void callback_nextBlockHeader_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret));
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr> m_callback;
    };
    auto blockHeaderImpl =
        std::dynamic_pointer_cast<const bcostars::protocol::BlockHeaderImpl>(blockHeader);
    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_nextBlockHeader(
        new Callback(m_callbackPool, std::move(callback)), schedulerTermId,
        blockHeaderImpl->inner());
}

void ExecutorServiceClient::executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(
                bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_executeTransaction(
            const bcostars::Error& ret, bcostars::ExecutionMessage const& executionMessage) override
        {
            auto executionMsgImpl = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
                [m_executionMessage = executionMessage]() mutable { return &m_executionMessage; });
            m_callback(toUniqueBcosError(ret), std::move(executionMsgImpl));
        }

        void callback_executeTransaction_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret), nullptr);
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr>
            m_callback;
    };
    auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*input);

    // timeout is 2 min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_executeTransaction(
            new Callback(m_callbackPool, std::move(callback)), executionMsgImpl.inner());
}

void ExecutorServiceClient ::call(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(
                bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_call(
            const bcostars::Error& ret, bcostars::ExecutionMessage const& executionMessage) override
        {
            auto bcosExecutionMessage = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
                [m_executionMessage = executionMessage]() mutable { return &m_executionMessage; });
            m_callback(toUniqueBcosError(ret), std::move(bcosExecutionMessage));
        }

        void callback_call_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret), nullptr);
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr>
            m_callback;
    };
    auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*input);
    // timeout is 2min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_call(new Callback(m_callbackPool, std::move(callback)), executionMsgImpl.inner());
}

void ExecutorServiceClient::executeTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_executeTransactions(const bcostars::Error& ret,
            std::vector<bcostars::ExecutionMessage> const& executionMessages) override
        {
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr> inputList;
            for (auto const& it : executionMessages)
            {
                auto bcosExecutionMessage =
                    std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
                        [m_executionMessage = it]() mutable { return &m_executionMessage; });
                inputList.emplace_back(std::move(bcosExecutionMessage));
            }
            m_callback(toUniqueBcosError(ret), std::move(inputList));
        }

        void callback_executeTransactions_exception(tars::Int32 ret) override
        {
            m_callback(
                toUniqueBcosError(ret), std::vector<bcos::protocol::ExecutionMessage::UniquePtr>());
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>
            m_callback;
    };
    std::vector<bcostars::ExecutionMessage> tarsInputs;
    for (auto const& it : inputs)
    {
        auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*it);
        tarsInputs.emplace_back(executionMsgImpl.inner());
    }
    // timeout is 2min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_executeTransactions(
            new Callback(m_callbackPool, std::move(callback)), contractAddress, tarsInputs);
}

void ExecutorServiceClient::preExecuteTransactions(int64_t schedulerTermId,
    const bcos::protocol::BlockHeader::ConstPtr& blockHeader, std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(bcos::Error::UniquePtr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_preExecuteTransactions(const bcostars::Error& ret) override
        {
            m_callback(toUniqueBcosError(ret));
        }

        void callback_preExecuteTransactions_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret));
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr> m_callback;
    };

    auto blockHeaderImpl =
        std::dynamic_pointer_cast<const bcostars::protocol::BlockHeaderImpl>(blockHeader);
    std::vector<bcostars::ExecutionMessage> tarsInputs;
    for (auto const& it : inputs)
    {
        auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*it);
        tarsInputs.emplace_back(executionMsgImpl.inner());
    }
    // timeout is 2min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_preExecuteTransactions(new Callback(m_callbackPool, std::move(callback)),
            schedulerTermId, blockHeaderImpl->inner(), contractAddress, tarsInputs);
}

void ExecutorServiceClient::dmcExecuteTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_dmcExecuteTransactions(const bcostars::Error& ret,
            std::vector<bcostars::ExecutionMessage> const& executionMessages) override
        {
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr> inputList;
            for (auto const& it : executionMessages)
            {
                auto bcosExecutionMessage =
                    std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
                        [m_executionMessage = it]() mutable { return &m_executionMessage; });
                inputList.emplace_back(std::move(bcosExecutionMessage));
            }
            m_callback(toUniqueBcosError(ret), std::move(inputList));
        }

        void callback_dmcExecuteTransactions_exception(tars::Int32 ret) override
        {
            m_callback(
                toUniqueBcosError(ret), std::vector<bcos::protocol::ExecutionMessage::UniquePtr>());
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>
            m_callback;
    };
    std::vector<bcostars::ExecutionMessage> tarsInputs;
    for (auto const& it : inputs)
    {
        auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*it);
        tarsInputs.emplace_back(executionMsgImpl.inner());
    }
    // timeout is 2min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_dmcExecuteTransactions(
            new Callback(m_callbackPool, std::move(callback)), contractAddress, tarsInputs);
}

void ExecutorServiceClient::dagExecuteTransactions(
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_dagExecuteTransactions(const bcostars::Error& ret,
            std::vector<bcostars::ExecutionMessage> const& executionMessages) override
        {
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr> inputList;
            for (auto const& it : executionMessages)
            {
                auto bcosExecutionMessage =
                    std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
                        [m_executionMessage = it]() mutable { return &m_executionMessage; });
                inputList.emplace_back(std::move(bcosExecutionMessage));
            }
            m_callback(toUniqueBcosError(ret), std::move(inputList));
        }

        void callback_dagExecuteTransactions_exception(tars::Int32 ret) override
        {
            m_callback(
                toUniqueBcosError(ret), std::vector<bcos::protocol::ExecutionMessage::UniquePtr>());
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>
            m_callback;
    };
    std::vector<bcostars::ExecutionMessage> tarsInput;
    for (auto const& it : inputs)
    {
        auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*it);
        tarsInput.emplace_back(executionMsgImpl.inner());
    }
    // timeout is 2min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_dagExecuteTransactions(
            new Callback(m_callbackPool, std::move(callback)), tarsInput);
}

void ExecutorServiceClient::dmcCall(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(
                bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_dmcCall(
            const bcostars::Error& ret, bcostars::ExecutionMessage const& executionMessage) override
        {
            auto bcosExecutionMessage = std::make_unique<bcostars::protocol::ExecutionMessageImpl>(
                [m_executionMessage = executionMessage]() mutable { return &m_executionMessage; });
            m_callback(toUniqueBcosError(ret), std::move(bcosExecutionMessage));
        }

        void callback_dmcCall_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret), nullptr);
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr>
            m_callback;
    };
    auto& executionMsgImpl = dynamic_cast<bcostars::protocol::ExecutionMessageImpl&>(*input);
    // timeout is 2min
    m_prx->tars_set_timeout(2 * 60 * 1000)
        ->async_dmcCall(
            new Callback(m_callbackPool, std::move(callback)), executionMsgImpl.inner());
}

void ExecutorServiceClient::getHash(bcos::protocol::BlockNumber number,
    std::function<void(bcos::Error::UniquePtr, bcos::crypto::HashType)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::UniquePtr, bcos::crypto::HashType)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_getHash(
            const bcostars::Error& ret, std::vector<tars::Char> const& hashBytes) override
        {
            auto hash = bcos::crypto::HashType(
                reinterpret_cast<const bcos::byte*>(hashBytes.data()), hashBytes.size());
            m_callback(toUniqueBcosError(ret), std::move(hash));
        }

        void callback_getHash_exception(tars::Int32 ret) override
        {
            m_callback(toUniqueBcosError(ret), bcos::crypto::HashType());
        }

    private:
        AsyncCallback<bcos::Error::UniquePtr, bcos::crypto::HashType> m_callback;
    };
    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_getHash(
        new Callback(m_callbackPool, std::move(callback)), number);
}

void ExecutorServiceClient::prepare(
    const bcos::protocol::TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::Ptr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_prepare(const bcostars::Error& ret) override { m_callback(toBcosError(ret)); }

        void callback_prepare_exception(tars::Int32 ret) override { m_callback(toBcosError(ret)); }

    private:
        AsyncCallback<bcos::Error::Ptr> m_callback;
    };

    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_prepare(
        new Callback(m_callbackPool, std::move(callback)), toTarsTwoPCParams(params));
}

void ExecutorServiceClient::commit(
    const bcos::protocol::TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::Ptr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_commit(const bcostars::Error& ret) override { m_callback(toBcosError(ret)); }

        void callback_commit_exception(tars::Int32 ret) override { m_callback(toBcosError(ret)); }

    private:
        AsyncCallback<bcos::Error::Ptr> m_callback;
    };
    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_commit(
        new Callback(m_callbackPool, std::move(callback)), toTarsTwoPCParams(params));
}

void ExecutorServiceClient::rollback(
    const bcos::protocol::TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::Ptr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_rollback(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_rollback_exception(tars::Int32 ret) override { m_callback(toBcosError(ret)); }

    private:
        AsyncCallback<bcos::Error::Ptr> m_callback;
    };
    m_prx->tars_set_timeout(30000)->async_rollback(
        new Callback(m_callbackPool, std::move(callback)), toTarsTwoPCParams(params));
}

void ExecutorServiceClient::reset(std::function<void(bcos::Error::Ptr)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::Ptr)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_reset(const bcostars::Error& ret) override { m_callback(toBcosError(ret)); }

        void callback_reset_exception(tars::Int32 ret) override { m_callback(toBcosError(ret)); }

    private:
        AsyncCallback<bcos::Error::Ptr> m_callback;
    };
    m_prx->tars_set_timeout(30000)->async_reset(new Callback(m_callbackPool, std::move(callback)));
}

void ExecutorServiceClient::getCode(
    std::string_view contract, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::Ptr, bcos::bytes)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_getCode(
            const bcostars::Error& ret, std::vector<tars::Char> const& code) override
        {
            bcos::bytes codeBytes(code.begin(), code.end());
            m_callback(toBcosError(ret), std::move(codeBytes));
        }

        void callback_getCode_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), bcos::bytes());
        }

    private:
        AsyncCallback<bcos::Error::Ptr, bcos::bytes> m_callback;
    };
    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_getCode(
        new Callback(m_callbackPool, std::move(callback)), std::string(contract));
}

void ExecutorServiceClient::getABI(
    std::string_view contract, std::function<void(bcos::Error::Ptr, std::string)> callback)
{
    class Callback : public ExecutorServicePrxCallback
    {
    public:
        Callback(std::weak_ptr<bcos::ThreadPool> threadPool,
            std::function<void(bcos::Error::Ptr, std::string)>&& _callback)
          : m_callback(threadPool, std::move(_callback))
        {}
        ~Callback() override {}

        void callback_getABI(const bcostars::Error& ret, std::string const& abi) override
        {
            std::string tempAbi = abi;
            m_callback(toBcosError(ret), std::move(tempAbi));
        }

        void callback_getABI_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), std::string());
        }

    private:
        AsyncCallback<bcos::Error::Ptr, std::string> m_callback;
    };
    // timeout is 30s
    m_prx->tars_set_timeout(30000)->async_getABI(
        new Callback(m_callbackPool, std::move(callback)), std::string(contract));
}

void ExecutorServiceClient::updateEoaNonce(std::unordered_map<std::string, bcos::u256> const&)
{
    BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented"));
}
