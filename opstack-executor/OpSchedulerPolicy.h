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
 * @file OpSchedulerPolicy.h
 * @brief Pending-slot conflict classification
 */
#pragma once

#include <bcos-framework/protocol/ProtocolTypeDef.h>

namespace bcos::executor_v1::opstack
{

/// What to do with m_pending when a new execute arrives.
/// One pending slot + MLS FIFO merge: never stack a second verify=true layer.
enum class PendingConflict
{
    None,
    ReplaceSameHeight,
    KeepProbe,
    RefuseOtherHeight,
};

[[nodiscard]] inline PendingConflict classifyPendingConflict(bool hasPending,
    protocol::BlockNumber pendingHeight, protocol::BlockNumber incoming, bool verify)
{
    if (!hasPending)
    {
        return PendingConflict::None;
    }
    if (pendingHeight != incoming)
    {
        return PendingConflict::RefuseOtherHeight;
    }
    return verify ? PendingConflict::ReplaceSameHeight : PendingConflict::KeepProbe;
}

}  // namespace bcos::executor_v1::opstack
