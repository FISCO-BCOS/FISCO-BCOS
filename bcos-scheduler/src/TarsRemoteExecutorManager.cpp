//
// Created by Jimmy Shi on 2022/5/25.
//

#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "TarsRemoteExecutorManager.h"
#include <bcos-tars-protocol/client/ExecutorServiceClient.h>
#include <bcos-tars-protocol/tars/ExecutorService.h>
#include <tarscpp/servant/ObjectProxy.h>
#include <tarscpp/servant/QueryF.h>
#include <sstream>

using namespace bcos::scheduler;


TarsRemoteExecutorManager::EndPointSet buildEndPointSet(const vector<EndpointF>& endPointInfos)
{
    TarsRemoteExecutorManager::EndPointSet endPointSet =
        std::make_shared<std::set<std::pair<std::string, uint16_t>>>();

    if (endPointInfos.empty())
    {
        return endPointSet;
    }

    for (const EndpointF& endPointInfo : endPointInfos)
    {
        if (endPointInfo.host.empty())
        {
            continue;  // ignore error endpoint info
        }

        endPointSet->insert({endPointInfo.host, endPointInfo.port});
    }
    return endPointSet;
}

std::string dumpEndPointsLog(
    const vector<EndpointF>& activeEndPoints, const vector<EndpointF>& inactiveEndPoints)
{
    // dump logs
    std::stringstream ss;
    ss << "active:[";
    for (const EndpointF& endpointInfo : activeEndPoints)
    {
        ss << endpointInfo.host << ":" << endpointInfo.port << ", ";
    }
    ss << "], inactive:[";
    for (const EndpointF& endpointInfo : inactiveEndPoints)
    {
        ss << endpointInfo.host << ":" << endpointInfo.port << ", ";
    }
    ss << "]";
    return ss.str();
}

// return success, activeEndPoints, inactiveEndPoints
static std::tuple<bool, vector<EndpointF>, vector<EndpointF>> getActiveEndpoints(
    const std::string& executorServiceName)
{
    static string locator = tars::Application::getCommunicator()->getProperty("locator");

    if (locator.find_first_not_of('@') == string::npos)
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "Tars locator is not valid:" << LOG_KV("locator", locator);

        return {false, {}, {}};
    }

    static QueryFPrx locatorPrx =
        tars::Application::getCommunicator()->stringToProxy<QueryFPrx>(locator);

    vector<EndpointF> activeEndPoints;
    vector<EndpointF> inactiveEndPoints;
    int iRet = locatorPrx->findObjectById4Any(
        executorServiceName, activeEndPoints, inactiveEndPoints, ServerConfig::Context);

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

void TarsRemoteExecutorManager::refresh()
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
            EXECUTOR_MANAGER_LOG(DEBUG) << "Update current endpoint map: "
                                        << dumpEndPointsLog(activeEndPoints, inactiveEndPoints);
            update(currentEndPointMap);
        }
        else
        {
            EXECUTOR_MANAGER_LOG(DEBUG) << "Executor endpoint map: "
                                        << dumpEndPointsLog(activeEndPoints, inactiveEndPoints);
        }
    }
    catch (std::exception const& e)
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "Workloop exception: " << e.what();
    }
    catch (...)
    {
        EXECUTOR_MANAGER_LOG(ERROR) << "Workloop exception";
    }
}


void TarsRemoteExecutorManager::executeWorker()
{
    refresh();
}

void TarsRemoteExecutorManager::update(EndPointSet endPointSet)
{
    // update
    clear();
    m_endPointSet = endPointSet;
    for (auto hostAndPort : *m_endPointSet)
    {
        auto host = hostAndPort.first;
        auto port = hostAndPort.second;

        string endPointUrl = buildEndPointUrl(host, port);
        auto executorServicePrx =
            Application::getCommunicator()->stringToProxy<bcostars::ExecutorServicePrx>(
                endPointUrl);
        auto executor = std::make_shared<bcostars::ExecutorServiceClient>(executorServicePrx);

        EXECUTOR_MANAGER_LOG(DEBUG) << "Build new executor connection: " << endPointUrl;
        auto executorName = "executor-" + host + "-" + std::to_string(port);
        addExecutor(executorName, executor);
    }

    // trigger handler to notify scheduler
    if (m_onRemoteExecutorChange != nullptr)
    {
        m_onRemoteExecutorChange();
    }
}
