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

#pragma once
#include "Common.h"
#include "Host.h"
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FixedHash.h>
#include <memory>

namespace dev
{
namespace p2p
{
class P2PMsgHandler
{
public:
    P2PMsgHandler()
    {
        m_seq2Callback = std::make_shared<std::unordered_map<uint32_t, ResponseCallback::Ptr>>();
        m_protocolID2Handler =
            std::make_shared<std::unordered_map<uint32_t, CallbackFuncWithSession>>();
        m_topic2Handler =
            std::make_shared<std::unordered_map<std::string, CallbackFuncWithSession>>();
    }

    ///< Add, Get, Erase interface of seq2Callback.
    ///< The return true indicates that the operation was successful.
    ///< The return false indicates that the operation failed.
    bool addSeq2Callback(uint32_t seq, ResponseCallback::Ptr const& callback);
    ResponseCallback::Ptr getCallbackBySeq(uint32_t seq);
    bool eraseCallbackBySeq(uint32_t seq);

    ///< Add, Get, Erase interface of protocolID2Handler.
    ///< The return true indicates that the operation was successful.
    ///< The return false indicates that the operation failed.
    bool addProtocolID2Handler(int16_t protocolID, CallbackFuncWithSession const& callback);
    bool getHandlerByProtocolID(int16_t protocolID, CallbackFuncWithSession& callback);
    bool eraseHandlerByProtocolID(int16_t protocolID);

    ///< Add, Get, Erase interface of topic2Handler.
    ///< The return true indicates that the operation was successful.
    ///< The return false indicates that the operation failed.
    bool addTopic2Handler(std::string const& topic, CallbackFuncWithSession const& callback);
    bool getHandlerByTopic(std::string const& topic, CallbackFuncWithSession& callback);
    bool eraseHandlerByTopic(std::string const& topic);

private:
    ///< A call B, the function to call after the response is received by A.
    mutable RecursiveMutex x_seq2Callback;
    std::shared_ptr<std::unordered_map<uint32_t, ResponseCallback::Ptr>> m_seq2Callback;

    ///< A call B, the function to call after the request is received by B.
    mutable RecursiveMutex x_protocolID2Handler;
    std::shared_ptr<std::unordered_map<uint32_t, CallbackFuncWithSession>> m_protocolID2Handler;

    ///< A call B, the function to call after the request is received by B in topic.
    mutable RecursiveMutex x_topic2Handler;
    std::shared_ptr<std::unordered_map<std::string, CallbackFuncWithSession>> m_topic2Handler;
};

}  // namespace p2p

}  // namespace dev
