// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
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
