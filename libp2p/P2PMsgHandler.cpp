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
/** @file P2PMsgHandler.h
 *  @author chaychen
 *  @date 20180916
 */

#include "P2PMsgHandler.h"

namespace dev
{
namespace p2p
{
bool P2PMsgHandler::addSeq2Callback(
    uint32_t seq, std::function<void(dev::Exception, Message::Ptr)> callback)
{
    RecursiveGuard l(x_seq2Callback);
    if (m_seq2Callback->find(seq) == m_seq2Callback->end())
    {
        m_seq2Callback->insert(std::make_pair(seq, callback));
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler repetition! SeqID = " << seq;
        return false;
    }
}

bool P2PMsgHandler::getSeq2Callback(
    uint32_t seq, std::function<void(dev::Exception, Message::Ptr)>& callback)
{
    RecursiveGuard l(x_seq2Callback);
    auto it = m_seq2Callback->find(seq);
    if (it != m_seq2Callback->end())
    {
        callback = it->second;
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! SeqID = " << seq;
        return false;
    }
}

bool P2PMsgHandler::eraseSeq2Callback(uint32_t seq)
{
    RecursiveGuard l(x_seq2Callback);
    if (m_seq2Callback->find(seq) != m_seq2Callback->end())
    {
        m_seq2Callback->erase(seq);
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! SeqID = " << seq;
        return false;
    }
}

bool P2PMsgHandler::addProtocolID2Handler(uint32_t protocolID,
    std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)> callback)
{
    RecursiveGuard l(x_protocolID2Handler);
    if (m_protocolID2Handler->find(protocolID) == m_protocolID2Handler->end())
    {
        m_protocolID2Handler->insert(std::make_pair(protocolID, callback));
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler repetition! ProtocolID = " << protocolID;
        return false;
    }
}

bool P2PMsgHandler::getProtocolID2Handler(uint32_t protocolID,
    std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)>& callback)
{
    RecursiveGuard l(x_protocolID2Handler);
    auto it = m_protocolID2Handler->find(protocolID);
    if (it != m_protocolID2Handler->end())
    {
        callback = it->second;
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! ProtocolID = " << protocolID;
        return false;
    }
}

bool P2PMsgHandler::eraseProtocolID2Handler(uint32_t protocolID)
{
    RecursiveGuard l(x_protocolID2Handler);
    if (m_protocolID2Handler->find(protocolID) != m_protocolID2Handler->end())
    {
        m_protocolID2Handler->erase(protocolID);
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! ProtocolID = " << protocolID;
        return false;
    }
}

bool P2PMsgHandler::addTopic2Handler(std::string const& topic,
    std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)> callback)
{
    RecursiveGuard l(x_topic2Handler);
    if (m_topic2Handler->find(topic) == m_topic2Handler->end())
    {
        m_topic2Handler->insert(std::make_pair(topic, callback));
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler repetition! Topic = " << topic;
        return false;
    }
}

bool P2PMsgHandler::getTopic2Handler(std::string const& topic,
    std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)>& callback)
{
    RecursiveGuard l(x_topic2Handler);
    auto it = m_topic2Handler->find(topic);
    if (it != m_topic2Handler->end())
    {
        callback = it->second;
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! Topic = " << topic;
        return false;
    }
}

bool P2PMsgHandler::eraseTopic2Handler(std::string const& topic)
{
    RecursiveGuard l(x_topic2Handler);
    if (m_topic2Handler->find(topic) != m_topic2Handler->end())
    {
        m_topic2Handler->erase(topic);
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! Topic = " << topic;
        return false;
    }
}

}  // namespace p2p

}  // namespace dev
