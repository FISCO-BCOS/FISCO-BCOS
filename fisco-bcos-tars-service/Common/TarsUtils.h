#pragma once
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-tars-protocol/impl/TarsServantProxyCallback.h>
#include <servant/Application.h>
#include <servant/Communicator.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>

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

template <typename S, typename... Args>
S createServantPrx(std::string const& _serviceName, bool _withCb = true)
{
    auto prx = tars::Application::getCommunicator()->stringToProxy<S>(_serviceName);
    if (_withCb)
    {
        auto proxyCallback = new bcostars::TarsServantProxyCallback(_serviceName, *prx);
        prx->tars_set_push_callback(proxyCallback);
        proxyCallback->startTimer();
    }
    return prx;
}

template <typename S, typename... Args>
S createServantPrxWithCB(std::string const& _serviceName,
    TarsServantProxyOnConnectHandler _connectHandler = TarsServantProxyOnConnectHandler(),
    TarsServantProxyOnCloseHandler _closeHandler = TarsServantProxyOnCloseHandler(),
    bool _withCb = true)
{
    auto prx = tars::Application::getCommunicator()->stringToProxy<S>(_serviceName);
    if (_withCb)
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

template <typename S, typename... Args>
S createServantPrx(
    std::string const& _serviceName, const std::string& _host, uint16_t _port, bool _withCb = true)
{
    auto endPointStr = endPointToString(_serviceName, _host, _port);
    auto prx = tars::Application::getCommunicator()->stringToProxy<S>(endPointStr);
    if (_withCb)
    {
        auto proxyCallback = new bcostars::TarsServantProxyCallback(_serviceName, *prx);
        prx->tars_set_push_callback(proxyCallback);
        proxyCallback->startTimer();
    }
    return prx;
}

template <typename S, typename... Args>
S createServantPrx(tars::Communicator* communicator, std::string const& _serviceName,
    const tars::TC_Endpoint& _ep, bool _withCb = true)
{
    auto endPointStr = endPointToString(_serviceName, _ep);
    auto prx = communicator->stringToProxy<S>(endPointStr);
    if (_withCb)
    {
        auto proxyCallback = new bcostars::TarsServantProxyCallback(_serviceName, *prx);
        prx->tars_set_push_callback(proxyCallback);
        proxyCallback->startTimer();
    }
    return prx;
}

template <typename S, typename... Args>
S createServantPrx(
    std::string const& _serviceName, const tars::TC_Endpoint& _ep, bool _withCB = true)
{
    return createServantPrx<S, Args...>(
        tars::Application::getCommunicator().get(), _serviceName, _ep, _withCB);
}

template <typename S, typename... Args>
S createServantPrx(std::string const& _serviceName, const std::vector<tars::TC_Endpoint>& _eps,
    bool _withCb = true)
{
    std::string endPointStr = endPointToString(_serviceName, _eps);
    auto prx = tars::Application::getCommunicator()->stringToProxy<S>(endPointStr);
    if (_withCb)
    {
        auto proxyCallback = new bcostars::TarsServantProxyCallback(_serviceName, *prx);
        prx->tars_set_push_callback(proxyCallback);
        proxyCallback->startTimer();
    }
    return prx;
}


}  // namespace bcostars