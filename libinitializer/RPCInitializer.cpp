/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file RPCInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "RPCInitializer.h"
#include "libblockchain/BlockChainInterface.h"
#include "libchannelserver/ChannelRPCServer.h"
#include "libchannelserver/ChannelServer.h"
#include "libdevcore/Common.h"                    // for byte
#include "libeventfilter/EventLogFilterParams.h"  // for eventfilter
#include "libinitializer/Common.h"
#include "libledger/LedgerManager.h"
#include "librpc/Rpc.h"  // for Rpc
#include "librpc/SafeHttpServer.h"
#include <libstat/ChannelNetworkStatHandler.h>

using namespace dev;
using namespace dev::initializer;
using namespace dev::channel;

void RPCInitializer::initChannelRPCServer(boost::property_tree::ptree const& _pt)
{
    std::string listenIP = _pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
    // listen ip for channel service, load from rpc.listen_ip if rpc.channel_listen_ip is not setted
    listenIP = _pt.get<std::string>("rpc.channel_listen_ip", listenIP);

    int listenPort = _pt.get<int>("rpc.channel_listen_port", 20200);
    int httpListenPort = _pt.get<int>("rpc.jsonrpc_listen_port", 8545);
    bool checkCertIssuer = _pt.get<bool>("network_security.check_cert_issuer", true);
    INITIALIZER_LOG(INFO) << LOG_BADGE("RPCInitializer")
                          << LOG_KV("network_security.check_cert_issuer", checkCertIssuer);

    if (!isValidPort(listenPort) || !isValidPort(httpListenPort))
    {
        ERROR_OUTPUT << LOG_BADGE("RPCInitializer")
                     << LOG_DESC(
                            "initConfig for RPCInitializer failed! Invalid ListenPort for RPC!")
                     << std::endl;
        exit(1);
    }

    m_channelRPCServer.reset(new ChannelRPCServer(), [](ChannelRPCServer* p) { (void)p; });
    m_channelRPCServer->setListenAddr(listenIP);
    m_channelRPCServer->setListenPort(listenPort);
    m_channelRPCServer->setSSLContext(m_sslContext);
    m_channelRPCServer->setService(m_p2pService);
    if (g_BCOSConfig.enableStat())
    {
        // set networkStatHandler for channelRPCServer
        m_networkStatHandler = createNetWorkStatHandler(_pt);
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("RPCInitializer")
                               << LOG_DESC("Enable network statistic");
    }
    m_channelRPCServer->setNetworkStatHandler(m_networkStatHandler);
    // create network-bandwidth-limiter
    auto networkBandwidth = createNetworkBandwidthLimit(_pt);
    if (networkBandwidth)
    {
        m_channelRPCServer->setNetworkBandwidthLimiter(networkBandwidth);
    }
    auto ioService = std::make_shared<boost::asio::io_service>();

    auto server = std::make_shared<dev::channel::ChannelServer>();
    server->setIOService(ioService);
    server->setSSLContext(m_sslContext);
    server->setEnableSSL(true);
    server->setBind(listenIP, listenPort);
    server->setCheckCertIssuer(checkCertIssuer);
    server->setMessageFactory(std::make_shared<dev::channel::ChannelMessageFactory>());

    m_channelRPCServer->setChannelServer(server);

    // start channelServer before initialize ledger, because amdb-proxy depends on channel
    auto rpcEntity = new rpc::Rpc(nullptr, nullptr);

    auto modularServer = new ModularServer<rpc::Rpc>(rpcEntity);
    auto qpsLimiter = createQPSLimiter(_pt);
    modularServer->setNetworkStatHandler(m_networkStatHandler);
    modularServer->setQPSLimiter(qpsLimiter);

    m_channelRPCHttpServer = modularServer;

    m_rpcForChannel.reset(rpcEntity, [](rpc::Rpc*) {});
    m_channelRPCHttpServer->addConnector(m_channelRPCServer.get());
    m_channelRPCServer->setQPSLimiter(qpsLimiter);
    try
    {
        if (!m_channelRPCHttpServer->StartListening())
        {
            BOOST_THROW_EXCEPTION(ListenPortIsUsed());
        }
    }
    catch (std::exception& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("RPCInitializer")
                               << LOG_KV("check channel_listen_port", listenPort)
                               << LOG_KV("check listenIP", listenIP)
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        ERROR_OUTPUT << LOG_BADGE("RPCInitializer")
                     << LOG_KV("check channel_listen_port", listenPort)
                     << LOG_KV("check listenIP", listenIP) << std::endl;
        BOOST_THROW_EXCEPTION(
            ListenPortIsUsed() << errinfo_comment(
                "Please check channel_listenIP and channel_listen_port are valid"));
    }
    if (m_networkStatHandler)
    {
        m_networkStatHandler->start();
    }
    INITIALIZER_LOG(INFO) << LOG_BADGE("RPCInitializer")
                          << LOG_DESC("ChannelRPCHttpServer started.");
    m_channelRPCServer->setCallbackSetter(std::bind(&rpc::Rpc::setCurrentTransactionCallback,
        rpcEntity, std::placeholders::_1, std::placeholders::_2));
}

void RPCInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    std::string listenIP = _pt.get<std::string>("rpc.listen_ip", "127.0.0.1");
    // listen ip for jsonrpc, load from rpc.listen ip if rpc.jsonrpc_listen_ip is not setted
    listenIP = _pt.get<std::string>("rpc.jsonrpc_listen_ip", listenIP);

    int listenPort = _pt.get<int>("rpc.channel_listen_port", 20200);
    int httpListenPort = _pt.get<int>("rpc.jsonrpc_listen_port", 8545);
    if (!isValidPort(listenPort) || !isValidPort(httpListenPort))
    {
        ERROR_OUTPUT << LOG_BADGE("RPCInitializer")
                     << LOG_DESC(
                            "initConfig for RPCInitializer failed! Invalid ListenPort for RPC!")
                     << std::endl;
        exit(1);
    }

    try
    {
        // m_rpcForChannel is created in initChannelRPCServer, now complete m_rpcForChannel
        m_rpcForChannel->setLedgerInitializer(m_ledgerInitializer);
        m_rpcForChannel->setService(m_p2pService);
        // event log filter callback
        m_channelRPCServer->setEventFilterCallback(
            [this](const std::string& _json, uint32_t _version,
                std::function<bool(const std::string& _filterID, int32_t _result,
                    const Json::Value& _logs, GROUP_ID const& _groupId)>
                    _respCallback,
                std::function<bool()> _activeCallback) -> int32_t {
                auto params =
                    dev::event::EventLogFilterParams::buildEventLogFilterParamsObject(_json);
                if (!params)
                {  // json parser failed
                    return dev::event::ResponseCode::INVALID_REQUEST;
                }

                auto ledger = getLedgerManager()->ledger(params->getGroupID());
                if (!ledger)
                {
                    return dev::event::ResponseCode::GROUP_NOT_EXIST;
                }

                return ledger->getEventLogFilterManager()->addEventLogFilterByRequest(
                    params, _version, _respCallback, _activeCallback);
            });

        auto channelRPCServerWeak = std::weak_ptr<dev::ChannelRPCServer>(m_channelRPCServer);
        m_p2pService->setCallbackFuncForTopicVerify(
            [channelRPCServerWeak](const std::string& _1, const std::string& _2) {
                auto channelRPCServer = channelRPCServerWeak.lock();
                if (channelRPCServer)
                {
                    channelRPCServer->asyncPushChannelMessageHandler(_1, _2);
                }
            });

        // Don't to set destructor, the ModularServer will destruct.
        auto rpcEntity = new rpc::Rpc(m_ledgerInitializer, m_p2pService);
        auto ipAddress = boost::asio::ip::make_address(listenIP);
        m_safeHttpServer.reset(new SafeHttpServer(listenIP, httpListenPort, ipAddress.is_v6()),
            [](SafeHttpServer* p) { (void)p; });
        m_jsonrpcHttpServer = new ModularServer<rpc::Rpc>(rpcEntity);
        m_jsonrpcHttpServer->addConnector(m_safeHttpServer.get());
        // TODO: StartListening() will throw exception, catch it and give more specific help
        if (!m_jsonrpcHttpServer->StartListening())
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("RPCInitializer") << LOG_KV("ipv6", ipAddress.is_v6())
                << LOG_KV("check jsonrpc_listen_port", httpListenPort);
            ERROR_OUTPUT << LOG_BADGE("RPCInitializer") << LOG_KV("ipv6", ipAddress.is_v6())
                         << LOG_KV("check jsonrpc_listen_port", httpListenPort) << std::endl;
            BOOST_THROW_EXCEPTION(ListenPortIsUsed());
        }
        INITIALIZER_LOG(INFO) << LOG_BADGE("RPCInitializer JsonrpcHttpServer started")
                              << LOG_KV("rpcListenIp", listenIP)
                              << LOG_KV("rpcListenPort", httpListenPort)
                              << LOG_KV("ipv6", ipAddress.is_v6());
    }
    catch (std::exception& e)
    {
        // TODO: catch in Initializer::init, delete this catch
        INITIALIZER_LOG(ERROR) << LOG_BADGE("RPCInitializer")
                               << LOG_DESC("init RPC/channelserver failed")
                               << LOG_KV("check channel_listen_port", listenPort)
                               << LOG_KV("check jsonrpc_listen_port", httpListenPort)
                               << LOG_KV("EINFO", boost::diagnostic_information(e));

        ERROR_OUTPUT << LOG_BADGE("RPCInitializer") << LOG_DESC("init RPC/channelserver failed")
                     << LOG_KV("check channel_listen_port", listenPort)
                     << LOG_KV("check jsonrpc_listen_port", httpListenPort)
                     << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
        exit(1);
    }
}

dev::stat::ChannelNetworkStatHandler::Ptr RPCInitializer::createNetWorkStatHandler(
    boost::property_tree::ptree const& _pt)
{
    auto networkStatHandler = std::make_shared<dev::stat::ChannelNetworkStatHandler>("SDK");
    // get flush interval in seconds
    int64_t flushInterval = _pt.get<int64_t>("log.stat_flush_interval", 60);
    if (flushInterval <= 0)
    {
        BOOST_THROW_EXCEPTION(
            dev::InvalidConfig() << errinfo_comment(
                "init network-stat-handler failed, log.stat_flush_interval must be positive!"));
    }
    INITIALIZER_LOG(DEBUG) << LOG_DESC("createNetWorkStatHandler")
                           << LOG_KV("flushInterval(s)", flushInterval);

    networkStatHandler->setFlushInterval(flushInterval * 1000);
    return networkStatHandler;
}

dev::flowlimit::RPCQPSLimiter::Ptr RPCInitializer::createQPSLimiter(
    boost::property_tree::ptree const& _pt)
{
    auto qpsLimiter = std::make_shared<dev::flowlimit::RPCQPSLimiter>();
    int64_t maxQPS = _pt.get<int64_t>("flow_control.limit_req", INT64_MAX);
    // the limit_req has not been setted
    if (maxQPS == INT64_MAX)
    {
        INITIALIZER_LOG(DEBUG) << LOG_DESC(
            "disable QPSLimit for flow_control.limit_req has not been setted!");
        return qpsLimiter;
    }
    if (maxQPS <= 0)
    {
        BOOST_THROW_EXCEPTION(
            dev::InvalidConfig() << errinfo_comment(
                "createQPSLimiter failed, flow_control.limit_req must be positive!"));
    }
    INITIALIZER_LOG(DEBUG) << LOG_DESC("createQPSLimiter") << LOG_KV("maxQPS", maxQPS);
    qpsLimiter->createRPCQPSLimiter(maxQPS);
    return qpsLimiter;
}

dev::flowlimit::RateLimiter::Ptr RPCInitializer::createNetworkBandwidthLimit(
    boost::property_tree::ptree const& _pt)
{
    auto outGoingBandwidthLimit =
        _pt.get<double>("flow_control.outgoing_bandwidth_limit", INT64_MAX);
    // default value
    if (outGoingBandwidthLimit == (double)INT64_MAX)
    {
        INITIALIZER_LOG(DEBUG) << LOG_DESC("Disable NetworkBandwidthLimit for channel");
        return nullptr;
    }
    // Configured outgoing_bandwidth_limit
    if (outGoingBandwidthLimit <= (double)(-0.0) ||
        outGoingBandwidthLimit >= (double)MAX_VALUE_IN_Mb)
    {
        BOOST_THROW_EXCEPTION(dev::InvalidConfig() << errinfo_comment(
                                  "createNetworkBandwidthLimit for channel failed, "
                                  "flow_control.limit_req must be larger than 0 and smaller than " +
                                  std::to_string(MAX_VALUE_IN_Mb)));
    }
    outGoingBandwidthLimit *= 1024 * 1024 / 8;
    auto bandwidthLimiter =
        std::make_shared<dev::flowlimit::RateLimiter>((int64_t)outGoingBandwidthLimit);
    bandwidthLimiter->setMaxPermitsSize(g_BCOSConfig.c_maxPermitsSize);
    INITIALIZER_LOG(INFO) << LOG_BADGE("createNetworkBandwidthLimit")
                          << LOG_KV("outGoingBandwidthLimit(Bytes)", outGoingBandwidthLimit)
                          << LOG_KV("maxPermitsSize", g_BCOSConfig.c_maxPermitsSize);
    return bandwidthLimiter;
}