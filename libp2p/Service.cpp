/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include "Service.h"

#include <libdevcore/CommonJS.h>
#include <libnetwork/Common.h>
#include <libp2p/Service.h>
#include <boost/random.hpp>
#include <libdevcore/easylog.h>
#include <libnetwork/Host.h>

namespace dev
{
namespace p2p
{

const uint32_t CHECK_INTERVEL = 5000;

void Service::start() {
    if(!m_run) {
        m_run = true;
        m_host->start();

        heartBeat();
    }
}

void Service::stop() {
    if(m_run) {
        m_run = false;
        m_host->stop();
    }
}

void Service::heartBeat() {
    if(!m_run) {
        return;
    }

    std::map<NodeIPEndpoint, NodeID> staticNodes;
    std::unordered_map<NodeID, P2PSession::Ptr> sessions;

    {
        RecursiveGuard l(x_sessions);
        sessions = m_sessions;
        staticNodes = m_staticNodes;
    }

    //Reconnect all nodes
    for(auto it: staticNodes) {
        if(it.second != NodeID()) {
            auto its = sessions.find(it.second);
            if(its != sessions.end() && its->second && its->second->session()->isConnected()) {
                continue;
            }
        }

        m_host->asyncConnect(it.first, std::bind(&Service::onConnect, shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    auto self = shared_from_this();
    auto timer = m_host->asioInterface()->newTimer(CHECK_INTERVEL);
    timer->async_wait([self](const boost::system::error_code& error) {
        if(error) {
            LOG(TRACE) << "timer canceled" << error;
            return;
        }

        self->heartBeat();
    });
}

void Service::onConnect(NetworkException e, NodeID nodeID, std::shared_ptr<SessionFace> session) {
    if(e.errorCode()) {
        LOG(ERROR) << "Connect error: " << boost::diagnostic_information(e);

        return;
    }

    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);
    if(it != m_sessions.end() && it->second->session()->isConnected()) {
        //Connected session, disconnect
        session->disconnect(DuplicatePeer);
        return;
    }

    auto p2pSession = std::make_shared<P2PSession>();
    p2pSession->setSession(session);
    p2pSession->setNodeID(nodeID);
    p2pSession->session()->setMessageHandler(std::bind(&Service::onMessage, shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, p2pSession));

    if(it != m_sessions.end()) {
        it->second = p2pSession;
    }
    else {
        m_sessions.insert(std::make_pair(nodeID, p2pSession));
    }

    p2pSession->session()->start();
}

void Service::onDisconnect(NetworkException e, NodeID nodeID) {
    {
        RecursiveGuard l(x_sessions);
        m_sessions.erase(nodeID);
    }

    {
        RecursiveGuard l(x_nodes);
        for(auto it: m_staticNodes) {
            if(it.second == nodeID) {
                it.second = NodeID();
                break;
            }
        }
    }
}

void Service::onMessage(NetworkException e, SessionFace::Ptr session, Message::Ptr message, P2PSession::Ptr p2pSession) {
    try {
        if(e.errorCode()) {
            LOG(ERROR) << "P2PSession error, disconnect: " << boost::diagnostic_information(e);

            p2pSession->stop();
            onDisconnect(e, p2pSession->nodeID());

            return;
        }

        auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
        if(p2pMessage->isRequestPacket()) {
            CallbackFuncWithSession callback;
            {
                RecursiveGuard lock(x_protocolID2Handler);
                auto it = m_protocolID2Handler->find(p2pMessage->protocolID());
                if(it != m_protocolID2Handler->end()) {
                    callback = it->second;
                }
            }

            if(callback) {
                m_host->threadPool()->enqueue([callback, p2pSession, p2pMessage, e]() {
                    callback(e, p2pSession, p2pMessage);
                });
            }
        }
        else {
            LOG(WARNING) << "Not found seq: " << p2pMessage->seq() << " response, may be timeout";
        }
    }
    catch(std::exception &e) {
        LOG(ERROR) << "onMessage error: " << boost::diagnostic_information(e);
    }
}

P2PMessage::Ptr Service::sendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message)
{
    P2PMSG_LOG(DEBUG) << "[#sendMessageByNodeID] [nodeID]: " << toJS(nodeID);
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            typedef std::shared_ptr<SessionCallback> Ptr;

            SessionCallback() { mutex.lock(); }

            void onResponse(NetworkException _error, std::shared_ptr<P2PSession> session, P2PMessage::Ptr _message)
            {
                error = _error;
                response = _message;
                mutex.unlock();
            }

            NetworkException error;
            P2PMessage::Ptr response;
            std::mutex mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFuncWithSession fp = std::bind(
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        asyncSendMessageByNodeID(nodeID, message, fp, Options());

        callback->mutex.lock();
        callback->mutex.unlock();
        P2PMSG_LOG(DEBUG) << "[#sendMessageByNodeID] mutex unlock.";

        NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            LOG(ERROR) << "asyncSendMessageByNodeID error:" << error.errorCode() << " "
                       << error.what();
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << boost::diagnostic_information(e);
        BOOST_THROW_EXCEPTION(e);
    }

    return P2PMessage::Ptr();
}

void Service::asyncSendMessageByNodeID(
    NodeID nodeID, P2PMessage::Ptr message, CallbackFuncWithSession callback, Options options)
{
    P2PMSG_LOG(DEBUG) << "[#asyncSendMessageByNodeID] [nodeID]: " << toJS(nodeID);
    try
    {
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(nodeID);

        if(it != m_sessions.end() && it->second->session()->isConnected()) {
            message->setLength(P2PMessage::HEADER_LENGTH + message->buffer()->size());

            auto session = it->second;
            session->session()->asyncSendMessage(message, options, [session, callback](NetworkException e, Message::Ptr message) {
                P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
                if(callback) {
                    callback(e, session, p2pMessage);
                }
            });
        }
        else {
            BOOST_THROW_EXCEPTION(NetworkException(Disconnect, g_P2PExceptionMsg[Disconnect]));
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << boost::diagnostic_information(e);

        m_host->threadPool()->enqueue([callback] {
            callback(NetworkException(), P2PSession::Ptr(), P2PMessage::Ptr());
        });
    }
}

P2PMessage::Ptr Service::sendMessageByTopic(std::string topic, P2PMessage::Ptr message)
{
    LOG(INFO) << "Call Service::sendMessageByTopic";
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            typedef std::shared_ptr<SessionCallback> Ptr;

            SessionCallback() { mutex.lock(); }

            void onResponse(NetworkException _error, std::shared_ptr<P2PSession> session, P2PMessage::Ptr _message)
            {
                error = _error;
                response = _message;
                mutex.unlock();
            }

            NetworkException error;
            P2PMessage::Ptr response;
            std::mutex mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFuncWithSession fp = std::bind(
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        asyncSendMessageByTopic(topic, message, fp, Options());

        callback->mutex.lock();
        callback->mutex.unlock();
        LOG(INFO) << "Service::sendMessageByNodeID mutex unlock.";

        NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            LOG(ERROR) << "asyncSendMessageByNodeID error:" << error.errorCode() << " "
                       << error.what();
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << boost::diagnostic_information(e);
        BOOST_THROW_EXCEPTION(e);
    }

    return P2PMessage::Ptr();
}

void Service::asyncSendMessageByTopic(
    std::string topic, P2PMessage::Ptr message, CallbackFuncWithSession callback, Options options)
{
    LOG(INFO) << "Call Service::asyncSendMessageByTopic, topic=" << topic;
    //assert(options.timeout > 0 && options.subTimeout > 0);
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    if (nodeIDsToSend.size() == 0)
    {
        P2PMSG_LOG(DEBUG) << "[#asyncSendMessageByTopic] no nodeID to be Sent.";
        return;
    }

    class TopicStatus: public std::enable_shared_from_this<TopicStatus> {
    public:
        void onResponse(NetworkException e, P2PSession::Ptr session, P2PMessage::Ptr msg) {
            if(e.errorCode() != 0 || !m_current) {
                if(e.errorCode() != 0) {
                    LOG(WARNING) << "Send topics message to " << m_current << "error once: " << e.what();
                }

                if(m_nodeIDs.empty()) {
                    LOG(WARNING) << "Send topics message all failed";
                    m_callback(NetworkException(), session, P2PMessage::Ptr());

                    return;
                }

                boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
                boost::uniform_int<int> index(0, m_nodeIDs.size() - 1);

                auto ri = index(rng);

                m_current = m_nodeIDs[ri];
                m_nodeIDs.erase(m_nodeIDs.begin() + ri);

                auto s = m_service.lock();
                if(s) {
                    auto self = shared_from_this();

                    //auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
                    s->asyncSendMessageByNodeID(m_current, msg, std::bind(&TopicStatus::onResponse, shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), m_options);
                }
            }
            else {
                m_callback(e, session, msg);
            }
        }

        NodeID m_current;
        NodeIDs m_nodeIDs;
        CallbackFuncWithSession m_callback;
        P2PMessage::Ptr m_message;
        std::weak_ptr<Service> m_service;
        dev::p2p::Options m_options;
    };

    auto topicStatus = std::make_shared<TopicStatus>();
    topicStatus->m_nodeIDs = nodeIDsToSend;
    topicStatus->m_callback = callback;
    topicStatus->m_message = message;
    topicStatus->m_service = std::weak_ptr<Service>(shared_from_this());
    topicStatus->m_options = options;

    topicStatus->onResponse(NetworkException(), P2PSession::Ptr(), message);
}

void Service::asyncMulticastMessageByTopic(std::string topic, P2PMessage::Ptr message)
{
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    P2PMSG_LOG(DEBUG) << "[#asyncMulticastMessageByTopic] [node size]: " << nodeIDsToSend.size();
    try
    {
        for (auto nodeID : nodeIDsToSend)
        {
            asyncSendMessageByNodeID(nodeID, message, CallbackFuncWithSession(), Options());
        }
    }
    catch (std::exception& e)
    {
        P2PMSG_LOG(WARNING) << "[#asyncMulticastMessageByTopic] [EINFO]: " << e.what();
    }
}

void Service::asyncMulticastMessageByNodeIDList(NodeIDs nodeIDs, P2PMessage::Ptr message)
{
    LOG(INFO) << "Call Service::asyncMulticastMessageByNodeIDList nodes size=" << nodeIDs.size();
    try {
        for (auto nodeID: nodeIDs)
        {
            asyncSendMessageByNodeID(nodeID, message, CallbackFuncWithSession(), Options());
        }
    }
    catch (std::exception& e)
    {
        P2PMSG_LOG(WARNING) << "[#asyncMulticastMessageByNodeIDList] [EINFO]: " << e.what();
    }
}

void Service::asyncBroadcastMessage(P2PMessage::Ptr message, Options options)
{
    P2PMSG_LOG(DEBUG) << "[#asyncMulticastMessageByNodeIDList]";
    try
    {
        std::unordered_map<NodeID, P2PSession::Ptr> sessions;
        {
            RecursiveGuard l(x_sessions);
            sessions = m_sessions;
        }

        for (auto s: sessions)
       {
           asyncSendMessageByNodeID(s.first, message, CallbackFuncWithSession(), Options());
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
    if (it == m_protocolID2Handler->end()) {
        m_protocolID2Handler->insert(std::make_pair(protocolID, handler));
    }
    else {
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

SessionInfos Service::sessionInfos()
{
    SessionInfos infos;
    try
    {
        RecursiveGuard l(x_sessions);
        auto s = m_sessions;
        for (auto const& i : s)
        {
            infos.push_back( SessionInfo(i.first, i.second->session()->nodeIPEndpoint(), *(i.second->topics())) );
        }
    }
    catch (std::exception& e)
    {
        P2PMSG_LOG(WARNING) << "[#sessionInfos] [EINFO]: " << e.what();
    }
    return infos;
}

SessionInfos Service::sessionInfosByProtocolID(PROTOCOL_ID _protocolID)
{
    std::pair<GROUP_ID, MODULE_ID> ret = getGroupAndProtocol(_protocolID);
    h512s nodeList;
    SessionInfos infos;

    auto it = m_groupID2NodeList.find(int(ret.first));
    if(it != m_groupID2NodeList.end()) {
        try {
            RecursiveGuard l(x_sessions);
            auto s = m_sessions;
            for (auto const& i : s)
            {
                if (find(nodeList.begin(), nodeList.end(), i.first) != nodeList.end())
                {
                    infos.push_back(
                        SessionInfo(i.first, i.second->session()->nodeIPEndpoint(), *(i.second->topics())));
                }
            }
        }
        catch (std::exception &e) {
            LOG(ERROR) << "Service::sessionInfosByProtocolID error:" << e.what();
        }
    }

    P2PMSG_LOG(DEBUG) << "[#sessionInfosByProtocolID] return: [list size]: " << infos.size();
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

bool Service::isConnected(NodeID nodeID) {
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);

    if(it == m_sessions.end()) {
        return false;
    }

    if(!it->second->running()) {
        return false;
    }

    return true;
}

}  // namespace p2p

}  // namespace dev
