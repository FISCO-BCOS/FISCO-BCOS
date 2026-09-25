/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file ServiceRouter.h
 * @brief Internal header: the optional router (RIP) module state of Service, merged from the
 *        former ServiceV2 subclass. Only Service.cpp / ServiceRouter.cpp include it, so the
 *        public Service.h stays free of router/timer headers.
 */
#pragma once
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/libp2p/router/RouterTableInterface.h"
#include "bcos-utilities/Timer.h"

namespace bcos::gateway
{
struct Service::RouterState
{
    // for message forward
    // Note: must use ptr here, for the timer uses enable_shared_from_this
    std::shared_ptr<bcos::Timer> routerTimer;
    std::atomic<uint32_t> statusSeq{1};
    // FIB-186 (vector B): coalesce router-seq broadcasts. The first membership/route change of a
    // burst broadcasts immediately (leading edge) and sets this flag; further changes within the
    // window only advance statusSeq, and the routerTimer flush resets the flag. This bounds the
    // broadcast fan-out so connection churn cannot cascade a full-mesh gossip storm on the PBFT
    // delivery pool. See Service::markRouterSeqChanged().
    std::atomic_bool routerSeqDirty{false};

    RouterTableFactory::Ptr routerTableFactory;
    RouterTableInterface::Ptr routerTable;

    std::map<std::string, uint32_t> node2Seq;
    mutable SharedMutex x_node2Seq;

    // rawP2pID->p2pID
    std::map<std::string, std::string> rawP2pIDInfo;
    mutable SharedMutex x_rawP2pIDInfo;
    // p2pID->rawP2pID
    std::map<std::string, std::string> p2pIDInfo;
    mutable SharedMutex x_p2pIDInfo;

    const int unreachableDistance = 10;

    // called when the given node unreachable
    std::vector<std::function<void(std::string)>> unreachableHandlers;
    mutable SharedMutex x_unreachableHandlers;
};

// Template definition (declared in Service.h): lives here because the body touches the
// RouterState pimpl above, which the public Service.h deliberately does not complete.
template <::ranges::input_range Payloads>
    requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
task::Task<std::optional<Message>> Service::forwardMessageByNodeID(
    P2pID nodeID, Message& header, Payloads payloads, Options options)
{
    // Forwarding path (a message received from another node being relayed): unlike
    // sendMessageByNodeID it must NOT rewrite srcP2PNodeID — the original sender is preserved so
    // the final destination can reply directly to it. Only the next hop is resolved here.
    auto dstNodeID = header.dstP2PNodeID();
    // without nextHop: maybe network unreachable or with distance equal to 1
    auto nextHop = m_router->routerTable->getNextHop(dstNodeID);
    if (nextHop.empty())
    {
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            SERVICE2_LOG(TRACE) << LOG_BADGE("forwardMessageByNodeID")
                                << LOG_DESC("sendMessage to dstNode")
                                << LOG_KV("from", header.printSrcP2PNodeID())
                                << LOG_KV("to", header.printDstP2PNodeID())
                                << LOG_KV("type", header.packetType())
                                << LOG_KV("seq", header.seq())
                                << LOG_KV("rsp", header.isRespPacket());
        }
        co_return co_await directSendMessageByNodeID(
            std::move(dstNodeID), header, std::move(payloads), options);
    }
    // with nextHop, send the message to nextHop
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        SERVICE2_LOG(TRACE) << LOG_BADGE("forwardMessageByNodeID")
                            << LOG_DESC("forwardMessage to nextHop")
                            << LOG_KV("from", header.printSrcP2PNodeID())
                            << LOG_KV("to", header.printDstP2PNodeID())
                            << LOG_KV("nextHop", printShortP2pID(nextHop))
                            << LOG_KV("type", header.packetType()) << LOG_KV("seq", header.seq())
                            << LOG_KV("rsp", header.isRespPacket());
    }
    co_return co_await directSendMessageByNodeID(
        std::move(nextHop), header, std::move(payloads), options);
}
}  // namespace bcos::gateway
