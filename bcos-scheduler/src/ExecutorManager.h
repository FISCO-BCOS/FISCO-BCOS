#pragma once

#include "bcos-utilities/Timer.h"
#include <bcos-framework/executor/ParallelTransactionExecutorInterface.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>

namespace bcos::scheduler
{

#define EXECUTOR_MANAGER_CHECK_PERIOD (5000)

#define EXECUTOR_MANAGER_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR_MANAGER") << LOG_BADGE("Switch")

class ExecutorManager
{
public:
    using Ptr = std::shared_ptr<ExecutorManager>;

    ExecutorManager();

    virtual ~ExecutorManager();

    /**
     * Only used in max version
     */
    void startTimer();

    void stopTimer();

    bool addExecutor(std::string name,
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor, int64_t seq = -1);

    bcos::executor::ParallelTransactionExecutorInterface::Ptr dispatchExecutor(
        const std::string_view& contract);

    // return nullptr if there is no executor dispatched in this contract before
    bcos::executor::ParallelTransactionExecutorInterface::Ptr dispatchCorrespondExecutor(
        const std::string_view& contract);

    bool removeExecutor(const std::string_view& name);

    void checkExecutorStatus();

    std::pair<bool, bcos::protocol::ExecutorStatus::UniquePtr> getExecutorStatus(
        const std::string& _executorName,
        const bcos::executor::ParallelTransactionExecutorInterface::Ptr& _executor);

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
            handleExecutor);

    size_t size() const;

    void clear();

    virtual void stop();

    virtual void setExecutorChangeHandler(std::function<void()> _handler);

    std::function<void()> executorChangeHandler();

    struct ExecutorInfo
    {
        using Ptr = std::shared_ptr<ExecutorInfo>;

        std::string name;
        bcos::executor::ParallelTransactionExecutorInterface::Ptr executor;
        std::set<std::string> contracts;
        int64_t seq{0};
    };

    ExecutorInfo::Ptr getExecutorInfo(const std::string_view& contract);
    ExecutorInfo::Ptr getExecutorInfoByName(const std::string_view& name);

private:
    std::shared_ptr<Timer> m_timer;

    mutable SharedMutex m_mutex;
    std::unordered_map<std::string_view, ExecutorInfo::Ptr, std::hash<std::string_view>>
        m_name2Executors;

    struct ExecutorInfoComp
    {
        bool operator()(const ExecutorInfo::Ptr& lhs, const ExecutorInfo::Ptr& rhs) const
        {
            return lhs->contracts.size() > rhs->contracts.size();
        }
    };

    std::string toLowerAddress(const std::string_view& address);

    std::function<void()> m_executorChangeHandler;

    tbb::concurrent_unordered_map<std::string_view, ExecutorInfo::Ptr, std::hash<std::string_view>>
        m_contract2ExecutorInfo;

    std::priority_queue<ExecutorInfo::Ptr, std::vector<ExecutorInfo::Ptr>, ExecutorInfoComp>
        m_executorPriorityQueue;


    bcos::executor::ParallelTransactionExecutorInterface::Ptr const& executorView(
        const decltype(m_name2Executors)::value_type& value) const
    {
        return value.second->executor;
    }
};
}  // namespace bcos::scheduler
