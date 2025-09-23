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

#include <magic_enum.hpp>
namespace bcos::boostssl::ws
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
    MessageOverflow = -4011,
    UndefinedException = -4012,
    MessageEncodeError = -4013
};

inline bool notRetryAgain(int _wsError)
{
    return (_wsError == boostssl::ws::WsError::MessageOverflow);
}

inline std::string wsErrorToString(WsError _wsError)
{
#ifdef WIN32
    return std::to_string(_wsError);
#else
    return std::string(magic_enum::enum_name(_wsError));
#endif
}

}  // namespace bcos::boostssl::ws