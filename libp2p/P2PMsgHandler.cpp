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
/** @file P2PMsgHandler.cpp
 *  @author chaychen
 *  @date 20180916
 */

#include "P2PMsgHandler.h"

namespace dev
{
namespace p2p
{
bool P2PMsgHandler::addSeq2Callback(uint32_t seq, ResponseCallback::Ptr const& callback)
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

ResponseCallback::Ptr P2PMsgHandler::getCallbackBySeq(uint32_t seq)
{
    RecursiveGuard l(x_seq2Callback);
    auto it = m_seq2Callback->find(seq);
    if (it != m_seq2Callback->end())
    {
        return it->second;
    }
    else
    {
        LOG(ERROR) << "Handler not found! SeqID = " << seq;
        return NULL;
    }
}

bool P2PMsgHandler::eraseCallbackBySeq(uint32_t seq)
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

bool P2PMsgHandler::addProtocolID2Handler(
    int16_t protocolID, CallbackFuncWithSession const& callback)
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

bool P2PMsgHandler::getHandlerByProtocolID(int16_t protocolID, CallbackFuncWithSession& callback)
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

bool P2PMsgHandler::eraseHandlerByProtocolID(int16_t protocolID)
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

bool P2PMsgHandler::addTopic2Handler(
    std::string const& topic, CallbackFuncWithSession const& callback)
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

bool P2PMsgHandler::getHandlerByTopic(std::string const& topic, CallbackFuncWithSession& callback)
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

bool P2PMsgHandler::eraseHandlerByTopic(std::string const& topic)
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
