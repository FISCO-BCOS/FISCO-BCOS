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

namespace dev
{
namespace p2p
{
P2PMessage::Ptr Service::sendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message)
{
    LOG(INFO) << "Call Service::sendMessageByNodeID";
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
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2);
        asyncSendMessageByNodeID(nodeID, message, fp, Options());

        callback->mutex.lock();
        callback->mutex.unlock();
        LOG(INFO) << "Service::sendMessageByNodeID mutex unlock.";

        NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            LOG(ERROR) << "asyncSendMessageByNodeID error:" << error.errorCode() << " "
                       << error.what();
            throw error;
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << e.what();
    }

    return P2PMessage::Ptr();
}

void Service::asyncSendMessageByNodeID(
    NodeID nodeID, P2PMessage::Ptr message, CallbackFuncWithSession callback, Options options)
{
    LOG(INFO) << "Call Service::asyncSendMessageByNodeID to NodeID=" << toJS(nodeID);
    try
    {
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(nodeID);

        if(it != m_sessions.end() && it->second->session()->isConnected()) {
            std::shared_ptr<SessionFace> p = it->second;
            uint32_t seq = ++m_seq;

#if 0
            if (m_callback)
            {
                ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();
                responseCallback->callbackFunc = m_callback;
                if (m_options.timeout > 0)
                {
                    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                        std::make_shared<boost::asio::deadline_timer>(
                            *m_host->ioService(), boost::posix_time::milliseconds(m_options.timeout));
                    timeoutHandler->async_wait(boost::bind(&Service::onTimeoutByNode,
                        shared_from_this(), boost::asio::placeholders::error, seq, p));
                    responseCallback->timeoutHandler = timeoutHandler;
                }
                //p->addSeq2Callback(seq, responseCallback);
            }
#endif

            ///< update seq and length
            message->setSeq(seq);

            auto session = it->second;
            it->second->session()->asyncSendMessage(message, options, [session, callback](NetworkException e, Message::Ptr message) {
                P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
                if(callback) {
                    callback(e, session, p2pMessage);
                }
            });

            message->setLength(P2PMessage::HEADER_LENGTH + message->buffer()->size());
        }
        else {
            //TODO: call callback, but must execute in threadpool
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << e.what();
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
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2);
        asyncSendMessageByTopic(topic, message, fp, Options());

        callback->mutex.lock();
        callback->mutex.unlock();
        LOG(INFO) << "Service::sendMessageByNodeID mutex unlock.";

        NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            LOG(ERROR) << "asyncSendMessageByNodeID error:" << error.errorCode() << " "
                       << error.what();
            throw error;
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::sendMessageByTopic error:" << e.what();
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
        LOG(INFO) << "Call Service::asyncSendMessageByTopic, nodeIDsToSend size=0.";
        return;
    }

    class TopicStatus: public std::enable_shared_from_this<TopicStatus> {
    public:
        void onResponse(NetworkException e, P2PSession::Ptr session, Message::Ptr message) {
            if(e.errorCode() != 0 || !m_current) {
                if(e.errorCode() != 0) {
                    LOG(WARNING) << "Send topics message to " << m_current << "error once: " << e.what();
                }

                if(m_nodeIDs.empty()) {
                    LOG(WARNING) << "Send topics message all failed";
                    m_callback(NetworkException(), Message::Ptr());

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

                    s->asyncSendMessageByNodeID(m_current, message, std::bind(&TopicStatus::onResponse, shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), m_options);
#if 0
                    s->asyncSendMessageByNodeID(m_current, m_message, [self](NetworkException e, P2PSession::Ptr session, P2PMessage::Ptr m_message) {
                        self->onResponse(e, session, m_message);
                    }, m_options);
#endif
                }
            }
            else {
                m_callback(e, message);
            }
        }

        NodeID m_current;
        NodeIDs m_nodeIDs;
        CallbackFunc m_callback;
        P2PMessage::Ptr m_message;
        std::weak_ptr<Service> m_service;
        dev::p2p::Options m_options;
    };

    auto topicStatus = std::make_shared<TopicStatus>();
    topicStatus->m_nodeIDs = nodeIDsToSend;
    topicStatus->m_callback = callback;
    topicStatus->m_message = message;
    topicStatus->m_service = std::weak_ptr(shared_from_this());
    topicStatus->m_options = options;

    topicStatus->onResponse(NetworkException(), P2PSession::Ptr(), message);
}

#if 0
void Service::asyncMulticastMessageByTopic(std::string const& topic, P2PMessage::Ptr m_message)
{
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    LOG(INFO) << "Call Service::asyncMulticastMessageByTopic nodes size=" << nodeIDsToSend.size();
    try
    {
        uint32_t seq = ++m_seq;
        m_message->setSeq(seq);
        m_message->setLength(P2PMessage::HEADER_LENGTH + m_message->buffer()->size());
        std::shared_ptr<bytes> buf = std::make_shared<bytes>();
        m_message->encode(*buf);

        RecursiveGuard l(x_sessions);
        auto s = m_sessions;
        for (auto const& i : s)
        {
            if (isSessionInNodeIDList(i.first, nodeIDsToSend))
                //i.second->send(buf);
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::asyncMulticastMessageByTopic error:" << e.what();
    }
}

void Service::asyncMulticastMessageByNodeIDList(NodeIDs const& m_nodeIDs, P2PMessage::Ptr m_message)
{
    LOG(INFO) << "Call Service::asyncMulticastMessageByNodeIDList nodes size=" << m_nodeIDs.size();
    try
    {
        uint32_t seq = ++m_seq;
        m_message->setSeq(seq);
        m_message->setLength(P2PMessage::HEADER_LENGTH + m_message->buffer()->size());
        std::shared_ptr<bytes> buf = std::make_shared<bytes>();
        m_message->encode(*buf);

        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (isSessionInNodeIDList(i.first, m_nodeIDs))
                i.second->send(buf);
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::asyncMulticastMessageByNodeIDList error:" << e.what();
    }
}
#endif

bool Service::isSessionInNodeIDList(NodeID const& targetNodeID, NodeIDs const& nodeIDs)
{
    for (auto const& nodeID : nodeIDs)
    {
        if (targetNodeID == nodeID)
            return true;
    }
    return false;
}

#if 0
void Service::asyncBroadcastMessage(P2PMessage::Ptr m_message, Options const& m_options)
{
    LOG(INFO) << "Call Service::asyncBroadcastMessage";
    try
    {
        uint32_t seq = ++m_seq;
        m_message->setSeq(seq);
        m_message->setLength(P2PMessage::HEADER_LENGTH + m_message->buffer()->size());
        std::shared_ptr<bytes> buf = std::make_shared<bytes>();
        m_message->encode(*buf);

        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            i.second->send(buf);
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::asyncBroadcastMessage error:" << e.what();
    }
}
#endif

void Service::registerHandlerByProtoclID(PROTOCOL_ID protocolID, CallbackFuncWithSession handler)
{
    m_p2pMsgHandler->addProtocolID2Handler(protocolID, handler);
}

void Service::registerHandlerByTopic(std::string const& topic, CallbackFuncWithSession handler)
{
    m_p2pMsgHandler->addTopic2Handler(topic, handler);

    ///< Register handler by Topic protocolID only once.
    CallbackFuncWithSession callback;
    if (false == m_p2pMsgHandler->getHandlerByProtocolID(dev::eth::ProtocolID::Topic, callback))
    {
        registerHandlerByProtoclID(dev::eth::ProtocolID::Topic,
            [=](NetworkException e, std::shared_ptr<P2PSession> s, P2PMessage::Ptr msg) {
                LOG(INFO) << "Session::onMessage, call callbackFunc by Topic protocolID.";
                //if (msg->buffer()->size() <= Message::MAX_LENGTH)
                if(true)
                {
                    ///< Get topic from message buffer.
                    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
                    std::string topic;
                    msg->decodeAMOPBuffer(buffer, topic);
                    std::shared_ptr<std::vector<std::string>> topics = s->topics();

                    ///< This above topic get this node/host attention or not.
                    bool bFind = false;
                    for (size_t i = 0; i < topics->size(); i++)
                    {
                        if (topic == (*topics)[i])
                        {
                            bFind = true;
                            break;
                        }
                    }

                    if (bFind)
                    {
                        CallbackFuncWithSession callbackFunc;

                        bool ret = m_p2pMsgHandler->getHandlerByTopic(topic, callbackFunc);
                        if (ret && callbackFunc)
                        {
                            LOG(INFO) << "Session::onMessage, call callbackFunc by topic=" << topic;
                            ///< execute funtion, send response packet by user in callbackFunc
                            ///< TODO: use threadPool
                            callbackFunc(e, s, msg);
                        }
                        else
                        {
                            LOG(ERROR)
                                << "Session::onMessage, handler not found by topic=" << topic;
                        }
                    }
                    else
                    {
                        LOG(ERROR)
                            << "Session::onMessage, topic donot get this node/host attention.";
                    }
                }
                else
                {
                    LOG(ERROR) << "Session::onMessage, Packet too large, size:"
                               << msg->buffer()->size();
                }
            });
    }
}

#if 0
void Service::setTopicsByNode(
    NodeID const& _nodeID, std::shared_ptr<std::vector<std::string>> _topics)
{
    LOG(INFO) << "Call Service::setTopicsByNode to nodeID=" << toJS(_nodeID);
    try
    {
        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (i.first == _nodeID)
            {
                i.second->setTopics(_topics);
                LOG(INFO) << "Service::setTopicsByNode completed, topics size="
                          << getTopicsByNode(_nodeID)->size();
                break;
            }
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::setTopicsByNode error:" << e.what();
    }
}

std::shared_ptr<std::vector<std::string>> Service::getTopicsByNode(NodeID const& _nodeID)
{
    LOG(INFO) << "Call Service::getTopicsByNode by nodeID=" << toJS(_nodeID);
    std::shared_ptr<std::vector<std::string>> ret = make_shared<std::vector<std::string>>();
    try
    {
        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (i.first == _nodeID)
            {
                ret = i.second->topics();
                LOG(INFO) << "Service::getTopicsByNode success, topics size=" << ret->size();
                break;
            }
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::getTopicsByNode error:" << e.what();
    }

    return ret;
}
#endif

SessionInfos Service::sessionInfos() const
{
    SessionInfos infos;
    try
    {
        RecursiveGuard l(x_sessions);
        auto s = m_sessions;
        for (auto const& i : s)
        {
            infos.push_back(
                SessionInfo(i.first, i.second->session()->nodeIPEndpoint(), *(i.second->topics())));
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::sessionInfos error:" << e.what();
    }
    return infos;
}

SessionInfos Service::sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const
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

    LOG(INFO) << "Service::sessionInfosByProtocolID, return list size:" << infos.size();
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
        LOG(ERROR) << "Service::getPeersByTopic error:" << e.what();
    }
    LOG(INFO) << "Service::getPeersByTopic topic=" << topic << ", peers size=" << nodeList.size();
    return nodeList;
}

}  // namespace p2p

}  // namespace dev
