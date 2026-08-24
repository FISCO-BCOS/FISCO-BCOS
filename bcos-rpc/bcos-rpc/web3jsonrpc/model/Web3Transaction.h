/**
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file Web3Transaction.h
 * Compatibility shim: typed-tx RLP lives in bcos-codec. takeToTarsTransaction
 * stays here because it depends on protocol-tars.
 */

#pragma once
#include <bcos-codec/rlp/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>

namespace bcos::rpc
{
/// Convert a decoded Web3 tx into the Tars wire form used by the rest of the node.
bcostars::Transaction takeToTarsTransaction(Web3Transaction& tx);
}  // namespace bcos::rpc
