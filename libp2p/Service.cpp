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
namespace dev
{
namespace p2p
{
Message::Ptr Service::sendMessageByNodeID(NodeID const& nodeID, Message::Ptr message)
{
    LOG(INFO) << "Call Service::sendMessageByNodeID";
    try
    {
        bool findNodeID = false;
        {
            RecursiveGuard l(m_host->mutexSessions());
            auto s = m_host->sessions();
            for (auto const& i : s)
            {
                if (i.first == nodeID)
                {
                    findNodeID = true;
                    break;
                }
            }
        }
        if (!findNodeID)
        {
            LOG(ERROR) << "Service::sendMessageByNodeID cannot find target.";
            return Message::Ptr();
        }

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFunc fp = std::bind(
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2);
        asyncSendMessageByNodeID(nodeID, message, fp, Options());

        callback->mutex.lock();
        callback->mutex.unlock();
        LOG(INFO) << "Service::sendMessageByNodeID mutex unlock.";

        P2PException error = callback->error;
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

    return Message::Ptr();
}

void Service::asyncSendMessageByNodeID(
    NodeID const& nodeID, Message::Ptr message, CallbackFunc callback, Options const& options)
{
    LOG(INFO) << "Call Service::asyncSendMessageByNodeID to NodeID=" << toJS(nodeID);
    try
    {
        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (i.first == nodeID)
            {
                std::shared_ptr<SessionFace> p = i.second;
                uint32_t seq = ++m_seq;
                if (callback)
                {
                    ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();
                    responseCallback->callbackFunc = callback;
                    if (options.timeout > 0)
                    {
                        std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                            std::make_shared<boost::asio::deadline_timer>(
                                *m_ioService, boost::posix_time::milliseconds(options.timeout));
                        timeoutHandler->async_wait(boost::bind(&Service::onTimeoutByNode,
                            shared_from_this(), boost::asio::placeholders::error, seq, p));
                        responseCallback->timeoutHandler = timeoutHandler;
                    }
                    p->addSeq2Callback(seq, responseCallback);
                }
                ///< update seq and length
                message->setSeq(seq);
                message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
                std::shared_ptr<bytes> buf = std::make_shared<bytes>();
                message->encode(*buf);
                p->send(buf);
                return;
            }
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << e.what();
    }
}

void Service::onTimeoutByTopic(const boost::system::error_code& error,
    std::shared_ptr<SessionFace> oriSession, NodeIDs& nodeIDsToSend, Message::Ptr message,
    CallbackFunc callback, Options const& options, uint32_t totalTimeout)
{
    LOG(INFO) << "Service::onTimeoutByTopic, error=" << error;

    try
    {
        auto it = oriSession->getCallbackBySeq(message->seq());
        ///< Both active cancellation and passive timeout need to delete callback.
        oriSession->eraseCallbackBySeq(message->seq());

        if (it != NULL)
        {
            if (error == 0)
            {
                ///< passive timeout
                if (nodeIDsToSend.size() > 0 && totalTimeout < options.timeout)
                {
                    NodeID nodeID = nodeIDsToSend[0];
                    LOG(INFO) << "Call Service::asyncSendMessageByTopic to NodeID=" << toJS(nodeID);
                    try
                    {
                        RecursiveGuard l(m_host->mutexSessions());
                        auto s = m_host->sessions();
                        for (auto const& i : s)
                        {
                            if (i.first == nodeID)
                            {
                                std::shared_ptr<SessionFace> newSession = i.second;
                                if (callback)
                                {
                                    ResponseCallback::Ptr responseCallback =
                                        std::make_shared<ResponseCallback>();
                                    responseCallback->callbackFunc = callback;

                                    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                                        std::make_shared<boost::asio::deadline_timer>(*m_ioService,
                                            boost::posix_time::milliseconds(options.subTimeout));
                                    timeoutHandler->async_wait(
                                        boost::bind(&Service::onTimeoutByTopic, shared_from_this(),
                                            boost::asio::placeholders::error, newSession,
                                            nodeIDsToSend, message, callback, options,
                                            totalTimeout + options.subTimeout));
                                    responseCallback->timeoutHandler = timeoutHandler;
                                    newSession->addSeq2Callback(message->seq(), responseCallback);
                                }

                                std::shared_ptr<bytes> msgBuffer = std::make_shared<bytes>();
                                message->encode(*msgBuffer);
                                newSession->send(msgBuffer);
                                return;
                            }
                        }
                    }
                    catch (std::exception& e)
                    {
                        LOG(ERROR) << "Service::onTimeoutByTopic error:" << e.what();
                    }
                    nodeIDsToSend.erase(nodeIDsToSend.begin());
                }
                else
                {
                    if (it->callbackFunc)
                    {
                        P2PException e(P2PExceptionType::NetworkTimeout,
                            g_P2PExceptionMsg[P2PExceptionType::NetworkTimeout]);
                        Message::Ptr msg = make_shared<Message>();
                        msg->setSeq(message->seq());
                        it->callbackFunc(e, msg);
                    }
                    else
                    {
                        LOG(ERROR) << "Service::onTimeoutByTopic callback empty.";
                    }
                }
            }
        }
        else
        {
            LOG(WARNING) << "Service::onTimeoutByNode not found seq: " << message->seq()
                         << " callback，may timeout";
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::onTimeoutByNode error:" << e.what();
    }
}

void Service::onTimeoutByNode(
    const boost::system::error_code& error, uint32_t seq, std::shared_ptr<SessionFace> p)
{
    ///< This function is called by the timer active cancellation or passive timeout.
    ///< Active cancellation or passive timeout can be distinguished by error parameter.
    ///< If the timer is active cancelled, callbackFunc will not be called.
    LOG(INFO) << "Service::onTimeoutByNode, error=" << error << ", seq=" << seq;

    try
    {
        auto it = p->getCallbackBySeq(seq);
        if (it != NULL)
        {
            if (error == 0)
            {
                ///< passive timeout
                if (it->callbackFunc)
                {
                    P2PException e(P2PExceptionType::NetworkTimeout,
                        g_P2PExceptionMsg[P2PExceptionType::NetworkTimeout]);
                    Message::Ptr message = make_shared<Message>();
                    message->setSeq(seq);
                    it->callbackFunc(e, message);
                }
                else
                {
                    LOG(ERROR) << "Service::onTimeoutByNode callback empty.";
                }
            }

            ///< Both active cancellation and passive timeout need to delete callback.
            p->eraseCallbackBySeq(seq);
        }
        else
        {
            LOG(WARNING) << "Service::onTimeoutByNode not found seq: " << seq
                         << " callback，may timeout";
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::onTimeoutByNode error:" << e.what();
    }
}

Message::Ptr Service::sendMessageByTopic(std::string const& topic, Message::Ptr message)
{
    LOG(INFO) << "Call Service::sendMessageByTopic";
    try
    {
        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFunc fp = std::bind(
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2);
        asyncSendMessageByTopic(topic, message, fp, Options());

        callback->mutex.lock();
        callback->mutex.unlock();
        LOG(INFO) << "Service::sendMessageByTopic mutex unlock.";

        P2PException error = callback->error;
        if (error.errorCode() != 0)
        {
            LOG(ERROR) << "Service::sendMessageByTopic error:" << error.errorCode() << " "
                       << error.what();
            throw error;
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::sendMessageByTopic error:" << e.what();
    }

    return Message::Ptr();
}

void Service::asyncSendMessageByTopic(
    std::string const& topic, Message::Ptr message, CallbackFunc callback, Options const& options)
{
    LOG(INFO) << "Call Service::asyncSendMessageByTopic, topic=" << topic;
    assert(options.timeout > 0 && options.subTimeout > 0);
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    if (nodeIDsToSend.size() == 0)
    {
        LOG(INFO) << "Call Service::asyncSendMessageByTopic, nodeIDsToSend size=0.";
        return;
    }
    NodeID nodeID = nodeIDsToSend[0];
    nodeIDsToSend.erase(nodeIDsToSend.begin());
    LOG(INFO) << "Call Service::asyncSendMessageByTopic to NodeID=" << toJS(nodeID);

    try
    {
        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (i.first == nodeID)
            {
                std::shared_ptr<SessionFace> p = i.second;
                uint32_t seq = ++m_seq;
                ///< update seq and length
                message->setSeq(seq);
                message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
                if (callback)
                {
                    ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();
                    responseCallback->callbackFunc = callback;
                    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                        std::make_shared<boost::asio::deadline_timer>(
                            *m_ioService, boost::posix_time::milliseconds(options.subTimeout));
                    timeoutHandler->async_wait(boost::bind(&Service::onTimeoutByTopic,
                        shared_from_this(), boost::asio::placeholders::error, p, nodeIDsToSend,
                        message, callback, options, 0));
                    responseCallback->timeoutHandler = timeoutHandler;
                    p->addSeq2Callback(seq, responseCallback);
                }
                std::shared_ptr<bytes> buf = std::make_shared<bytes>();
                message->encode(*buf);
                p->send(buf);
                return;
            }
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR:" << e.what();
    }
}

void Service::asyncMulticastMessageByTopic(std::string const& topic, Message::Ptr message)
{
    NodeIDs nodeIDsToSend = getPeersByTopic(topic);
    LOG(INFO) << "Call Service::asyncMulticastMessageByTopic nodes size=" << nodeIDsToSend.size();
    try
    {
        uint32_t seq = ++m_seq;
        message->setSeq(seq);
        message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
        std::shared_ptr<bytes> buf = std::make_shared<bytes>();
        message->encode(*buf);

        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (isSessionInNodeIDList(i.first, nodeIDsToSend))
                i.second->send(buf);
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::asyncMulticastMessageByTopic error:" << e.what();
    }
}

void Service::asyncMulticastMessageByNodeIDList(NodeIDs const& nodeIDs, Message::Ptr message)
{
    LOG(INFO) << "Call Service::asyncMulticastMessageByNodeIDList nodes size=" << nodeIDs.size();
    try
    {
        uint32_t seq = ++m_seq;
        message->setSeq(seq);
        message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
        std::shared_ptr<bytes> buf = std::make_shared<bytes>();
        message->encode(*buf);

        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            if (isSessionInNodeIDList(i.first, nodeIDs))
                i.second->send(buf);
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::asyncMulticastMessageByNodeIDList error:" << e.what();
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

void Service::asyncBroadcastMessage(Message::Ptr message, Options const& options)
{
    LOG(INFO) << "Call Service::asyncBroadcastMessage";
    try
    {
        uint32_t seq = ++m_seq;
        message->setSeq(seq);
        message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
        std::shared_ptr<bytes> buf = std::make_shared<bytes>();
        message->encode(*buf);

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

void Service::registerHandlerByProtoclID(int16_t protocolID, CallbackFuncWithSession handler)
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
            [=](P2PException e, std::shared_ptr<Session> s, Message::Ptr msg) {
                LOG(INFO) << "Session::onMessage, call callbackFunc by Topic protocolID.";

                ///< Get topic from message buffer.
                std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
                std::string topic;
                msg->decodeAMOPBuffer(buffer, topic);
                std::shared_ptr<std::vector<std::string>> topics = s->host()->topics();

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
                        LOG(ERROR) << "Session::onMessage, handler not found by topic=" << topic;
                    }
                }
                else
                {
                    LOG(ERROR) << "Session::onMessage, topic donot get this node/host attention.";
                }
            });
    }
}

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

SessionInfos Service::sessionInfos() const
{
    SessionInfos infos;
    try
    {
        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            infos.push_back(
                SessionInfo(i.first, i.second->nodeIPEndpoint(), *(i.second->topics())));
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::sessionInfos error:" << e.what();
    }
    return infos;
}

SessionInfos Service::sessionInfosByProtocolID(int16_t _protocolID) const
{
    std::pair<int8_t, uint8_t> ret = getGroupAndProtocol(_protocolID);
    h512s nodeList;
    SessionInfos infos;

    if (true == m_host->getNodeListByGroupID(int(ret.first), nodeList))
    {
        LOG(INFO) << "Service::sessionInfosByProtocolID, getNodeListByGroupID list size:"
                  << nodeList.size();
        try
        {
            RecursiveGuard l(m_host->mutexSessions());
            auto s = m_host->sessions();
            for (auto const& i : s)
            {
                if (find(nodeList.begin(), nodeList.end(), i.first) != nodeList.end())
                {
                    infos.push_back(
                        SessionInfo(i.first, i.second->nodeIPEndpoint(), *(i.second->topics())));
                }
            }
        }
        catch (std::exception& e)
        {
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
        RecursiveGuard l(m_host->mutexSessions());
        auto s = m_host->sessions();
        for (auto const& i : s)
        {
            for (auto j : *(i.second->topics()))
            {
                if (j == topic)
                {
                    nodeList.push_back(i.first);
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
