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
 * @file WsError.h
 * @author: octopus
 * @date 2021-10-02
 */
#pragma once

namespace bcos
{
namespace boostssl
{
namespace ws
{
enum WsError
{
    AcceptError = -4000,
    ReadError = -4001,
    WriteError = -4002,
    PingError = -4003,
    PongError = -4004,
    PacketError = -4005,
    SessionDisconnect = -4006,
    UserDisconnect = -4007,
    TimeOut = -4008,
    NoActiveCons = -4009,
    EndPointNotExist = -4010,
    UndefinedException = -4099
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos