#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-ledger/src/libledger/LedgerMethods.h>
#include <bcos-rpc/filter/FilterSystem.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>

#define CPU_CORES std::thread::hardware_concurrency()

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::rpc::filter;

FilterSystem::FilterSystem(GroupManager::Ptr groupManager, const std::string& groupId,
    FilterRequestFactory::Ptr factory, int filterTimeout, int maxBlockProcessPerReq)
  : m_filterTimeout(filterTimeout * 1000),
    m_maxBlockProcessPerReq(maxBlockProcessPerReq),
    m_groupManager(groupManager),
    m_group(groupId),
    m_matcher(std::make_shared<LogMatcher>()),
    m_factory(factory),
    m_filters(CPU_CORES)
{
    // Trigger a filter cleanup operation every 3s
    m_cleanUpTimer = std::make_shared<Timer>(CLEANUP_FILTER_TIME, "filter_system_timer");
    m_cleanUpTimer->registerTimeoutHandler([this] { cleanUpExpiredFilters(); });
    m_cleanUpTimer->start();
}

uint64_t FilterSystem::insertFilter(Filter::Ptr filter)
{
    static std::mt19937_64 generator(std::random_device{}());
    uint64_t id = 0;
    while (true)
    {
        id = generator();
        FilterMap::WriteAccessor::Ptr accessor;
        if (m_filters.insert(accessor, {KeyType(filter->group(), id), filter}))
        {
            break;
        }
    }
    filter->setId(id);
    filter->updateLastAccessTime();
    return id;
}

void FilterSystem::cleanUpExpiredFilters()
{
    m_cleanUpTimer->restart();
    if (m_filters.empty())
    {
        return;
    }
    size_t traversedFiltersNum = 0;
    uint64_t currentTime = utcTime();
    std::vector<KeyType> expiredFilters;
    m_filters.forEach<FilterMap::ReadAccessor>(
        [&traversedFiltersNum, &expiredFilters, this, &currentTime](
            FilterMap::ReadAccessor::Ptr accessor) {
            const auto& filter = accessor->value();
            if (currentTime > (filter->lastAccessTime() + m_filterTimeout))
            {
                expiredFilters.emplace_back(KeyType(filter->group(), filter->id()));
            }
            if (++traversedFiltersNum > MAX_TRAVERSE_FILTERS_COUNT)
            {
                return false;
            }
            return true;
        });
    m_filters.batchRemove(expiredFilters);
    FILTER_LOG(INFO) << LOG_DESC("cleanUpExpiredFilters") << LOG_KV("filters", m_filters.size())
                     << LOG_KV("erasedFilters", expiredFilters.size())
                     << LOG_KV("traversedFiltersNum", traversedFiltersNum);
}

NodeService::Ptr FilterSystem::getNodeService(
    std::string_view _groupID, std::string_view _command) const
{
    auto nodeService = m_groupManager->getNodeService(_groupID, "");
    if (!nodeService)
    {
        std::stringstream errorMsg;
        errorMsg << LOG_DESC("group not exist") << LOG_KV("chain", m_groupManager->chainID())
                 << LOG_KV("group", _groupID) << LOG_KV("commond", _command);
        FILTER_LOG(WARNING) << errorMsg.str();
        BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::GroupNotExist, errorMsg.str()));
    }
    return nodeService;
}

task::Task<std::string> FilterSystem::newBlockFilter(std::string_view groupId)
{
    auto latestBlockNumber = getLatestBlockNumber(groupId);
    auto filter = std::make_shared<Filter>(
        BlocksSubscription, 0, nullptr, false, latestBlockNumber + 1, groupId);
    auto id = insertFilter(filter);
    FILTER_LOG(TRACE) << LOG_BADGE("newBlockFilter") << LOG_KV("id", id)
                      << LOG_KV("startBlockNumber", latestBlockNumber);
    co_return toQuantity(id);
}

task::Task<std::string> FilterSystem::newPendingTxFilter(std::string_view groupId)
{
    auto latestBlockNumber = getLatestBlockNumber(groupId);
    auto filter = std::make_shared<Filter>(
        PendingTransactionsSubscription, 0, nullptr, false, latestBlockNumber + 1, groupId);
    auto id = insertFilter(filter);
    FILTER_LOG(TRACE) << LOG_BADGE("newPendingTxFilter") << LOG_KV("id", id)
                      << LOG_KV("startBlockNumber", latestBlockNumber);
    co_return toQuantity(id);
}

task::Task<std::string> FilterSystem::newFilter(std::string_view groupId, FilterRequest::Ptr params)
{
    if (!params->checkBlockRange())
    {
        FILTER_LOG(WARNING) << LOG_BADGE("newFilter") << LOG_DESC("invalid block range params")
                            << LOG_KV("fromBlock", params->fromBlock())
                            << LOG_KV("toBlock", params->toBlock());
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(), "invalid block range params"));
    }
    auto latestBlockNumber = getLatestBlockNumber(groupId);
    auto filter = std::make_shared<Filter>(
        LogsSubscription, 0, params, false, latestBlockNumber + 1, groupId);
    auto id = insertFilter(filter);
    FILTER_LOG(TRACE) << LOG_BADGE("newFilter") << LOG_KV("id", id)
                      << LOG_KV("startBlockNumber", latestBlockNumber);
    co_return toQuantity(id);
}

task::Task<Json::Value> FilterSystem::getFilterChangeImpl(std::string_view groupId, u256 filterID)
{
    auto filter = getFilterByID(groupId, filterID);
    if (filter == nullptr)
    {
        FILTER_LOG(WARNING) << LOG_BADGE("getFilterChangeImpl") << LOG_DESC("filter not found")
                            << LOG_KV("id", filterID);
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(), "filter not found"));
    }
    filter->updateLastAccessTime();
    FILTER_LOG(TRACE) << LOG_BADGE("getFilterChangeImpl") << LOG_KV("id", filterID)
                      << LOG_KV("subType", filter->type());

    Json::Value jValue(Json::arrayValue);
    switch (filter->type())
    {
    case PendingTransactionsSubscription:
    {
        jValue = co_await getPendingTxChangeImpl(groupId, filter);
        break;
    }
    case LogsSubscription:
    {
        jValue = co_await getLogChangeImpl(groupId, filter);
        break;
    }
    case BlocksSubscription:
    {
        jValue = co_await getBlockChangeImpl(groupId, filter);
        break;
    }
    default:
        break;
    }
    co_return jValue;
}

task::Task<Json::Value> FilterSystem::getBlockChangeImpl(
    std::string_view groupId, Filter::Ptr filter)
{
    // getLatestBlockNumber and getBlockHash use the same ledger
    auto ledger = getNodeService(groupId, "getBlockChangeImpl")->ledger();
    auto latestBlockNumber = getLatestBlockNumber(*ledger);
    auto startBlockNumber = filter->startBlockNumber();

    if (latestBlockNumber < startBlockNumber)
    {  // Since the last query, no new blocks have been generated
        co_return Json::Value(Json::arrayValue);
    }
    // limit the number of blocks processed
    auto processBlockNum =
        std::min(latestBlockNumber - startBlockNumber + 1, m_maxBlockProcessPerReq);
    filter->setStartBlockNumber(startBlockNumber + processBlockNum);

    FILTER_LOG(DEBUG) << LOG_BADGE("getBlockChangeImpl") << LOG_KV("id", filter->id())
                      << LOG_KV("latestBlockNumber", latestBlockNumber)
                      << LOG_KV("startBlockNumber", startBlockNumber)
                      << LOG_KV("processBlockNum", processBlockNum)
                      << LOG_KV("nextStartBlockNumber", startBlockNumber + processBlockNum);
    Json::Value jResult(Json::arrayValue);
    for (auto i = 0; i < processBlockNum; ++i)
    {
        auto hash = co_await ledger::getBlockHash(*ledger, startBlockNumber + i);
        jResult.append(hash.hexPrefixed());
    }
    co_return jResult;
}

task::Task<Json::Value> FilterSystem::getPendingTxChangeImpl(
    std::string_view groupId, Filter::Ptr filter)
{
    // getLatestBlockNumber and getBlockData use the same ledger
    auto ledger = getNodeService(groupId, "getPendingTxChangeImpl")->ledger();
    auto latestBlockNumber = getLatestBlockNumber(*ledger);
    auto startBlockNumber = filter->startBlockNumber();
    if (latestBlockNumber < startBlockNumber)
    {  // Since the last query, no new blocks have been generated
        co_return Json::Value(Json::arrayValue);
    }
    // limit the number of blocks processed
    auto processBlockNum =
        std::min(latestBlockNumber - startBlockNumber + 1, m_maxBlockProcessPerReq);
    filter->setStartBlockNumber(startBlockNumber + processBlockNum);

    FILTER_LOG(DEBUG) << LOG_BADGE("getPendingTxChangeImpl") << LOG_KV("id", filter->id())
                      << LOG_KV("latestBlockNumber", latestBlockNumber)
                      << LOG_KV("startBlockNumber", startBlockNumber)
                      << LOG_KV("processBlockNum", processBlockNum)
                      << LOG_KV("nextStartBlockNumber", startBlockNumber + processBlockNum);

    Json::Value jRes(Json::arrayValue);
    for (auto i = 0; i < processBlockNum; ++i)
    {
        auto block = co_await ledger::getBlockData(
            *ledger, i + startBlockNumber, bcos::ledger::TRANSACTIONS_HASH);
        for (std::size_t index = 0; index < block->transactionsMetaDataSize(); ++index)
        {
            jRes.append(block->transactionHash(index).hexPrefixed());
        }
    }
    co_return jRes;
}

task::Task<Json::Value> FilterSystem::getLogChangeImpl(std::string_view groupId, Filter::Ptr filter)
{
    // getLatestBlockNumber and getLogsInternal use the same ledger
    auto ledger = getNodeService(groupId, "getLogsImpl")->ledger();
    auto latestBlockNumber = getLatestBlockNumber(*ledger);
    auto startBlockNumber = filter->startBlockNumber();
    auto fromBlock = filter->params()->fromBlock();
    auto toBlock = filter->params()->toBlock();
    auto fromIsLatest = filter->params()->fromIsLatest();
    auto toIsLatest = filter->params()->toIsLatest();

    /* clang-format off */   
    //   -------------------------------------------------------------------------------------------
    //   The values of (fromBlock, toBlock) may be:
    //   1. (latest, latest)
    //   2. [from, latest)
    //   3. [from, to]
    //   -------------------------------------------------------------------------------------------
    //   "end < begin" is equivalent to the following conditions:
    //   1. (fromBlock, toBlock) => [from, latest)
    //     - from > latestBlockNumber (the block of interest has not been generated yet)
    //     - Since the last query, no new blocks have been generated
    //   2. (fromBlock, toBlock) => [from, to] 
    //     - from > latestBlockNumber (the block of interest has not been generated yet)
    //     - to < latestBlockNumber (the blocks of interest are those that have already been stored)
    //     - Since the last query, no new blocks have been generated
    //     - All blocks within the interval [from, to] have been processed
    //   3. (fromBlock, toBlock) => (latest, latest)
    //     - Since the last query, no new blocks have been generated
    //   -------------------------------------------------------------------------------------------
    /* clang-format on */
    auto begin = fromIsLatest ? startBlockNumber : std::max(fromBlock, startBlockNumber);
    auto end = toIsLatest ? latestBlockNumber : std::min(toBlock, latestBlockNumber);
    if (end < begin)
    {
        co_return Json::Value(Json::arrayValue);
    }

    auto params = m_factory->create(*(filter->params()));
    // limit the number of blocks processed
    auto processBlockNum = std::min(end - begin + 1, m_maxBlockProcessPerReq);
    params->setFromBlock(begin);
    params->setToBlock(begin + processBlockNum - 1);
    filter->setStartBlockNumber(begin + processBlockNum);
    FILTER_LOG(DEBUG) << LOG_BADGE("getLogChangeImpl") << LOG_KV("id", filter->id())
                      << LOG_KV("latestBlockNumber", latestBlockNumber)
                      << LOG_KV("startBlockNumber", startBlockNumber)
                      << LOG_KV("processBlockNum", processBlockNum)
                      << LOG_KV("nextStartBlockNumber", begin + processBlockNum)
                      << LOG_KV("begin", begin) << LOG_KV("end", end) << LOG_KV("from", begin)
                      << LOG_KV("to", begin + processBlockNum - 1);
    co_return co_await getLogsInternal(*ledger, std::move(params));
}

task::Task<Json::Value> FilterSystem::getFilterLogsImpl(std::string_view groupId, u256 filterID)
{
    auto filter = getFilterByID(groupId, filterID);
    if (filter == nullptr || filter->type() != LogsSubscription)
    {
        FILTER_LOG(ERROR) << LOG_BADGE("getFilterLogsImpl") << LOG_DESC("filter not found")
                          << LOG_KV("id", filterID);
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(), "filter not found"));
    }
    auto params = m_factory->create(*(filter->params()));
    co_return co_await getLogsImpl(groupId, params, false);
}

task::Task<Json::Value> FilterSystem::getLogsImpl(
    std::string_view groupId, FilterRequest::Ptr params, bool needCheckRange)
{
    // getLatestBlockNumber and getLogsInPool use the same ledger
    auto ledger = getNodeService(groupId, "getLogsImpl")->ledger();
    if (!params->blockHash().empty())
    {  // when blockHash is not empty, match logs within the specified block
        auto matcher = m_matcher;
        int64_t blockNumber = 0;
        try
        {
            blockNumber = co_await ledger::getBlockNumber(*ledger,
                bcos::crypto::HashType(params->blockHash(), bcos::crypto::HashType::FromHex));
        }
        catch (std::exception& e)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(), "unknown block"));
        }
        auto block = co_await ledger::getBlockData(*ledger, blockNumber,
            bcos::ledger::HEADER | bcos::ledger::RECEIPTS | bcos::ledger::TRANSACTIONS_HASH);
        Json::Value jArray(Json::arrayValue);
        matcher->matches(params, block, jArray);
        co_return jArray;
    }
    else
    {
        auto latestBlockNumber = getLatestBlockNumber(*ledger);
        auto fromBlock = params->fromBlock();
        auto toBlock = params->toBlock();
        if (needCheckRange && !params->checkBlockRange())
        {
            FILTER_LOG(WARNING) << LOG_BADGE("getLogsImpl")
                                << LOG_DESC("invalid block range params")
                                << LOG_KV("fromBlock", fromBlock) << LOG_KV("toBlock", toBlock);
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParamsCode(), "invalid block range params"));
        }
        fromBlock = params->fromIsLatest() ? latestBlockNumber : fromBlock;
        toBlock = params->toIsLatest() ? latestBlockNumber : toBlock;
        if (fromBlock > latestBlockNumber)
        {  // the block of interest has not been generated yet
            co_return Json::Value(Json::arrayValue);
        }
        params->setFromBlock(fromBlock);
        params->setToBlock(std::min(toBlock, latestBlockNumber));
        co_return co_await getLogsInternal(*ledger, std::move(params));
    }
}

task::Task<Json::Value> FilterSystem::getLogsInternal(
    bcos::ledger::LedgerInterface& ledger, FilterRequest::Ptr params)
{
    auto fromBlock = params->fromBlock();
    auto toBlock = params->toBlock();
    Json::Value jArray(Json::arrayValue);
    auto matcher = m_matcher;
    for (auto number = fromBlock; number <= toBlock; ++number)
    {
        auto block = co_await ledger::getBlockData(ledger, number,
            bcos::ledger::HEADER | bcos::ledger::RECEIPTS | bcos::ledger::TRANSACTIONS_HASH);
        matcher->matches(params, block, jArray);
    }
    co_return jArray;
}