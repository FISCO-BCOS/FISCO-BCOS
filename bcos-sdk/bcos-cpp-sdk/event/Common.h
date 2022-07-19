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
 * @file Common.h
 * @author: octopus
 * @date 2021-09-01
 */

#pragma once

#include <bcos-utilities/BoostLog.h>
namespace bcos
{
namespace cppsdk
{
namespace event
{
/**
 * @brief: event sub message types
 */
enum MessageType
{
    // ------------event begin ---------

    EVENT_SUBSCRIBE = 0x120,    // 288
    EVENT_UNSUBSCRIBE = 0x121,  // 289
    EVENT_LOG_PUSH = 0x122,     // 290

    // ------------event end ---------
};
}  // namespace event
}  // namespace cppsdk
}  // namespace bcos


// The largest number of topic in one event log
#define EVENT_LOG_TOPICS_MAX_INDEX (4)

#define EVENT_TASK(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][TASK]"
#define EVENT_PARAMS(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][PARAMS]"
#define EVENT_REQUEST(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][REQUEST]"
#define EVENT_RESPONSE(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][RESPONSE]"
#define EVENT_SUB(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][SUB]"
