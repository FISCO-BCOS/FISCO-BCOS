#pragma once
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-framework/libutilities/Log.h>
#include <tarscpp/servant/Application.h>
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
}  // namespace bcostars