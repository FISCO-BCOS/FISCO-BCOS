#pragma once

#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/parallel_for.h>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/range/any_range.hpp>
#include <functional>
#include <iterator>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace bcos::scheduler
{
class ExecutorManager
{
public:
    using Ptr = std::shared_ptr<ExecutorManager>;

    void addExecutor(
        std::string name, bcos::executor::ParallelTransactionExecutorInterface::Ptr executor);

    bcos::executor::ParallelTransactionExecutorInterface::Ptr dispatchExecutor(
        const std::string_view& contract);


    void removeExecutor(const std::string_view& name);

    auto begin() const
    {
        return boost::make_transform_iterator(m_name2Executors.cbegin(),
            std::bind(&ExecutorManager::executorView, this, std::placeholders::_1));
    }

    auto end() const
    {
        return boost::make_transform_iterator(m_name2Executors.cend(),
            std::bind(&ExecutorManager::executorView, this, std::placeholders::_1));
    }

    void forEachExecutor(
        std::function<void(std::string, bcos::executor::ParallelTransactionExecutorInterface::Ptr)>
            handleExecutor)
    {
        ReadGuard lock(m_mutex);
        if (m_name2Executors.empty())
        {
            return;
        }

        for (auto it : m_name2Executors)
        {
            handleExecutor(std::string(it.first), it.second->executor);
        }
    }

    size_t size() const
    {
        ReadGuard lock(m_mutex);
        return m_name2Executors.size();
    }

    void clear()
    {
        WriteGuard lock(m_mutex);
        m_contract2ExecutorInfo.clear();
        m_name2Executors.clear();
        m_executorPriorityQueue = std::priority_queue<ExecutorInfo::Ptr,
            std::vector<ExecutorInfo::Ptr>, ExecutorInfoComp>();
    };

    virtual void stop()
    {
        WriteGuard lock(m_mutex);
        for (auto it : m_name2Executors)
        {
            it.second->executor->stop();
        }
    }

    struct ExecutorInfo
    {
        using Ptr = std::shared_ptr<ExecutorInfo>;

        std::string name;
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor;
        std::set<std::string> contracts;
    };

    ExecutorInfo::Ptr getExecutorInfo(const std::string_view& contract);
    ExecutorInfo::Ptr getExecutorInfoByName(const std::string_view& name)
    {
        return m_name2Executors[name];
    };

private:
    struct ExecutorInfoComp
    {
        bool operator()(const ExecutorInfo::Ptr& lhs, const ExecutorInfo::Ptr& rhs) const
        {
            return lhs->contracts.size() > rhs->contracts.size();
        }
    };

    tbb::concurrent_unordered_map<std::string_view, ExecutorInfo::Ptr, std::hash<std::string_view>>
        m_contract2ExecutorInfo;
    std::unordered_map<std::string_view, ExecutorInfo::Ptr, std::hash<std::string_view>>
        m_name2Executors;
    std::priority_queue<ExecutorInfo::Ptr, std::vector<ExecutorInfo::Ptr>, ExecutorInfoComp>
        m_executorPriorityQueue;
    mutable SharedMutex m_mutex;

    bcos::executor::ParallelTransactionExecutorInterface::Ptr const& executorView(
        const decltype(m_name2Executors)::value_type& value) const
    {
        return value.second->executor;
    }
};
}  // namespace bcos::scheduler