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
/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include "Service.h"
#include "Common.h"
#include "P2PMessage.h"
#include "libdevcore/FixedHash.h"      // for FixedHash, hash
#include "libdevcore/ThreadPool.h"     // for ThreadPool
#include "libnetwork/ASIOInterface.h"  // for ASIOInterface
#include "libnetwork/Common.h"         // for SocketFace
#include "libnetwork/SocketFace.h"     // for SocketFace
#include "libp2p/P2PInterface.h"       // for CallbackFunc...
#include "libp2p/P2PMessageFactory.h"  // for P2PMessageFa...
#include "libp2p/P2PSession.h"         // for P2PSession
#include <boost/random.hpp>
#include <unordered_map>

using namespace dev;
using namespace dev::p2p;
using namespace dev::network;

static const uint32_t CHECK_INTERVEL = 10000;

Service::Service()
  : m_topics(std::make_shared<std::set<std::string>>()),
    m_protocolID2Handler(std::make_shared<std::unordered_map<uint32_t, CallbackFuncWithSession>>()),
    m_topic2Handler(std::make_shared<std::unordered_map<std::string, CallbackFuncWithSession>>())
{}

void Service::start()
{
    if (!m_run)
    {
        m_run = true;

        auto self = std::weak_ptr<Service>(shared_from_this());
        m_host->setConnectionHandler(
            [self](dev::network::NetworkException e, dev::network::NodeInfo const& nodeInfo,
                std::shared_ptr<dev::network::SessionFace> session) {
                auto service = self.lock();
                if (service)
                {
                    service->onConnect(e, nodeInfo, session);
                }
            });
        m_host->start();

        heartBeat();
    }
}

void Service::stop()
{
    if (m_run)
    {
        m_run = false;
        if (m_timer)
        {
            m_timer->cancel();
        }
        m_host->stop();
        /// disconnect sessions

        RecursiveGuard l(x_sessions);
        for (auto session : m_sessions)
        {
            session.second->stop(dev::network::ClientQuit);
        }


        /// clear sessions
        m_sessions.clear();
    }
}

void Service::heartBeat()
{
    if (!m_run)
    {
        return;
    }

    checkWhitelistAndClearSession();

    std::map<dev::network::NodeIPEndpoint, NodeID> staticNodes;
    {
        RecursiveGuard l(x_nodes);
        staticNodes = m_staticNodes;
    }

    // Reconnect all nodes
    for (auto& it : staticNodes)
    {
        /// exclude myself
        if (it.second == id())
        {
            SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat ignore myself nodeID same")
                               << LOG_KV("remote endpoint", it.first.name())
                               << LOG_KV("nodeID", it.second.abridged());
            continue;
        }
        if (it.second != NodeID() && isConnected(it.second))
        {
            SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat ignore connected")
                               << LOG_KV("endpoint", it.first.name())
                               << LOG_KV("nodeID", it.second.abridged());
            continue;
        }
        if (it.first.host.empty() || it.first.port.empty())
        {
            SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat ignore invalid address");
            continue;
        }
        SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat try to reconnect")
                           << LOG_KV("endpoint", it.first.name());
        m_host->asyncConnect(
            it.first, std::bind(&Service::onConnect, shared_from_this(), std::placeholders::_1,
                          std::placeholders::_2, std::placeholders::_3));
    }
    {
        RecursiveGuard l(x_sessions);
        SERVICE_LOG(INFO) << LOG_DESC("heartBeat") << LOG_KV("connected count", m_sessions.size());
    }

    auto self = std::weak_ptr<Service>(shared_from_this());
    m_timer = m_host->asioInterface()->newTimer(CHECK_INTERVEL);
    m_timer->async_wait([self](const boost::system::error_code& error) {
        if (error)
        {
            SERVICE_LOG(TRACE) << "timer canceled" << LOG_KV("errorCode", error);
            return;
        }
        auto service = self.lock();
        if (service && service->host()->haveNetwork())
        {
            service->heartBeat();
        }
    });
}

// reset whitelist: stop session which is not in whitelist
void Service::setWhitelist(PeerWhitelist::Ptr _whitelist)
{
    m_whitelist = _whitelist;
    host()->setWhitelist(_whitelist);
    checkWhitelistAndClearSession();
    SERVICE_LOG(DEBUG) << LOG_BADGE("Whitelist") << LOG_DESC("Set whitelist")
                       << m_whitelist->dump(true);
}

void Service::checkWhitelistAndClearSession()
{
    if (m_whitelist != nullptr && m_whitelist->enable())
    {
        std::unordered_map<NodeID, P2PSession::Ptr> sessions;
        {
            RecursiveGuard l(x_sessions);
            sessions = m_sessions;
        }
        for (auto session : sessions)
        {
            NodeID nodeID = session.first;
            if (m_whitelist->has(nodeID))
            {
                // Find in whitelist
                continue;
            }

            auto p2pSession = session.second;
            std::string msg = "resetWhitelist: node " + nodeID.abridged() + " is not in whitelist";
            NetworkException e(dev::network::P2PExceptionType::NotInWhitelist, msg);
            p2pSession->stop(dev::network::UselessPeer);
            onDisconnect(e, p2pSession);
            SERVICE_LOG(DEBUG) << LOG_BADGE("Whitelist")
                               << LOG_DESC("Disconnect section outside whitelist")
                               << LOG_KV("nodeId", nodeID.abridged());
        }
    }
}

/// update the staticNodes
void Service::updateStaticNodes(
    std::shared_ptr<dev::network::SocketFace> const& _s, dev::network::NodeID const& nodeID)
{
    dev::network::NodeIPEndpoint endpoint(_s->nodeIPEndpoint());
    RecursiveGuard l(x_nodes);
    auto it = m_staticNodes.find(endpoint);
    // modify m_staticNodes(including accept cases, namely the client endpoint)
    if (it != m_staticNodes.end())
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes") << LOG_KV("nodeID", nodeID.abridged())
                           << LOG_KV("endpoint", endpoint.name());
        it->second = nodeID;
    }
    else
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes can't find endpoint")
                           << LOG_KV("nodeID", nodeID.abridged())
                           << LOG_KV("endpoint", endpoint.name());
    }
}

void Service::onConnect(dev::network::NetworkException e, dev::network::NodeInfo const& nodeInfo,
    std::shared_ptr<dev::network::SessionFace> session)
{
    NodeID nodeID = nodeInfo.nodeID;
    if (e.errorCode())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onConnect") << LOG_KV("errorCode", e.errorCode())
                             << LOG_KV("what", e.what());

        return;
    }

    SERVICE_LOG(TRACE) << LOG_DESC("Service onConnect") << LOG_KV("nodeID", nodeID.abridged());

    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);
    if (it != m_sessions.end() && it->second->actived())
    {
        SERVICE_LOG(TRACE) << "Disconnect duplicate peer" << LOG_KV("nodeID", nodeID.abridged());
        updateStaticNodes(session->socket(), nodeID);
        session->disconnect(dev::network::DuplicatePeer);
        return;
    }

    if (nodeID == id())
    {
        SERVICE_LOG(TRACE) << "Disconnect self";
        updateStaticNodes(session->socket(), id());
        session->disconnect(dev::network::DuplicatePeer);
        return;
    }

    auto p2pSession = std::make_shared<P2PSession>();
    p2pSession->setSession(session);
    p2pSession->setNodeInfo(nodeInfo);
    p2pSession->setService(std::weak_ptr<Service>(shared_from_this()));
    p2pSession->session()->setMessageHandler(std::bind(&Service::onMessage, shared_from_this(),
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, p2pSession));
    p2pSession->start();
    updateStaticNodes(session->socket(), nodeID);
    if (it != m_sessions.end())
    {
        it->second = p2pSession;
    }
    else
    {
        m_sessions.insert(std::make_pair(nodeID, p2pSession));
    }
    SERVICE_LOG(INFO) << LOG_DESC("Connection established") << LOG_KV("nodeID", nodeID.abridged())
                      << LOG_KV("endpoint", session->nodeIPEndpoint().name());
}

void Service::onDisconnect(dev::network::NetworkException e, P2PSession::Ptr p2pSession)
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(p2pSession->nodeID());
    if (it != m_sessions.end() && it->second == p2pSession)
    {
        SERVICE_LOG(TRACE) << "Service onDisconnect and remove from m_sessions"
                           << LOG_KV("nodeID", p2pSession->nodeID().abridged())
                           << LOG_KV("endpoint", p2pSession->session()->nodeIPEndpoint().name());

        m_sessions.erase(it);
        if (e.errorCode() == dev::network::P2PExceptionType::DuplicateSession)
            return;
        SERVICE_LOG(WARNING) << LOG_DESC("onDisconnect") << LOG_KV("errorCode", e.errorCode())
                             << LOG_KV("what", boost::diagnostic_information(e));
        RecursiveGuard l(x_nodes);
        for (auto& it : m_staticNodes)
        {
            if (it.second == p2pSession->nodeID())
            {
                it.second = NodeID();
                break;
            }
        }
    }
}

void Service::onMessage(dev::network::NetworkException e, dev::network::SessionFace::Ptr session,
    dev::network::Message::Ptr message, P2PSession::Ptr p2pSession)
{
    try
    {
        if (e.errorCode())
        {
            SERVICE_LOG(WARNING) << LOG_DESC("disconnect error P2PSession")
                                 << LOG_KV("nodeID", p2pSession->nodeID().abridged())
                                 << LOG_KV("endpoint", session->nodeIPEndpoint().name())
                                 << LOG_KV("errorCode", e.errorCode()) << LOG_KV("what", e.what());

            p2pSession->stop(dev::network::UserReason);
            onDisconnect(e, p2pSession);
            return;
        }

        // update the network-in packets information
        if (m_statisticHandler)
        {
            auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
            m_statisticHandler->updateServiceInPackets(p2pMessage);
        }

        /// SERVICE_LOG(TRACE) << "Service onMessage: " << message->seq();

        auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);

        // AMOP topic message, redirect to p2psession
        if (abs(p2pMessage->protocolID()) == dev::eth::ProtocolID::Topic)
        {
            p2pSession->onTopicMessage(p2pMessage);
            return;
        }

        if (p2pMessage->isRequestPacket())
        {
            CallbackFuncWithSession callback;
            {
                RecursiveGuard lock(x_protocolID2Handler);
                auto it = m_protocolID2Handler->find(p2pMessage->protocolID());
                if (it != m_protocolID2Handler->end())
                {
                    callback = it->second;
                }
            }

            if (callback)
            {
                m_host->threadPool()->enqueue([callback, p2pSession, p2pMessage, e]() {
                    callback(e, p2pSession, p2pMessage);
                });
            }
            else
            {
                SERVICE_LOG(TRACE) << LOG_DESC("Request message doesn't have callback")
                                   << LOG_KV("messageSeq", message->seq());
            }
        }
        else
        {
            SERVICE_LOG(WARNING) << LOG_DESC("Response packet not found may be timeout")
                                 << LOG_KV("messageSeq", p2pMessage->seq());
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "onMessage error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

P2PMessage::Ptr Service::sendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message)
{
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            typedef std::shared_ptr<SessionCallback> Ptr;

            SessionCallback() { mutex.lock(); }

            void onResponse(dev::network::NetworkException _error, std::shared_ptr<P2PSession>,
                P2PMessage::Ptr _message)
            {
                error = _error;
                response = _message;
                mutex.unlock();
            }

            dev::network::NetworkException error;
            P2PMessage::Ptr response;
            std::mutex mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFuncWithSession fp = std::bind(&SessionCallback::onResponse, callback,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        asyncSendMessageByNodeID(nodeID, message, fp, dev::network::Options());
        // lock to wait for async send
        callback->mutex.lock();
        callback->mutex.unlock();
        SERVICE_LOG(DEBUG) << LOG_DESC("sendMessageByNodeID mutex unlock");

        dev::network::NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            SERVICE_LOG(ERROR) << LOG_DESC("asyncSendMessageByNodeID error")
                               << LOG_KV("nodeID", nodeID.abridged())
                               << LOG_KV("errorCode", error.errorCode())
                               << LOG_KV("what", error.what());
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << LOG_DESC("asyncSendMessageByNodeID error")
                           << LOG_KV("nodeID", nodeID.abridged())
                           << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return P2PMessage::Ptr();
}

void Service::asyncSendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message,
    CallbackFuncWithSession callback, dev::network::Options options)
{
    try
    {
        if (nodeID == id())
        {
            // exclude myself
            return;
        }
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(nodeID);

        if (it != m_sessions.end() && it->second->actived())
        {
            if (message->seq() == 0)
            {
                message->setSeq(m_p2pMessageFactory->newSeq());
            }
            auto session = it->second;
            if (callback)
            {
                session->session()->asyncSendMessage(message, options,
                    [session, callback](
                        dev::network::NetworkException e, dev::network::Message::Ptr message) {
                        P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
                        if (callback)
                        {
                            callback(e, session, p2pMessage);
                        }
                    });
            }
            else
            {
                session->session()->asyncSendMessage(message, options, nullptr);
            }
            // update the network-out packets information
            if (m_statisticHandler)
            {
                m_statisticHandler->updateServiceOutPackets(message);
            }
        }
        else
        {
            SERVICE_LOG(WARNING) << "Node inactived" << LOG_KV("nodeID", nodeID.abridged());
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "asyncSendMessageByNodeID" << LOG_KV("nodeID", nodeID.abridged())
                           << LOG_KV("what", boost::diagnostic_information(e));

        if (callback)
        {
            m_host->threadPool()->enqueue([callback, e] {
                callback(NetworkException(dev::network::Disconnect, "Disconnect"),
                    P2PSession::Ptr(), P2PMessage::Ptr());
            });
        }
    }
}

P2PMessage::Ptr Service::sendMessageByTopic(std::string topic, P2PMessage::Ptr message)
{
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            typedef std::shared_ptr<SessionCallback> Ptr;

            SessionCallback() { mutex.lock(); }

            void onResponse(dev::network::NetworkException _error, std::shared_ptr<P2PSession>,
                P2PMessage::Ptr _message)
            {
                error = _error;
                response = _message;
                mutex.unlock();
            }

            dev::network::NetworkException error;
            P2PMessage::Ptr response;
            std::mutex mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFuncWithSession fp = std::bind(&SessionCallback::onResponse, callback,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        asyncSendMessageByTopic(topic, message, fp, dev::network::Options());

        dev::network::NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            SERVICE_LOG(ERROR) << LOG_DESC("sendMessageByTopic error") << LOG_KV("topic", topic)
                               << LOG_KV("errorCode", error.errorCode())
                               << LOG_KV("what", error.what());
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "sendMessageByTopic error"
                           << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return P2PMessage::Ptr();
}

void Service::asyncSendMessageByTopic(std::string topic, P2PMessage::Ptr message,
    CallbackFuncWithSession callback, dev::network::Options options)
{
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    if (nodeIDsToSend.size() == 0)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("asyncSendMessageByTopic no topic to be sent");
        if (callback)
        {
            m_host->threadPool()->enqueue([callback]() {
                dev::network::NetworkException e(TOPIC_NOT_FOUND, "No topic to be sent");
                callback(e, std::shared_ptr<dev::p2p::P2PSession>(), dev::p2p::P2PMessage::Ptr());
            });
        }
        return;
    }

    class TopicStatus : public std::enable_shared_from_this<TopicStatus>
    {
    public:
        void onResponse(
            dev::network::NetworkException e, P2PSession::Ptr session, P2PMessage::Ptr msg)
        {
            if (e.errorCode() != 0 || !m_current)
            {
                if (e.errorCode() != 0)
                {
                    SERVICE_LOG(WARNING) << LOG_DESC("Send topics message error")
                                         << LOG_KV("to", m_current) << LOG_KV("what", e.what());
                }

                if (m_nodeIDs.empty())
                {
                    SERVICE_LOG(WARNING) << LOG_DESC("Send topics message all failed");
                    if (m_callback)
                    {
                        m_callback(dev::network::NetworkException(
                                       e.errorCode(), "Send topics message all failed"),
                            session, P2PMessage::Ptr());
                    }
                    return;
                }

                boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
                boost::uniform_int<int> index(0, m_nodeIDs.size() - 1);

                auto ri = index(rng);

                m_current = m_nodeIDs[ri];
                m_nodeIDs.erase(m_nodeIDs.begin() + ri);

                auto s = m_service.lock();
                if (s)
                {
                    auto self = shared_from_this();

                    s->asyncSendMessageByNodeID(m_current, m_message,
                        std::bind(&TopicStatus::onResponse, shared_from_this(),
                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        m_options);
                }
            }
            else
            {
                if (m_callback)
                {
                    m_callback(e, session, msg);
                }
            }
        }

        dev::network::NodeID m_current;
        NodeIDs m_nodeIDs;
        CallbackFuncWithSession m_callback;
        P2PMessage::Ptr m_message;
        std::weak_ptr<Service> m_service;
        dev::network::Options m_options;
    };

    auto topicStatus = std::make_shared<TopicStatus>();
    topicStatus->m_nodeIDs = nodeIDsToSend;
    topicStatus->m_callback = callback;
    topicStatus->m_message = message;
    topicStatus->m_service = std::weak_ptr<Service>(shared_from_this());
    topicStatus->m_options = options;

    topicStatus->onResponse(dev::network::NetworkException(), P2PSession::Ptr(), message);
}

void Service::asyncMulticastMessageByTopic(std::string topic, P2PMessage::Ptr message)
{
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    SERVICE_LOG(DEBUG) << LOG_DESC("asyncMulticastMessageByTopic")
                       << LOG_KV("nodes", nodeIDsToSend.size());
    try
    {
        for (auto nodeID : nodeIDsToSend)
        {
            asyncSendMessageByNodeID(
                nodeID, message, CallbackFuncWithSession(), dev::network::Options());
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("asyncMulticastMessageByTopic")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void Service::asyncMulticastMessageByNodeIDList(NodeIDs nodeIDs, P2PMessage::Ptr message)
{
    SERVICE_LOG(TRACE) << "asyncMulticastMessageByNodeIDList"
                       << LOG_KV("nodes size", nodeIDs.size());
    try
    {
        for (auto nodeID : nodeIDs)
        {
            asyncSendMessageByNodeID(
                nodeID, message, CallbackFuncWithSession(), dev::network::Options());
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("asyncMulticastMessageByNodeIDList")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void Service::asyncBroadcastMessage(P2PMessage::Ptr message, dev::network::Options options)
{
    try
    {
        std::unordered_map<NodeID, P2PSession::Ptr> sessions;
        {
            RecursiveGuard l(x_sessions);
            sessions = m_sessions;
        }

        for (auto s : sessions)
        {
            asyncSendMessageByNodeID(s.first, message, CallbackFuncWithSession(), options);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("asyncBroadcastMessage")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void Service::registerHandlerByProtoclID(PROTOCOL_ID protocolID, CallbackFuncWithSession handler)
{
    RecursiveGuard l(x_protocolID2Handler);
    auto it = m_protocolID2Handler->find(protocolID);
    if (it == m_protocolID2Handler->end())
    {
        m_protocolID2Handler->insert(std::make_pair(protocolID, handler));
    }
    else
    {
        it->second = handler;
    }
}

void Service::removeHandlerByProtocolID(PROTOCOL_ID const& _protocolID)
{
    RecursiveGuard l(x_protocolID2Handler);
    if (m_protocolID2Handler && m_protocolID2Handler->count(_protocolID))
    {
        m_protocolID2Handler->erase(_protocolID);
    }
}

void Service::registerHandlerByTopic(std::string topic, CallbackFuncWithSession handler)
{
    RecursiveGuard l(x_topic2Handler);
    auto it = m_topic2Handler->find(topic);
    if (it == m_topic2Handler->end())
    {
        m_topic2Handler->insert(std::make_pair(topic, handler));
    }
    else
    {
        it->second = handler;
    }
}

P2PSessionInfos Service::sessionInfos()
{
    P2PSessionInfos infos;
    try
    {
        RecursiveGuard l(x_sessions);
        auto s = m_sessions;
        for (auto const& i : s)
        {
            infos.push_back(P2PSessionInfo(
                i.second->nodeInfo(), i.second->session()->nodeIPEndpoint(), (i.second->topics())));
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("sessionInfos")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
    return infos;
}

P2PSessionInfos Service::sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const
{
    std::pair<GROUP_ID, MODULE_ID> ret = dev::eth::getGroupAndProtocol(_protocolID);
    P2PSessionInfos infos;

    RecursiveGuard l(x_nodeList);
    auto it = m_groupID2NodeList.find(int(ret.first));
    if (it == m_groupID2NodeList.end())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("sessionInfosByProtocolID cannot find GroupID")
                             << LOG_KV("GroupID", int(ret.first));
        return infos;
    }
    try
    {
        RecursiveGuard l(x_sessions);
        for (auto const& i : m_sessions)
        {
            /// ignore the node self and the inactived session
            if (i.first == id() || false == i.second->actived())
            {
                continue;
            }
            if (find(it->second.begin(), it->second.end(), i.first) != it->second.end())
            {
                infos.push_back(P2PSessionInfo(i.second->nodeInfo(),
                    i.second->session()->nodeIPEndpoint(), i.second->topics()));
            }
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << LOG_DESC("sessionInfosByProtocolID")
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
    return infos;
}

NodeIDs Service::getPeersByTopic(std::string const& topic)
{
    NodeIDs nodeList;
    try
    {
        RecursiveGuard l(x_sessions);
        auto s = m_sessions;
        for (auto const& it : s)
        {
            for (auto& j : it.second->topics())
            {
                if ((j.topic == topic) && (j.topicStatus == TopicStatus::VERIFYI_SUCCESS_STATUS))
                {
                    nodeList.push_back(it.first);
                }
            }
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("getPeersByTopic")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
    SERVICE_LOG(DEBUG) << LOG_DESC("getPeersByTopic") << LOG_KV("topic", topic)
                       << LOG_KV("peersSize", nodeList.size());
    return nodeList;
}

bool Service::isConnected(NodeID const& nodeID) const
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);

    if (it != m_sessions.end() && it->second->actived())
    {
        return true;
    }
    return false;
}
