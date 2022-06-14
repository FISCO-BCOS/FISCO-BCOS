//
// Created by Jimmy Shi on 2022/5/25.
//

#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "TarsRemoteExecutorManager.h"
#include <bcos-tars-protocol/client/ExecutorServiceClient.h>
#include <bcos-tars-protocol/tars/ExecutorService.h>
#include <tarscpp/servant/ObjectProxy.h>
#include <sstream>

using namespace bcos::scheduler;


TarsRemoteExecutorManager::EndPointSet buildEndPointSet(const vector<EndpointInfo>& endPointInfos)
{
    TarsRemoteExecutorManager::EndPointSet endPointSet =
        std::make_shared<std::set<std::pair<std::string, uint16_t>>>();

    if (endPointInfos.empty())
    {
        return endPointSet;
    }

    for (const EndpointInfo& endPointInfo : endPointInfos)
    {
        if (endPointInfo.host().empty())
        {
            continue;  // ignore error endpoint info
        }

        endPointSet->insert({endPointInfo.host(), endPointInfo.port()});
    }
    return endPointSet;
}

void dumpEndPointsLog(
    const vector<EndpointInfo>& activeEndPoints, const vector<EndpointInfo>& inactiveEndPoints)
{
    // dump logs
    std::stringstream ss;
    ss << "Detect executor endpoints. active:[";
    for (const EndpointInfo& endpointInfo : activeEndPoints)
    {
        ss << endpointInfo.host() << ":" << endpointInfo.port() << ", ";
    }
    ss << "], inactive:[";
    for (const EndpointInfo& endpointInfo : inactiveEndPoints)
    {
        ss << endpointInfo.host() << ":" << endpointInfo.port() << ", ";
    }
    ss << "]";
    EXECUTOR_MANAGER_LOG(DEBUG) << ss.str();
}

void TarsRemoteExecutorManager::executeWorker()
{
    auto proxy = tars::Application::getCommunicator()->stringToProxy<bcostars::ExecutorServicePrx>(
        m_executorServiceName);

    vector<EndpointInfo> activeEndPoints;
    vector<EndpointInfo> inactiveEndPoints;
    proxy->tars_endpoints(activeEndPoints, inactiveEndPoints);

    EndPointSet currentEndPointMap = buildEndPointSet(activeEndPoints);

    if (*m_endPointSet != *currentEndPointMap)
    {
        dumpEndPointsLog(activeEndPoints, inactiveEndPoints);
        update(currentEndPointMap);
    }
    else
    {
        EXECUTOR_MANAGER_LOG(TRACE) << "No need to update";
    }
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
