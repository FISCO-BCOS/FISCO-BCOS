#pragma once
#include "bcos-tars-protocol/bcos-tars-protocol/impl/TarsServantProxyCallback.h"
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <servant/Application.h>
#include <servant/Communicator.h>
#include <util/tc_clientsocket.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/throw_exception.hpp>

#define RPCSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[RPCSERVICE][INITIALIZER]"
#define GATEWAYSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[GATEWAYSERVICE][INITIALIZER]"
#define TXPOOLSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[TXPOOLSERVICE]"
#define PBFTSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[PBFTSERVICE]"
#define FRONTSERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[FRONTSERVICE]"

namespace bcostars
{

inline tars::TC_Endpoint string2TarsEndPoint(const std::string& _strEndPoint)
{
    std::vector<std::string> temp;
    boost::split(temp, _strEndPoint, boost::is_any_of(":"), boost::token_compress_on);

    if (temp.size() != 2)
    {
        BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                  "incorrect string endpoint, it should be in IP:Port format"));
    }

    tars::TC_Endpoint ep(temp[0], boost::lexical_cast<int>(temp[1]), 60000);
    return ep;
}

inline std::string getProxyDesc(std::string const& _servantName)
{
    std::string desc =
        tars::ServerConfig::Application + "." + tars::ServerConfig::ServerName + "." + _servantName;
    return desc;
}
inline std::string getLogPath()
{
    return tars::ServerConfig::LogPath + "/" + tars::ServerConfig::Application + "/" +
           tars::ServerConfig::ServerName;
}

inline std::string endPointToString(
    std::string const& _serviceName, const std::string& _host, uint16_t _port)
{
    return _serviceName + "@tcp -h " + _host + " -p " + boost::lexical_cast<std::string>(_port);
}

inline std::string endPointToString(
    std::string const& _serviceName, tars::TC_Endpoint const& _endPoint)
{
    return endPointToString(_serviceName, _endPoint.getHost(), _endPoint.getPort());
}

inline std::string endPointToString(
    std::string const& _serviceName, const std::vector<tars::TC_Endpoint>& _eps)
{
    if (_eps.empty())
    {
        BOOST_THROW_EXCEPTION(
            bcos::InvalidParameter() << bcos::errinfo_comment(
                "vector<tars::TC_Endpoint> should not be empty in endPointToString"));
    }

    bool first = true;
    std::string endPointStr = _serviceName;
    for (const auto& _ep : _eps)
    {
        endPointStr += (first ? "@" : ":");
        endPointStr +=
            ("tcp -h " + _ep.getHost() + " -p " + boost::lexical_cast<std::string>(_ep.getPort()));

        first = false;
    }
    return endPointStr;
}

inline std::pair<bool, std::string> getEndPointDescByAdapter(
    tars::Application* _application, std::string const& _servantName)
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

template <typename S>
S createServantProxy(tars::Communicator* communicator, std::string const& _serviceName,
    TarsServantProxyOnConnectHandler _connectHandler = TarsServantProxyOnConnectHandler(),
    TarsServantProxyOnCloseHandler _closeHandler = TarsServantProxyOnCloseHandler())
{
    auto prx = communicator->stringToProxy<S>(_serviceName);
    if (!prx->tars_get_push_callback())
    {
        auto proxyCallback = new bcostars::TarsServantProxyCallback(_serviceName, *prx);

        if (_connectHandler)
        {
            proxyCallback->setOnConnectHandler(_connectHandler);
        }

        if (_closeHandler)
        {
            proxyCallback->setOnCloseHandler(_closeHandler);
        }

        prx->tars_set_push_callback(proxyCallback);
        proxyCallback->startTimer();
    }
    return prx;
}

template <typename S>
S createServantProxy(std::string const& _serviceName)
{
    return createServantProxy<S>(tars::Application::getCommunicator().get(), _serviceName,
        TarsServantProxyOnConnectHandler(), TarsServantProxyOnCloseHandler());
}

template <typename S>
S createServantProxy(std::string const& _serviceName, const std::string& _host, uint16_t _port)
{
    auto endPointStr = endPointToString(_serviceName, _host, _port);
    return createServantProxy<S>(endPointStr);
}

template <typename S>
S createServantProxy(std::string const& _serviceName, const tars::TC_Endpoint& _ep)
{
    auto endPointStr = endPointToString(_serviceName, _ep);
    return createServantProxy<S>(endPointStr);
}

template <typename S>
S createServantProxy(std::string const& _serviceName, const std::vector<tars::TC_Endpoint>& _eps)
{
    std::string endPointStr = endPointToString(_serviceName, _eps);

    return createServantProxy<S>(endPointStr);
}

template <typename S>
S createServantProxy(bool _withEndPoints, std::string const& _serviceName,
    const std::vector<tars::TC_Endpoint>& _eps = std::vector<tars::TC_Endpoint>{})
{
    std::string serviceParams;
    if (_withEndPoints)
    {
        serviceParams = endPointToString(_serviceName, _eps);
    }
    else
    {
        serviceParams = _serviceName;
    }

    return createServantProxy<S>(serviceParams);
}

}  // namespace bcostars