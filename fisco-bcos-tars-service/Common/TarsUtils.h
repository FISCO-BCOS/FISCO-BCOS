#pragma once
#include <bcos-framework//Common.h>
#include <bcos-framework//protocol/ServiceDesc.h>
#include <servant/Application.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <string>

#define RPCSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[RPCSERVICE][INITIALIZER]"
#define GATEWAYSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[GATEWAYSERVICE][INITIALIZER]"
#define TXPOOLSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[TXPOOLSERVICE]"
#define PBFTSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[PBFTSERVICE]"
#define FRONTSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[FRONTSERVICE]"

namespace bcostars
{
inline std::string getProxyDesc(std::string const& _servantName)
{
    std::string desc =
        ServerConfig::Application + "." + ServerConfig::ServerName + "." + _servantName;
    return desc;
}
inline std::string getLogPath()
{
    return ServerConfig::LogPath + "/" + ServerConfig::Application + "/" + ServerConfig::ServerName;
}

inline std::string endPointToString(std::string const& _serviceName, TC_Endpoint const& _endPoint)
{
    return _serviceName + "@tcp -h " + _endPoint.getHost() + " -p " +
           boost::lexical_cast<std::string>(_endPoint.getPort());
}

inline std::pair<bool, std::string> getEndPointDescByAdapter(
    Application* _application, std::string const& _servantName)
{
    auto adapters = _application->getEpollServer()->getBindAdapters();
    if (adapters.size() == 0)
    {
        return std::make_pair(false, "");
    }
    auto prxDesc = getProxyDesc(_servantName);
    auto adapterName = prxDesc + "Adapter";
    for (auto const& adapter : adapters)
    {
        if (adapter->getName() == adapterName)
        {
            return std::make_pair(true, endPointToString(prxDesc, adapter->getEndpoint()));
        }
    }
    return std::make_pair(false, "");
}
}  // namespace bcostars