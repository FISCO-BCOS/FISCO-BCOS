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
#include <libdevcore/Common.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/easylog.h>
#include <libnetwork/Common.h>
#include <libnetwork/Host.h>
#include <libp2p/Service.h>
#include <boost/random.hpp>
#include <unordered_map>

using namespace dev;
using namespace dev::p2p;

static const uint32_t CHECK_INTERVEL = 10000;

Service::Service()
{
    m_protocolID2Handler =
        std::make_shared<std::unordered_map<uint32_t, CallbackFuncWithSession>>();
    m_topic2Handler = std::make_shared<std::unordered_map<std::string, CallbackFuncWithSession>>();
    m_topics = std::make_shared<std::vector<std::string>>();
}

void Service::start()
{
    if (!m_run)
    {
        m_run = true;

        auto self = std::weak_ptr<Service>(shared_from_this());
        m_host->setConnectionHandler([self](dev::network::NetworkException e, NodeID nodeID,
                                         std::shared_ptr<dev::network::SessionFace> session) {
            auto service = self.lock();
            if (service)
            {
                service->onConnect(e, nodeID, session);
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
        {
            DEV_RECURSIVE_GUARDED(x_sessions)
            for (auto session : m_sessions)
            {
                session.second->stop(dev::network::ClientQuit);
            }
        }

        /// clear sessions
        RecursiveGuard l(x_sessions);
        m_sessions.clear();
    }
}

void Service::heartBeat()
{
    if (!m_run)
    {
        return;
    }
    std::map<dev::network::NodeIPEndpoint, NodeID> staticNodes;
    std::unordered_map<NodeID, P2PSession::Ptr> sessions;

    {
        RecursiveGuard l(x_sessions);
        sessions = m_sessions;
        staticNodes = m_staticNodes;
    }

    // Reconnect all nodes
    for (auto it : staticNodes)
    {
        if ((it.first.address == m_host->tcpClient().address() &&
                it.first.tcpPort == m_host->listenPort()))
        {
            SERVICE_LOG(DEBUG) << "[#heartBeat] ignore myself [address]: " << m_host->listenHost()
                               << std::endl;
            continue;
        }
        /// exclude myself
        if (it.second == id())
        {
            SERVICE_LOG(DEBUG) << "[#heartBeat] ignore myself [nodeId]: " << it.second << std::endl;
            continue;
        }
        if (it.second != NodeID() && isConnected(it.second))
        {
            SERVICE_LOG(DEBUG) << "[#heartBeat] ignore connected [nodeId]: " << it.second
                               << std::endl;
            continue;
        }
        if (it.first.address.to_string().empty())
        {
            SERVICE_LOG(DEBUG) << "[#heartBeat] ignore invalid address" << std::endl;
            continue;
        }
        SERVICE_LOG(DEBUG) << "[#heartBeat] try to reconnect [endpoint]" << it.first.name();
        m_host->asyncConnect(
            it.first, std::bind(&Service::onConnect, shared_from_this(), std::placeholders::_1,
                          std::placeholders::_2, std::placeholders::_3));
    }
    auto self = std::weak_ptr<Service>(shared_from_this());
    m_timer = m_host->asioInterface()->newTimer(CHECK_INTERVEL);
    m_timer->async_wait([self](const boost::system::error_code& error) {
        if (error)
        {
            SERVICE_LOG(TRACE) << "timer canceled" << error;
            return;
        }
        auto service = self.lock();
        if (service && service->host()->haveNetwork())
        {
            service->heartBeat();
        }
    });
}

/// update the staticNodes
void Service::updateStaticNodes(
    std::shared_ptr<dev::network::SocketFace> const& _s, dev::network::NodeID const& nodeId)
{
    /// update the staticNodes
    dev::network::NodeIPEndpoint endpoint(_s->remoteEndpoint().address().to_v4(),
        _s->remoteEndpoint().port(), _s->remoteEndpoint().port());
    RecursiveGuard l(x_nodes);
    auto it = m_staticNodes.find(endpoint);
    /// modify m_staticNodes(including accept cases, namely the client endpoint)
    if (it != m_staticNodes.end())
    {
        SERVICE_LOG(DEBUG) << "[#startPeerSession-updateStaticNodes] [nodeId/endpoint]:  "
                           << toHex(nodeId) << "/" << endpoint.name() << std::endl;
        it->second = nodeId;
    }
}

void Service::onConnect(dev::network::NetworkException e, dev::network::NodeID nodeID,
    std::shared_ptr<dev::network::SessionFace> session)
{
    if (e.errorCode())
    {
        SERVICE_LOG(ERROR) << "Connect error: " << boost::diagnostic_information(e);

        return;
    }

    SERVICE_LOG(TRACE) << "Service onConnect: " << nodeID;

    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);
    if (it != m_sessions.end() && it->second->actived())
    {
        SERVICE_LOG(TRACE) << "Disconnect duplicate peer";
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
    p2pSession->setNodeID(nodeID);
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
    SERVICE_LOG(INFO) << "Connection established to: " << nodeID << "@"
                      << session->nodeIPEndpoint().name();
}

void Service::onDisconnect(dev::network::NetworkException e, P2PSession::Ptr p2pSession)
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(p2pSession->nodeID());
    if (it != m_sessions.end() && it->second == p2pSession)
    {
        SERVICE_LOG(TRACE) << "Service onDisconnect: " << p2pSession->nodeID()
                           << " remove from m_sessions at"
                           << p2pSession->session()->nodeIPEndpoint().name();

        m_sessions.erase(it);
        if (e.errorCode() == dev::network::P2PExceptionType::DuplicateSession)
            return;
        SERVICE_LOG(WARNING) << "[#onDisconnect] [errCode/errMsg]" << e.errorCode() << "/"
                             << e.what();
        RecursiveGuard l(x_nodes);
        for (auto it : m_staticNodes)
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
            SERVICE_LOG(ERROR) << "P2PSession " << p2pSession->nodeID() << "@"
                               << session->nodeIPEndpoint().name()
                               << " error, disconnect: " << e.errorCode() << ", " << e.what();


            p2pSession->stop(dev::network::UserReason);
            onDisconnect(e, p2pSession);
            return;
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
                SERVICE_LOG(DEBUG) << "Request protocolID not found" << message->seq();
            }
        }
        else
        {
            SERVICE_LOG(WARNING) << "Response packet not found seq: " << p2pMessage->seq()
                                 << " response, may be timeout";
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "onMessage error: " << boost::diagnostic_information(e);
    }
}

P2PMessage::Ptr Service::sendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message)
{
    // P2PMSG_LOG(DEBUG) << "[#sendMessageByNodeID] [nodeID]: " << nodeID;
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            typedef std::shared_ptr<SessionCallback> Ptr;

            SessionCallback() { mutex.lock(); }

            void onResponse(dev::network::NetworkException _error,
                std::shared_ptr<P2PSession> session, P2PMessage::Ptr _message)
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

        callback->mutex.lock();
        callback->mutex.unlock();
        P2PMSG_LOG(DEBUG) << "[#sendMessageByNodeID] mutex unlock.";

        dev::network::NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            SERVICE_LOG(ERROR) << "asyncSendMessageByNodeID error:" << error.errorCode() << " "
                               << error.what();
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "ERROR:" << boost::diagnostic_information(e);
        BOOST_THROW_EXCEPTION(e);
    }

    return P2PMessage::Ptr();
}

void Service::asyncSendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message,
    CallbackFuncWithSession callback, dev::network::Options options)
{
    try
    {
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(nodeID);

        if (it != m_sessions.end() && it->second->actived())
        {
            message->setLength(P2PMessage::HEADER_LENGTH + message->buffer()->size());
            if (message->seq() == 0)
            {
                message->setSeq(m_p2pMessageFactory->newSeq());
            }
            auto session = it->second;
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
            SERVICE_LOG(WARNING) << "NodeID: " << nodeID.hex() << " inactived";

            BOOST_THROW_EXCEPTION(NetworkException(dev::network::Disconnect, "Disconnect"));
        }
    }
#if 0
    catch (NetworkException &e) {
        SERVICE_LOG(ERROR) << "NetworkException:" << boost::diagnostic_information(e);

        m_host->threadPool()->enqueue([callback, e] {
            callback(e, P2PSession::Ptr(), P2PMessage::Ptr());
        });
    }
#endif
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "ERROR:" << boost::diagnostic_information(e);

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

            void onResponse(dev::network::NetworkException _error,
                std::shared_ptr<P2PSession> session, P2PMessage::Ptr _message)
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

        callback->mutex.lock();
        callback->mutex.unlock();

        dev::network::NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            SERVICE_LOG(ERROR) << "asyncSendMessageByNodeID error:" << error.errorCode() << " "
                               << error.what();
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "ERROR:" << boost::diagnostic_information(e);
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
        SERVICE_LOG(WARNING) << "[#asyncSendMessageByTopic] no topic to be sent.";

        m_host->threadPool()->enqueue([callback]() {
            dev::network::NetworkException e(TOPIC_NOT_FOUND, "No topic to be sent");
            callback(e, std::shared_ptr<dev::p2p::P2PSession>(), dev::p2p::P2PMessage::Ptr());
        });

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
                    SERVICE_LOG(WARNING)
                        << "Send topics message to " << m_current << "error once: " << e.what();
                }

                if (m_nodeIDs.empty())
                {
                    SERVICE_LOG(WARNING) << "Send topics message all failed";
                    m_callback(dev::network::NetworkException(
                                   e.errorCode(), "Send topics message all failed"),
                        session, P2PMessage::Ptr());

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
                m_callback(e, session, msg);
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
    P2PMSG_LOG(DEBUG) << "[#asyncMulticastMessageByTopic] [node size]: " << nodeIDsToSend.size();
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
        P2PMSG_LOG(WARNING) << "[#asyncMulticastMessageByTopic] [EINFO]: " << e.what();
    }
}

void Service::asyncMulticastMessageByNodeIDList(NodeIDs nodeIDs, P2PMessage::Ptr message)
{
    SERVICE_LOG(TRACE) << "Call Service::asyncMulticastMessageByNodeIDList nodes size="
                       << nodeIDs.size();
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
        P2PMSG_LOG(WARNING) << "[#asyncMulticastMessageByNodeIDList] [EINFO]: " << e.what();
    }
}

void Service::asyncBroadcastMessage(P2PMessage::Ptr message, dev::network::Options options)
{
    P2PMSG_LOG(DEBUG) << "[#asyncMulticastMessageByNodeIDList]";
    try
    {
        std::unordered_map<NodeID, P2PSession::Ptr> sessions;
        {
            RecursiveGuard l(x_sessions);
            sessions = m_sessions;
        }

        for (auto s : sessions)
        {
            asyncSendMessageByNodeID(
                s.first, message, CallbackFuncWithSession(), dev::network::Options());
        }
    }
    catch (std::exception& e)
    {
        P2PMSG_LOG(WARNING) << "[#asyncBroadcastMessage] [EINFO]: " << e.what();
    }
}

bool Service::isSessionInNodeIDList(NodeID const& targetNodeID, NodeIDs const& nodeIDs)
{
    for (auto const& nodeID : nodeIDs)
    {
        if (targetNodeID == nodeID)
            return true;
    }
    return false;
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
                i.first, i.second->session()->nodeIPEndpoint(), *(i.second->topics())));
        }
    }
    catch (std::exception& e)
    {
        P2PMSG_LOG(WARNING) << "[#sessionInfos] [EINFO]: " << e.what();
    }
    return infos;
}

P2PSessionInfos Service::sessionInfosByProtocolID(PROTOCOL_ID _protocolID)
{
    std::pair<GROUP_ID, MODULE_ID> ret = dev::eth::getGroupAndProtocol(_protocolID);
    P2PSessionInfos infos;

    RecursiveGuard l(x_nodeList);
    auto it = m_groupID2NodeList.find(int(ret.first));
    if (it == m_groupID2NodeList.end())
    {
        SERVICE_LOG(WARNING) << "[#sessionInfosByProtocolID] cannot find GroupID "
                             << int(ret.first);
        return infos;
    }
    try
    {
        RecursiveGuard l(x_sessions);
        for (auto const& i : m_sessions)
        {
            if (find(it->second.begin(), it->second.end(), i.first) != it->second.end())
            {
                infos.push_back(P2PSessionInfo(
                    i.first, i.second->session()->nodeIPEndpoint(), *(i.second->topics())));
            }
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "[#sessionInfosByProtocolID] error:" << e.what();
    }

    // P2PMSG_LOG(DEBUG) << "[#sessionInfosByProtocolID] Finding nodeID in GroupID[" <<
    // int(ret.first) << "], consensus nodes size:" << it->second.size() << ", " <<
    // printSessionInfos(infos);
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
            for (auto j : *(it.second->topics()))
            {
                if (j == topic)
                {
                    nodeList.push_back(it.first);
                }
            }
        }
    }
    catch (std::exception& e)
    {
        P2PMSG_LOG(WARNING) << "[#getPeersByTopic] [EINFO]: " << e.what();
    }
    P2PMSG_LOG(DEBUG) << "[#getPeersByTopic] [topic/peers size]: " << topic << "/"
                      << nodeList.size();
    return nodeList;
}

bool Service::isConnected(NodeID nodeID)
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);

    if (it == m_sessions.end())
    {
        return false;
    }

    if (!it->second->actived())
    {
        return false;
    }

    return true;
}

std::string Service::printSessionInfos(P2PSessionInfos const& sessionInfos) const
{
    std::ostringstream oss;
    oss << "return list size:" << sessionInfos.size();
    for (auto const& it : sessionInfos)
    {
        oss << ", node " << it.nodeID.abridged() << " on " << it.nodeIPEndpoint.address << ":"
            << it.nodeIPEndpoint.tcpPort;
    }
    return oss.str();
}
