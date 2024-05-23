#pragma once
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-ledger/src/libledger/LedgerMethods.h>
#include <bcos-rpc/filter/Filter.h>
#include <bcos-rpc/filter/LogMatcher.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/BucketMap.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Timer.h>
#include <boost/functional/hash.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::rpc::filter
{
struct KeyType
{
    std::string group;
    u256 id;

    KeyType(std::string_view g, u256 i) : group(g), id(i) {}

    friend bool operator==(const KeyType& l, const KeyType& r)
    {
        return l.group == r.group && l.id == r.id;
    }

    friend bool operator!=(const KeyType& l, const KeyType& r) { return !operator==(l, r); }

    template <typename Stream>
    friend Stream& operator<<(Stream& stream, const KeyType& key)
    {
        stream << key.group << "-" << key.id;
        return stream;
    }
};
}  // namespace bcos::rpc::filter

template <>
struct std::hash<bcos::rpc::filter::KeyType>
{
    size_t operator()(const bcos::rpc::filter::KeyType& key) const noexcept
    {
        size_t seed = std::hash<std::string>{}(key.group);
        boost::hash_combine(seed, std::hash<bcos::u256>{}(key.id));
        return seed;
    }
};

namespace bcos::rpc
{

class FilterSystem : public std::enable_shared_from_this<FilterSystem>
{
public:
    using Ptr = std::shared_ptr<FilterSystem>;
    using ConstPtr = std::shared_ptr<const FilterSystem>;

    FilterSystem(GroupManager::Ptr groupManager, const std::string& groupId,
        FilterRequestFactory::Ptr factory, int filterTimeout, int maxBlockProcessPerReq);
    virtual ~FilterSystem() { m_cleanUpTimer->stop(); }

public:
    // jsonrpc
    task::Task<std::string> newBlockFilter(std::string_view groupId);
    task::Task<std::string> newPendingTxFilter(std::string_view groupId);
    task::Task<std::string> newFilter(std::string_view groupId, FilterRequest::Ptr params);
    task::Task<bool> uninstallFilter(std::string_view groupId, u256 filterID)
    {
        co_return uninstallFilterImpl(groupId, filterID);
    }
    task::Task<Json::Value> getFilterChanges(std::string_view groupId, u256 filterID)
    {
        co_return co_await getFilterChangeImpl(groupId, filterID);
    }
    task::Task<Json::Value> getFilterLogs(std::string_view groupId, u256 filterID)
    {
        co_return co_await getFilterLogsImpl(groupId, filterID);
    }
    task::Task<Json::Value> getLogs(std::string_view groupId, FilterRequest::Ptr params)
    {
        co_return co_await getLogsImpl(groupId, params, true);
    }

    // web3jsonrpc
    task::Task<std::string> newBlockFilter() { co_return co_await newBlockFilter(m_group); }
    task::Task<std::string> newPendingTxFilter() { co_return co_await newPendingTxFilter(m_group); }
    task::Task<std::string> newFilter(FilterRequest::Ptr params)
    {
        co_return co_await newFilter(m_group, params);
    }
    task::Task<bool> uninstallFilter(u256 filterID)
    {
        co_return co_await uninstallFilter(m_group, filterID);
    }
    task::Task<Json::Value> getFilterChanges(u256 filterID)
    {
        co_return co_await getFilterChanges(m_group, filterID);
    }
    task::Task<Json::Value> getFilterLogs(u256 filterID)
    {
        co_return co_await getFilterLogs(m_group, filterID);
    }
    task::Task<Json::Value> getLogs(FilterRequest::Ptr params)
    {
        co_return co_await getLogs(m_group, params);
    }

public:
    int64_t getLatestBlockNumber(std::string_view groupId)
    {
        auto ledger = getNodeService(groupId, "getCurrentBlockNumber")->ledger();
        return getLatestBlockNumber(*ledger);
    }
    int64_t getLatestBlockNumber() { return getLatestBlockNumber(m_group); }

    FilterRequestFactory::Ptr requestFactory() const { return m_factory; }
    LogMatcher::Ptr matcher() const { return m_matcher; }
    NodeService::Ptr getNodeService(std::string_view _groupID, std::string_view _command) const;

protected:
    bool uninstallFilterImpl(std::string_view groupId, u256 filterID)
    {
        return m_filters.remove(filter::KeyType(groupId, filterID)) != nullptr;
    }
    task::Task<Json::Value> getFilterChangeImpl(std::string_view groupId, u256 filterID);
    task::Task<Json::Value> getBlockChangeImpl(std::string_view groupId, Filter::Ptr filter);
    task::Task<Json::Value> getPendingTxChangeImpl(std::string_view groupId, Filter::Ptr filter);
    task::Task<Json::Value> getLogChangeImpl(std::string_view groupId, Filter::Ptr filter);
    task::Task<Json::Value> getFilterLogsImpl(std::string_view groupId, u256 filterID);
    task::Task<Json::Value> getLogsImpl(
        std::string_view groupId, FilterRequest::Ptr params, bool needCheckRange);
    task::Task<Json::Value> getLogsInternal(
        bcos::ledger::LedgerInterface& ledger, FilterRequest::Ptr params);

protected:
    virtual int32_t InvalidParamsCode() = 0;
    uint64_t insertFilter(Filter::Ptr filter);
    void cleanUpExpiredFilters();

    Filter::Ptr getFilterByID(std::string_view groupId, u256 id)
    {
        FilterMap::ReadAccessor::Ptr accessor;
        if (!m_filters.find<FilterMap::ReadAccessor>(accessor, filter::KeyType(groupId, id)))
        {
            return nullptr;
        }
        return accessor->value();
    }
    static int64_t getLatestBlockNumber(bcos::ledger::LedgerInterface& _ledger)
    {
        int64_t latest = 0;
        task::wait([](bcos::ledger::LedgerInterface& ledger, int64_t& ret) -> task::Task<void> {
            ret = co_await ledger::getCurrentBlockNumber(ledger);
        }(_ledger, latest));
        return latest;
    }

protected:
    using FilterMap = BucketMap<filter::KeyType, Filter::Ptr, std::hash<filter::KeyType>>;

    uint64_t m_filterTimeout = FILTER_DEFAULT_EXPIRATION_TIME;
    int64_t m_maxBlockProcessPerReq = 10;
    GroupManager::Ptr m_groupManager;
    std::string m_group;
    LogMatcher::Ptr m_matcher;
    FilterRequestFactory::Ptr m_factory;
    FilterMap m_filters;
    // timer to clear up the expired filter in-period
    std::shared_ptr<Timer> m_cleanUpTimer;
};

}  // namespace bcos::rpc