/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief FIB-69 follow-up: gateway-side tripwire pinning MAX_MESSAGE_LENGTH to 32 MB. The
 *        mirror assertion pinning front::MAX_PAYLOAD_LENGTH to the same value lives in
 *        bcos-front/test/unittests/FIB69_PayloadCapAlignmentTest.cpp; front and gateway
 *        must not include each other's headers, so each side pins its own constant by value.
 *        Raising one cap without the other silently re-opens the dead zone where messages
 *        pass the gateway but are dropped by the front service.
 * @file FIB69_MessageLengthAlignmentTest.cpp
 */

#include "bcos-gateway/Common.h"

static_assert(bcos::gateway::MAX_MESSAGE_LENGTH == 32UL * 1024 * 1024,
    "gateway MAX_MESSAGE_LENGTH must stay equal to front::MAX_PAYLOAD_LENGTH (32 MB); see "
    "bcos-front/bcos-front/FrontMessage.h");
