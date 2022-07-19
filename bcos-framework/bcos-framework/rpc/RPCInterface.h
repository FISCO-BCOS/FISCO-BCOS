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
 * @file RPCInterface.h
 * @author: octopus
 * @date 2021-07-01
 */
#pragma once

#include "bcos-framework/multigroup/GroupInfo.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-utilities/Error.h>

namespace bcos
{
namespace rpc
{
enum AMOPNotifyMessageType : int16_t
{
    Unicast,
    Broadcast,
};
class RPCInterface
{
public:
    using Ptr = std::shared_ptr<RPCInterface>;

    virtual ~RPCInterface() {}

public:
    virtual void start() = 0;
    virtual void stop() = 0;

public:
    /**
     * @brief: notify blockNumber to rpc
     * @param _blockNumber: blockNumber
     * @param _callback: resp callback
     * @return void
     */
    virtual void asyncNotifyBlockNumber(std::string const& _groupID, std::string const& _nodeName,
        bcos::protocol::BlockNumber _blockNumber, std::function<void(Error::Ptr)> _callback) = 0;

    /// multi-group manager related interfaces
    /**
     * @brief receive the latest group information notification from the GroupManagerInterface
     *
     * @param _groupInfo the latest group information
     */
    virtual void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)>) = 0;

    /// AMOP related interfaces
    /**
     * @brief receive the amop message from the gateway
     *
     * @param _requestData the AMOP data
     * @param _callback the callback
     */
    virtual void asyncNotifyAMOPMessage(int16_t _type, std::string const& _topic,
        bytesConstRef _requestData,
        std::function<void(Error::Ptr&& _error, bytesPointer _responseData)> _callback) = 0;

    // the gateway notify the rpc to re-subscribe the topic when the gateway set-up
    virtual void asyncNotifySubscribeTopic(
        std::function<void(Error::Ptr&& _error, std::string)> _callback) = 0;
};
}  // namespace rpc
}  // namespace bcos
