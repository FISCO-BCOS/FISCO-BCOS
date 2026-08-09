#pragma once

#include "CallParameters.h"
#include "bcos-framework/protocol/Transaction.h"
#include <cstdint>
#include <memory>

namespace bcos::executor
{

/// Resolved Web3 typed-tx metadata from protocol (Tars) fields.
struct Web3AccessListResolved
{
    /// EIP-2718 envelope / typed kind (1 = EIP-2930). 0 = unset / not applicable.
    uint8_t web3TypedTxKind = 0;
    /// Non-null when the Tars access list has at least one entry. Empty lists stay nullptr
    /// (downstream treats nullptr and empty the same).
    std::shared_ptr<const Eip2930AccessList> accessList;
};

/// Resolve Web3 access list from protocol (Tars) fields. Admission already
/// rejects Tars vs signed-RLP disagreement, so no RLP fallback is needed here.
Web3AccessListResolved resolveWeb3AccessList(protocol::Transaction const& tx);

}  // namespace bcos::executor
