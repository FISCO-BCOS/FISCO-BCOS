#include "ExecutorManager.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-utilities/Error.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::scheduler;

void ExecutorManager::forEachExecutor(
    std::function<void(std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr)>
        handleExecutor)
{
    ReadGuard lock(m_mutex);
    if (m_name2Executors.empty())
    {
        return;
    }

    for (auto const& it : m_name2Executors)
    {
        handleExecutor(std::string(it.first), it.second->executor);
    }
}

size_t ExecutorManager::size() const
{
    ReadGuard lock(m_mutex);
    return m_name2Executors.size();
}

void ExecutorManager::clear()
{
    bool notify = false;
    {
        WriteGuard lock(m_mutex);
        if (!m_name2Executors.empty())
        {
            notify = true;
            m_contract2ExecutorInfo.clear();
            m_name2Executors.clear();
            m_executorPriorityQueue = std::priority_queue<ExecutorInfo::Ptr,
                std::vector<ExecutorInfo::Ptr>, ExecutorInfoComp>();
        }
    }

    if (notify && m_executorChangeHandler)
    {
        m_executorChangeHandler();
    }
}

void ExecutorManager::stop()
{
    EXECUTOR_MANAGER_LOG(INFO) << "Try to stop ExecutorManager";

    std::vector<bcos::executor::ParallelTransactionExecutorInterface::Ptr> executors;
    {
        if (m_name2Executors.empty())
        {
            return;
        }

        WriteGuard lock(m_mutex);
        for (auto const& it : m_name2Executors)
        {
            executors.push_back(it.second->executor);
        }
    }

    for (auto& executor : executors)
    {
        executor->stop();
    }
}

void ExecutorManager::setExecutorChangeHandler(std::function<void()> _handler)
{
    m_executorChangeHandler = std::move(_handler);
}

std::function<void()> ExecutorManager::executorChangeHandler()
{
    return m_executorChangeHandler;
}

ExecutorManager::ExecutorInfo::Ptr ExecutorManager::getExecutorInfoByName(
    const std::string_view& name)
{
    return m_name2Executors[name];
}

std::string ExecutorManager::toLowerAddress(const std::string_view& address)
{
    return boost::algorithm::hex_lower(std::string(address));
}

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
                BOOST_THROW_EXCEPTION(bcos::Exception{} << errinfo_comment("Can't find contract in container"));
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
