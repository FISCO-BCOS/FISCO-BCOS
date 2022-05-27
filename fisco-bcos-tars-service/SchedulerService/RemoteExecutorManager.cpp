//
// Created by Jimmy Shi on 2022/5/25.
//

#include "RemoteExecutorManager.h"
#include <bcos-tars-protocol/client/ExecutorServiceClient.h>
#include <bcos-tars-protocol/tars/ExecutorService.h>
#include <tarscpp/servant/Application.h>
#include <tarscpp/servant/ObjectProxy.h>
#include <sstream>

using namespace bcos::scheduler;


RemoteExecutorManager::EndPointMap buildEndPointMap(const vector<EndpointInfo>& endPointInfos)
{
    RemoteExecutorManager::EndPointMap endPointMap =
        std::make_shared<std::map<std::string, uint16_t>>();

    if (endPointInfos.empty())
    {
        return endPointMap;
    }

    for (const EndpointInfo& endPointInfo : endPointInfos)
    {
        if (endPointInfo.host().empty())
        {
            continue;  // ignore error endpoint info
        }
        endPointMap->insert({endPointInfo.host(), endPointInfo.port()});
    }
    return endPointMap;
}

bool isSame(RemoteExecutorManager::EndPointMap a, RemoteExecutorManager::EndPointMap b)
{
    if (a->size() != b->size())
    {
        return false;
    }

    if (a->empty() && b->empty())
    {
        return true;
    }

    for (auto hostAndPort : *a)
    {
        if ((*b)[hostAndPort.first] != hostAndPort.second)
        {
            return false;
        }
    }
    return true;
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

void RemoteExecutorManager::executeWorker()
{
    auto proxy = tars::Application::getCommunicator()->stringToProxy<bcostars::ExecutorServicePrx>(
        m_executorServiceName);

    vector<EndpointInfo> activeEndPoints;
    vector<EndpointInfo> inactiveEndPoints;
    proxy->tars_endpoints(activeEndPoints, inactiveEndPoints);

    EndPointMap currentEndPointMap = buildEndPointMap(activeEndPoints);

    if (!isSame(m_endPointMap, currentEndPointMap))
    {
        dumpEndPointsLog(activeEndPoints, inactiveEndPoints);
        update(currentEndPointMap);
    }
    else
    {
        EXECUTOR_MANAGER_LOG(TRACE) << "No need to update";
    }
}

void RemoteExecutorManager::update(EndPointMap endPointMap)
{
    // update
    clear();
    m_endPointMap = endPointMap;
    for (auto hostAndPort : *m_endPointMap)
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
