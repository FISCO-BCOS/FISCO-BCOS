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
 * @date 2021-08-25
 */
#pragma once

#define AMOP_CLIENT(LEVEL) BCOS_LOG(LEVEL) << "[AMOP][CLIENT]"
#define AMOP_TOPIC_MANAGER(LEVEL) BCOS_LOG(LEVEL) << "[AMOP][TOPICMANAGER]"

namespace bcos
{
namespace cppsdk
{
namespace amop
{
/**
 * @brief: amop message types
 */
enum MessageType
{
    // ------------AMOP begin ---------

    AMOP_SUBTOPIC = 0x110,   // 272
    AMOP_REQUEST = 0x111,    // 273
    AMOP_BROADCAST = 0x112,  // 274
    AMOP_RESPONSE = 0x113    // 275

    // ------------AMOP end ---------

};
}  // namespace amop
}  // namespace cppsdk
}  // namespace bcos