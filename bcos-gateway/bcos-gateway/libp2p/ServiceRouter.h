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
}  // namespace bcos::gateway
