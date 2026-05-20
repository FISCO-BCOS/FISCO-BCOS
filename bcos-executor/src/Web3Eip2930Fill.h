#pragma once

#include "CallParameters.h"
#include "bcos-framework/protocol/Transaction.h"
#include <cstdint>
#include <memory>

namespace bcos::executor
{

/// Parsed EIP-2930 metadata from `protocol::Transaction::extraTransactionBytes` (Web3 typed tx).
struct Web3Eip2930Parsed
{
    /// EIP-2718 envelope / typed kind (1 = EIP-2930). 0 = unset / not applicable.
    uint8_t web3TypedTxKind = 0;
    /// Non-null only when an access list was decoded (may be empty for type-1 txs with no entries).
    std::shared_ptr<const Eip2930AccessList> accessList;
};

/// Read EIP-2930 metadata from protocol fields (scheme B); fall back to extraTransactionBytes for
/// legacy txs.
Web3Eip2930Parsed parseEip2930FromWeb3Transaction(protocol::Transaction const& tx);

}  // namespace bcos::executor
