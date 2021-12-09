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
 * @brief define the common service information
 * @file ServiceDesc.h
 * @author: yujiechen
 * @date: 2021-10-11
 */
#pragma once
#include <iostream>
#include <memory>
#include <string>
namespace bcos
{
namespace protocol
{
enum ServiceType : int32_t
{
    NATIVE = 0,
    EXECUTOR = 1,
    LEDGER = 2,
    SCHEDULER = 3,
    FRONT = 4,
    CONSENSUS = 5,
    GATEWAY = 6,
    RPC = 7,
    TXPOOL = 8,
};
const std::string NATIVE_SERVANT_NAME = "NativeNodeObj";

const std::string LEDGER_SERVANT_NAME = "LedgerServiceObj";

const std::string EXECUTOR_SERVANT_NAME = "ExecutorServiceObj";
const std::string EXECUTOR_SERVICE_NAME = "ExecutorService";

const std::string SCHEDULER_SERVANT_NAME = "SchedulerServiceObj";
const std::string SCHEDULER_SERVICE_NAME = "SchedulerService";

const std::string FRONT_SERVANT_NAME = "FrontServiceObj";
const std::string FRONT_SERVICE_NAME = "FrontService";

const std::string GATEWAY_SERVANT_NAME = "GatewayServiceObj";

const std::string TXPOOL_SERVANT_NAME = "TxPoolServiceObj";
const std::string TXPOOL_SERVICE_NAME = "TxPoolService";

const std::string CONSENSUS_SERVANT_NAME = "PBFTServiceObj";
const std::string CONSENSUS_SERVICE_NAME = "PBFTService";

const std::string RPC_SERVANT_NAME = "RpcServiceObj";

const std::string UNKNOWN_SERVANT = "Unknown";

inline std::string getPrxDesc(std::string const& _serviceName, std::string const& _objName)
{
    return _serviceName + "." + _objName;
}

inline std::string getServiceObjByType(ServiceType _type)
{
    switch (_type)
    {
    case NATIVE:
        return NATIVE_SERVANT_NAME;
    case EXECUTOR:
        return EXECUTOR_SERVANT_NAME;
    case LEDGER:
        return LEDGER_SERVANT_NAME;
    case SCHEDULER:
        return SCHEDULER_SERVANT_NAME;
    case FRONT:
        return FRONT_SERVANT_NAME;
    case CONSENSUS:
        return CONSENSUS_SERVANT_NAME;
    case GATEWAY:
        return GATEWAY_SERVANT_NAME;
    case RPC:
        return RPC_SERVANT_NAME;
    case TXPOOL:
        return TXPOOL_SERVANT_NAME;
    default:
        // undefined Comparator
        break;
    }
    return UNKNOWN_SERVANT;
}
}  // namespace protocol
}  // namespace bcos