//
// Created by Jimmy Shi on 2022/5/25.
//

#include "TarsRemoteExecutorManager.h"
#include <bcos-tars-protocol/client/ExecutorServiceClient.h>
#include <bcos-tars-protocol/tars/ExecutorService.h>
#include <servant/ObjectProxy.h>
#include <servant/QueryF.h>
#include <sstream>

using namespace bcos::scheduler;
using namespace tars;


TarsRemoteExecutorManager::EndPointSet buildEndPointSet(
    const std::vector<tars::EndpointF>& endPointInfos)
{
    TarsRemoteExecutorManager::EndPointSet endPointSet =
        std::make_shared<std::set<std::pair<std::string, uint16_t>>>();

    if (endPointInfos.empty())
    {
        return endPointSet;
    }

    for (const tars::EndpointF& endPointInfo : endPointInfos)
    {
        if (endPointInfo.host.empty())
        {
            continue;  // ignore error endpoint info
        }

        endPointSet->insert({endPointInfo.host, endPointInfo.port});
    }
    return endPointSet;
}

std::string dumpEndPointsLog(const std::vector<tars::EndpointF>& activeEndPoints,
    const std::vector<tars::EndpointF>& inactiveEndPoints)
{
    // dump logs
    std::stringstream ss;
    ss << "active:[";
    for (const tars::EndpointF& endpointInfo : activeEndPoints)
    {
        ss << endpointInfo.host << ":" << endpointInfo.port << ", ";
    }
    ss << "], inactive:[";
    for (const tars::EndpointF& endpointInfo : inactiveEndPoints)
    {
        ss << endpointInfo.host << ":" << endpointInfo.port << ", ";
    }
    ss << "]";
    return ss.str();
}

std::string dumpExecutor2Seq(std::map<std::string, int64_t> const& executor2Seq)
{
    // dump logs
    std::stringstream ss;
    ss << "seq:[";
    if (!executor2Seq.empty())
    {
        for (auto& it : executor2Seq)
        {
            ss << it.first << ":" << it.second << ", ";
        }
    }
    ss << "]";
    return ss.str();
}


// return success, activeEndPoints, inactiveEndPoints
static std::tuple<bool, std::vector<tars::EndpointF>, std::vector<tars::EndpointF>>
getActiveEndpoints(const std::string& executorServiceName)
{
    static std::string locator = tars::Application::getCommunicator()->getProperty("locator");

    if (locator.find_first_not_of('@') == std::string::npos)
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "Tars locator is not valid:" << LOG_KV("locator", locator);

        return {false, {}, {}};
    }

    static tars::QueryFPrx locatorPrx =
        tars::Application::getCommunicator()->stringToProxy<tars::QueryFPrx>(locator);

    std::vector<tars::EndpointF> activeEndPoints;
    std::vector<tars::EndpointF> inactiveEndPoints;
    int iRet = locatorPrx->findObjectById4Any(
        executorServiceName, activeEndPoints, inactiveEndPoints, tars::ServerConfig::Context);

    if (iRet == 0)
    {
        return {true, std::move(activeEndPoints), std::move(inactiveEndPoints)};
    }
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "Tars getActiveEndpoints error."
                                    << LOG_KV("executorServiceName", executorServiceName);
        return {false, {}, {}};
    }
}

std::pair<bool, bcos::protocol::ExecutorStatus::UniquePtr> getExecutorStatus(
    const std::string& name, bcos::executor::ParallelTransactionExecutorInterface::Ptr executor)
{
    // fetch status
    EXECUTOR_MANAGER_LOG(TRACE) << "Query executor status" << LOG_KV("name", name);
    std::promise<bcos::protocol::ExecutorStatus::UniquePtr> statusPromise;
    executor->status([&name, &statusPromise](bcos::Error::UniquePtr error,
                         bcos::protocol::ExecutorStatus::UniquePtr status) {
        if (error)
        {
            EXECUTOR_MANAGER_LOG(ERROR) << "Could not get executor status" << LOG_KV("name", name)
                                        << LOG_KV("errorCode", error->errorCode())
                                        << LOG_KV("errorMessage", error->errorMessage());
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
        EXECUTOR_MANAGER_LOG(TRACE) << "Get executor status success" << LOG_KV("name", name)
                                    << LOG_KV("statusSeq", status->seq());
        return {true, std::move(status)};
    }
}

void TarsRemoteExecutorManager::refresh(bool needNotifyChange)
{
    try
    {
        auto [success, activeEndPoints, inactiveEndPoints] =
            getActiveEndpoints(m_executorServiceName);

        if (!success)
        {
            EXECUTOR_MANAGER_LOG(DEBUG) << "getActiveEndpoints failed"
                                        << LOG_KV("executorServiceName", m_executorServiceName);
            return;
        }

        EndPointSet currentEndPointMap = buildEndPointSet(activeEndPoints);

        if (*m_endPointSet != *currentEndPointMap)
        {
            EXECUTOR_MANAGER_LOG(DEBUG) << "Update current endpoint map(for map not the same): "
                                        << dumpEndPointsLog(activeEndPoints, inactiveEndPoints);
            update(currentEndPointMap, needNotifyChange);
        }
        else if (!checkAllExecutorSeq())
        {
            EXECUTOR_MANAGER_LOG(DEBUG)
                << "Update current endpoint map(for executor seq not the same): "
                << dumpEndPointsLog(activeEndPoints, inactiveEndPoints);
            update(currentEndPointMap, needNotifyChange);
        }


        EXECUTOR_MANAGER_LOG(DEBUG)
            << "Executor endpoint map: " << dumpEndPointsLog(activeEndPoints, inactiveEndPoints)
            << ", " << dumpExecutor2Seq(m_executor2Seq);
    }
    catch (std::exception const& e)
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "Workloop exception: " << e.what();
    }
    catch (...)
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "WorkLoop exception";
    }
}

void TarsRemoteExecutorManager::waitForExecutorConnection()
{
    int retryTimes = 1;
    do
    {
        refresh(false);

        if (size() > 0)
        {
            break;
        }
        std::string message =
            "Waiting for connecting some executors, try times: " + std::to_string(retryTimes) +
            ", max retry times: " + std::to_string(m_waitingExecutorMaxRetryTimes);

        std::cout << message << std::endl;
        EXECUTOR_MANAGER_LOG(INFO) << message;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // wait for 1s

    } while (size() == 0 && ++retryTimes <= m_waitingExecutorMaxRetryTimes);


    if (retryTimes > m_waitingExecutorMaxRetryTimes)
    {
        // throw error
        throw std::runtime_error("Error: Could not connect any executor after " +
                                 std::to_string(m_waitingExecutorMaxRetryTimes) + " times retry");
    }
}

void TarsRemoteExecutorManager::executeWorker()
{
    refresh();
}

void TarsRemoteExecutorManager::update(EndPointSet endPointSet, bool needNotifyChange)
{
    // update
    clear();
    m_executor2Seq.clear();
    m_endPointSet = endPointSet;
    for (auto hostAndPort : *m_endPointSet)
    {
        auto host = hostAndPort.first;
        auto port = hostAndPort.second;

        string endPointUrl = buildEndPointUrl(host, port);
        auto executorServicePrx =
            tars::Application::getCommunicator()->stringToProxy<bcostars::ExecutorServicePrx>(
                endPointUrl);
        auto executor = std::make_shared<bcostars::ExecutorServiceClient>(executorServicePrx);
        auto executorName = "executor-" + host + "-" + std::to_string(port);

        // get seq
        auto [success, status] = getExecutorStatus(executorName, executor);
        if (success)
        {
            EXECUTOR_MANAGER_LOG(DEBUG)
                << "Build new executor connection: " << endPointUrl << LOG_KV("seq", status->seq());
            m_executor2Seq[executorName] = status->seq();
            addExecutor(executorName, executor);
        }
        else
        {
            EXECUTOR_MANAGER_LOG(ERROR)
                << "Could not make executor connection, ignore: " << endPointUrl;
        }
    }

    // trigger handler to notify scheduler
    if (needNotifyChange && m_onRemoteExecutorChange != nullptr)
    {
        m_onRemoteExecutorChange();
    }
}

bool TarsRemoteExecutorManager::checkAllExecutorSeq()
{
    if (m_executor2Seq.empty())
    {
        return true;  // is ok, no need to update
    }

    bool isSame = true;

    for (auto& it : m_executor2Seq)
    {
        auto name = it.first;
        auto seq = it.second;
        auto executorInfo = getExecutorInfoByName(name);

        if (!executorInfo)
        {
            EXECUTOR_MANAGER_LOG(ERROR)
                << "Could not get executor info in checkAllExecutorSeq()" << LOG_KV("name", name);
            continue;
        }

        auto [success, status] = getExecutorStatus(name, executorInfo->executor);
        if (!success)
        {
            EXECUTOR_MANAGER_LOG(ERROR)
                << "Could not query executor status" << LOG_KV("name", name);
            continue;
        }

        if (status->seq() != seq)
        {
            EXECUTOR_MANAGER_LOG(DEBUG)
                << "Executor seq not the same, need to switch" << LOG_KV("name", name)
                << LOG_KV("oldSeq", seq) << LOG_KV("queriedSeq", status->seq());

            isSame = false;
        }
    }

    return isSame;
}