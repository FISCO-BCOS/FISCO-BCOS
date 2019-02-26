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

using namespace dev;
using namespace dev::initializer;

void RPCInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    std::string listenIP = _pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
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
    /// init channelServer
    ChannelRPCServer::Ptr m_channelRPCServer;
    ///< TODO: Double free or no free?
    ///< Donot to set destructions, the ModularServer will destruct.
    try
    {
        m_channelRPCServer.reset(new ChannelRPCServer(), [](ChannelRPCServer* p) { (void)p; });
        m_channelRPCServer->setListenAddr(listenIP);
        m_channelRPCServer->setListenPort(listenPort);
        m_channelRPCServer->setSSLContext(m_sslContext);
        m_channelRPCServer->setService(m_p2pService);

        auto ioService = std::make_shared<boost::asio::io_service>();

        auto server = std::make_shared<dev::channel::ChannelServer>();
        server->setIOService(ioService);
        server->setSSLContext(m_sslContext);
        server->setEnableSSL(true);
        server->setBind(listenIP, listenPort);
        server->setMessageFactory(std::make_shared<dev::channel::ChannelMessageFactory>());

        m_channelRPCServer->setChannelServer(server);


        auto rpcEntity = new rpc::Rpc(m_ledgerManager, m_p2pService);
        m_channelRPCHttpServer = new ModularServer<rpc::Rpc>(rpcEntity);
        m_channelRPCHttpServer->addConnector(m_channelRPCServer.get());
        m_channelRPCHttpServer->StartListening();
        INITIALIZER_LOG(INFO) << LOG_BADGE("RPCInitializer")
                              << LOG_DESC("ChannelRPCHttpServer started.");

        m_channelRPCServer->setCallbackSetter(
            std::bind(&rpc::Rpc::setCurrentTransactionCallback, rpcEntity, std::placeholders::_1));

        for (auto it : m_ledgerManager->getGrouplList())
        {
            auto groupID = it;
            auto blockChain = m_ledgerManager->blockChain(it);
            auto channelRPCServer = std::weak_ptr<dev::ChannelRPCServer>(m_channelRPCServer);
            auto handler = blockChain->onReady([groupID, channelRPCServer](int64_t number) {
                LOG(TRACE) << "Push block notify: " << (int)groupID << "-" << number;
                auto c = channelRPCServer.lock();

                if (c)
                {
                    std::string topic =
                        "_block_notify_" + boost::lexical_cast<std::string>((int)groupID);
                    std::string content = boost::lexical_cast<std::string>(groupID) + "," +
                                          boost::lexical_cast<std::string>(number);
                    std::shared_ptr<dev::channel::TopicChannelMessage> message =
                        std::make_shared<dev::channel::TopicChannelMessage>();
                    message->setType(0x1001);
                    message->setSeq(std::string(32, '0'));
                    message->setResult(0);
                    message->setTopic(topic);
                    message->setData((const byte*)content.data(), content.size());
                    c->asyncBroadcastChannelMessage(topic, message);
                }
            });

            m_channelRPCServer->addHandler(handler);
        }

        /// init httpListenPort
        ///< Donot to set destructions, the ModularServer will destruct.
        m_safeHttpServer.reset(
            new SafeHttpServer(listenIP, httpListenPort), [](SafeHttpServer* p) { (void)p; });
        m_jsonrpcHttpServer = new ModularServer<rpc::Rpc>(rpcEntity);
        m_jsonrpcHttpServer->addConnector(m_safeHttpServer.get());
        m_jsonrpcHttpServer->StartListening();
        INITIALIZER_LOG(INFO) << LOG_BADGE("RPCInitializer")
                              << LOG_DESC("JsonrpcHttpServer started.");
    }
    catch (std::exception& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("RPCInitializer")
                               << LOG_DESC("init RPC/channelserver failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));

        ERROR_OUTPUT << LOG_BADGE("RPCInitializer") << LOG_DESC("init RPC/channelserver failed")
                     << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
        exit(1);
    }
}
