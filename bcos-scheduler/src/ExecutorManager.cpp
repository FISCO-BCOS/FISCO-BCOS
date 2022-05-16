#include "ExecutorManager.h"
#include <bcos-utilities/Error.h>
#include <tbb/parallel_sort.h>
#include <boost/concept_check.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <mutex>
#include <thread>
#include <tuple>

using namespace bcos::scheduler;

void ExecutorManager::addExecutor(
    std::string name, bcos::executor::ParallelTransactionExecutorInterface::Ptr executor)
{
    {
        std::shared_lock l(m_mutex);
        if (m_name2Executors.count(name))
        {
            return;
        }
    }
    auto executorInfo = std::make_shared<ExecutorInfo>();
    executorInfo->name = std::move(name);
    executorInfo->executor = std::move(executor);

    std::unique_lock lock(m_mutex);
    auto [it, exists] = m_name2Executors.emplace(executorInfo->name, executorInfo);
    boost::ignore_unused(it);

    if (!exists)
    {
        return;
    }

    m_executorPriorityQueue.emplace(std::move(executorInfo));
}

bcos::executor::ParallelTransactionExecutorInterface::Ptr ExecutorManager::dispatchExecutor(
    const std::string_view& contract)
{
    if (m_name2Executors.empty())
    {
        return nullptr;
    }

    while (true)
    {
        auto executorIt = m_contract2ExecutorInfo.find(contract);
        if (executorIt != m_contract2ExecutorInfo.end())
        {
            return executorIt->second->executor;
        }
        else
        {
            std::unique_lock lock(m_mutex, std::try_to_lock);
            if (!lock.owns_lock())
            {
                continue;
            }

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
    }
}

void ExecutorManager::removeExecutor(const std::string_view& name)
{
    std::unique_lock lock(m_mutex);

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
}