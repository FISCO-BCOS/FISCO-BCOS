/**
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
 * @file RPCServiceClient.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#include <bcos-framework/rpc/RPCInterface.h>
#include <bcos-tars-protocol/tars/RpcService.h>
#include <bcos-utilities/Common.h>
#include <servant/Application.h>
#include <boost/core/ignore_unused.hpp>

namespace bcostars
{
class RpcServiceClient : public bcos::rpc::RPCInterface
{
public:
    RpcServiceClient(bcostars::RpcServicePrx _prx, std::string const& _rpcServiceName);
    ~RpcServiceClient() override;

    class Callback : public RpcServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr)> _callback);
        ~Callback() override;

        void callback_asyncNotifyBlockNumber(const bcostars::Error& ret) override;
        void callback_asyncNotifyBlockNumber_exception(tars::Int32 ret) override;

    private:
        std::function<void(bcos::Error::Ptr)> m_callback;
    };

    void asyncNotifyBlockNumber(std::string const& _groupID, std::string const& _nodeName,
        bcos::protocol::BlockNumber _blockNumber,
        std::function<void(bcos::Error::Ptr)> _callback) override;

    void asyncNotifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback) override;

    void asyncNotifyAMOPMessage(int16_t _type, std::string const& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&& _error, bcos::bytesPointer _responseData)> _callback)
        override;

    void asyncNotifySubscribeTopic(
        std::function<void(bcos::Error::Ptr&& _error, std::string)> _callback) override;

    bcostars::RpcServicePrx prx();

protected:
    void start() override;
    void stop() override;

    static bool shouldStopCall();

private:
    bcostars::RpcServicePrx m_prx;
    std::string m_rpcServiceName;
    std::string const c_moduleName = "RpcServiceClient";
    // AMOP timeout 40s
    const int c_amopTimeout = 40000;

    static std::atomic<int64_t> s_tarsTimeoutCount;
    static const int64_t c_maxTarsTimeoutCount;
};

}  // namespace bcostars