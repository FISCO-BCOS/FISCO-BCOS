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
        message->setProtocolID(g_synchronousPackageProtocolID);
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

        callback->response->Print("Service::sendMessageByNodeID msg returned:");

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
        std::unordered_map<NodeID, std::shared_ptr<SessionFace>> s = m_host->sessions();
        for (auto const& i : s)
        {
            LOG(INFO) << "Service::asyncSendMessageByNodeID traversed nodeID = " << toJS(i.first);
            if (i.first == nodeID)
            {
                std::shared_ptr<SessionFace> p = i.second;
                LOG(INFO) << "Service::asyncSendMessageByNodeID get session success.";
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
                        timeoutHandler->async_wait(boost::bind(&Service::onTimeout,
                            shared_from_this(), boost::asio::placeholders::error, seq));
                        responseCallback->timeoutHandler = timeoutHandler;
                    }
                    m_p2pMsgHandler->addSeq2Callback(seq, responseCallback);
                }
                ///< update seq and length
                message->setSeq(seq);
                message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
                message->Print("Service::asyncSendMessageByNodeID msg sent:");
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

void Service::asyncSendMessageByNodeID(NodeID const& nodeID, Message::Ptr message)
{
    LOG(INFO) << "Call Service::asyncSendMessageByNodeID without callback to NodeID="
              << toJS(nodeID);
    try
    {
        RecursiveGuard l(m_host->mutexSessions());
        std::unordered_map<NodeID, std::shared_ptr<SessionFace>> s = m_host->sessions();
        for (auto const& i : s)
        {
            LOG(INFO) << "Service::asyncSendMessageByNodeID without callback traversed nodeID = "
                      << toJS(i.first);
            if (i.first == nodeID)
            {
                std::shared_ptr<SessionFace> p = i.second;
                LOG(INFO)
                    << "Service::asyncSendMessageByNodeID without callback get session success.";
                uint32_t seq = ++m_seq;

                ///< insert a empty code callback to seq map
                CallbackFunc callback = [](P2PException e, Message::Ptr msg) {};
                ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();
                responseCallback->callbackFunc = callback;
                m_p2pMsgHandler->addSeq2Callback(seq, responseCallback);

                ///< update seq and length
                message->setSeq(seq);
                message->setLength(Message::HEADER_LENGTH + message->buffer()->size());
                message->Print("Service::asyncSendMessageByNodeID without callback msg sent:");
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

void Service::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    ///< This function is called by the timer active cancellation or passive timeout.
    ///< Active cancellation or passive timeout can be distinguished by error parameter.
    ///< If the timer is active cancelled, callbackFunc will not be called.
    LOG(INFO) << "Service::onTimeout, error=" << error << ", seq=" << seq;

    try
    {
        auto it = m_p2pMsgHandler->getCallbackBySeq(seq);
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
                    LOG(ERROR) << "Service::onTimeout callback empty.";
                }
            }

            ///< Both active cancellation and passive timeout need to delete callback.
            m_p2pMsgHandler->eraseCallbackBySeq(seq);
        }
        else
        {
            LOG(WARNING) << "Service::onTimeout not found seq: " << seq << " callback，may timeout";
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Service::onTimeout error:" << e.what();
    }
}

Message::Ptr Service::sendMessageByTopic(std::string const& topic, Message::Ptr message)
{
    return Message::Ptr();
}

void Service::asyncSendMessageByTopic(
    std::string const& topic, Message::Ptr message, CallbackFunc callback, Options const& options)
{}

void Service::asyncMulticastMessageByTopic(std::string const& topic, Message::Ptr message) {}

void Service::asyncBroadcastMessage(Message::Ptr message, Options const& options) {}

void Service::registerHandlerByProtoclID(int16_t protocolID, CallbackFuncWithSession handler)
{
    m_p2pMsgHandler->addProtocolID2Handler(protocolID, handler);
}

void Service::registerHandlerByTopic(std::string const& topic, CallbackFuncWithSession handler)
{
    m_p2pMsgHandler->addTopic2Handler(topic, handler);
}

}  // namespace p2p

}  // namespace dev
