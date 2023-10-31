#include "ExecutorManager.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-utilities/Error.h>
#include <tbb/parallel_sort.h>
#include <boost/concept_check.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <mutex>
#include <thread>
#include <tuple>

using namespace bcos;
using namespace bcos::scheduler;

bool ExecutorManager::addExecutor(std::string name,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor, int64_t seq)
{
    {
        UpgradableGuard l(m_mutex);
        if (m_name2Executors.count(name))
        {
            return false;
        }

        auto executorInfo = std::make_shared<ExecutorInfo>();
        executorInfo->name = std::move(name);
        executorInfo->executor = std::move(executor);
        executorInfo->seq = seq;

        UpgradeGuard ul(l);
        auto [it, exists] = m_name2Executors.emplace(executorInfo->name, executorInfo);
        boost::ignore_unused(it);

        if (!exists)
        {
            return false;
        }

        m_executorPriorityQueue.emplace(std::move(executorInfo));
    }

    if (seq >= 0 && m_executorChangeHandler)
    {
        m_executorChangeHandler();
    }

    return true;
}

bcos::executor::ParallelTransactionExecutorInterface::Ptr ExecutorManager::dispatchExecutor(
    const std::string_view& _contract)
{
    auto contract = toLowerAddress(_contract);
    UpgradableGuard l(m_mutex);
    if (m_name2Executors.empty())
    {
        return nullptr;
    }
    auto executorIt = m_contract2ExecutorInfo.find(contract);
    if (executorIt != m_contract2ExecutorInfo.end())
    {
        return executorIt->second->executor;
    }
    UpgradeGuard ul(l);
    auto executorInfo = m_executorPriorityQueue.top();
    m_executorPriorityQueue.pop();

    auto [contractStr, success] = executorInfo->contracts.insert(std::string(contract));
    if (!success)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Insert into contracts fail!"));
    }
    m_executorPriorityQueue.push(executorInfo);

    (void)m_contract2ExecutorInfo.emplace(*contractStr, executorInfo);

    return executorInfo->executor;
}

bcos::executor::ParallelTransactionExecutorInterface::Ptr
ExecutorManager::dispatchCorrespondExecutor(const std::string_view& _contract)
{
    auto contract = toLowerAddress(_contract);
    UpgradableGuard l(m_mutex);
    auto executorIt = m_contract2ExecutorInfo.find(contract);
    if (executorIt != m_contract2ExecutorInfo.end())
    {
        return executorIt->second->executor;
    }
    else
    {
        return nullptr;
    }
}


bcos::scheduler::ExecutorManager::ExecutorInfo::Ptr ExecutorManager::getExecutorInfo(
    const std::string_view& _contract)
{
    auto contract = toLowerAddress(_contract);
    ReadGuard l(m_mutex);
    auto it = m_contract2ExecutorInfo.find(contract);
    if (it == m_contract2ExecutorInfo.end())
    {
        return nullptr;
    }
    else
    {
        return it->second;
    }
}

bool ExecutorManager::removeExecutor(const std::string_view& name)
{
    bool notify = false;
    WriteGuard lock(m_mutex);
    auto it = m_name2Executors.find(name);
    if (it != m_name2Executors.end())
    {
        auto& executorInfo = it->second;

        for (auto contractIt = executorInfo->contracts.begin();
             contractIt != executorInfo->contracts.end(); ++contractIt)
        {
            auto count = m_contract2ExecutorInfo.unsafe_erase(*contractIt);
            if (count < 1)
            {
                BOOST_THROW_EXCEPTION(bcos::Exception("Can't find contract in container"));
            }
        }

        m_name2Executors.erase(it);
        notify = true;

        m_executorPriorityQueue = std::priority_queue<ExecutorInfo::Ptr,
            std::vector<ExecutorInfo::Ptr>, ExecutorInfoComp>();

        for (auto& it : m_name2Executors)
        {
            m_executorPriorityQueue.push(it.second);
        }
    }
    else
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Not found executor: " + std::string(name)));
    }

    if (notify && m_executorChangeHandler)
    {
        m_executorChangeHandler();
    }

    return notify;
}


std::pair<bool, bcos::protocol::ExecutorStatus::UniquePtr> ExecutorManager::getExecutorStatus(
    const std::string& _executorName,
    const bcos::executor::ParallelTransactionExecutorInterface::Ptr& _executor)
{
    // fetch status
    EXECUTOR_MANAGER_LOG(TRACE) << "Query executor status" << LOG_KV("executorName", _executorName);
    std::promise<bcos::protocol::ExecutorStatus::UniquePtr> statusPromise;
    _executor->status([&_executorName, &statusPromise](bcos::Error::UniquePtr error,
                          bcos::protocol::ExecutorStatus::UniquePtr status) {
        if (error)
        {
            EXECUTOR_MANAGER_LOG(ERROR)
                << LOG_BADGE("getExecutorStatus") << "Could not get executor status"
                << LOG_KV("executorName", _executorName) << LOG_KV("code", error->errorCode())
                << LOG_KV("message", error->errorMessage());
            statusPromise.set_value(nullptr);
        }
        else
        {
            statusPromise.set_value(std::move(status));
        }
    });

    bcos::protocol::ExecutorStatus::UniquePtr status = statusPromise.get_future().get();
    if (status == nullptr)
    {
        return {false, nullptr};
    }
    else
    {
        EXECUTOR_MANAGER_LOG(TRACE)
            << "Get executor status success" << LOG_KV("executorName", _executorName)
            << LOG_KV("statusSeq", status->seq());
        return {true, std::move(status)};
    }
}


void ExecutorManager::checkExecutorStatus()
{
    bool notify = false;

    {
        ReadGuard lock(m_mutex);
        for (auto& [executorNameView, executorInfo] : m_name2Executors)
        {
            const std::string& executorName = executorInfo->name;
            auto [success, status] = getExecutorStatus(executorName, executorInfo->executor);
            if (!success)
            {
                EXECUTOR_MANAGER_LOG(INFO)
                    << LOG_BADGE("checkExecutorStatus") << LOG_DESC("getExecutorStatus failed")
                    << LOG_KV("executorName", executorInfo->name);
                notify = true;
                break;
            }

            int64_t oldSeq = executorInfo->seq;
            int64_t newSeq = status->seq();
            // reset seq to newSeq
            executorInfo->seq = newSeq;
            if (newSeq != oldSeq)
            {
                EXECUTOR_MANAGER_LOG(INFO)
                    << LOG_BADGE("checkExecutorStatus") << LOG_DESC("executor seq has been changed")
                    << LOG_KV("executorName", executorInfo->name) << LOG_KV("oldSeq", oldSeq)
                    << LOG_KV("newSeq", newSeq);
                notify = true;
                break;
            }
        }
    }

    if (notify && m_executorChangeHandler)
    {
        m_executorChangeHandler();
    }

    EXECUTOR_MANAGER_LOG(TRACE) << LOG_BADGE("checkExecutorStatus") << LOG_KV("notify", notify);

    if (m_timer)
    {
        m_timer->restart();
    }
}