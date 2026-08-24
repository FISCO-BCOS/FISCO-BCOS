/**
 * @file Web3TxConsistency.h
 * @brief Admission-time check: Tars accessList / web3TypedTxKind must match signed RLP.
 */
#pragma once

#include <bcos-framework/protocol/Transaction.h>

namespace bcostars::protocol
{
/// Decode extraTransactionBytes and require data.accessList / web3TypedTxKind to match.
/// Non-Web3 transactions return true. Decode failure or disagreement returns false.
bool web3TarsFieldsMatchSignedExtra(bcos::protocol::Transaction const& tx);
}  // namespace bcostars::protocol
