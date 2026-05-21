#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace bcos::executor
{

struct Eip7702Authorization
{
    uint64_t chainId = 0;
    bcos::Address address;
    uint64_t nonce = 0;
    uint8_t yParity = 0;
    bcos::h256 r;
    bcos::h256 s;
};

using Eip7702AuthorizationList = std::vector<Eip7702Authorization>;

/// Parsed EIP-7702 metadata from Tars fields and/or `extraTransactionBytes`.
struct Web3Eip7702Parsed
{
    uint8_t web3TypedTxKind = 0;
    /// Non-null only for type-4 txs with a non-empty decoded authorization list.
    std::shared_ptr<const Eip7702AuthorizationList> authorizationList;
};

Web3Eip7702Parsed parseEip7702FromWeb3Transaction(protocol::Transaction const& tx);

}  // namespace bcos::executor
