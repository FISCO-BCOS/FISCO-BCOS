/// @file OpEnvelopeCheck.h
/// @brief Fail-closed envelope↔mirror cross-checks for the bcos-evm-free OP
///        block path — the counterpart of OpstackExecutor.h's
///        envelopeExecutionFieldsMismatch / envelopeChainIdMismatch /
///        blockPathZeroSender / blockPathUnboundAuthorizationList.
///
/// The tars mirror is forgeable over p2p; only the SIGNED envelope bytes
/// (Transaction::extraTransactionBytes) bind the fields the signature covers.
/// The envelope side keeps the legacy RLP-walker logic verbatim (type-byte →
/// field-index mapping, over-wide integer pre-checks, element-wise list
/// binds); the mirror side reads protocol::Transaction accessors directly —
/// the same values the ethereum-executor stack consumes
/// (validateTransaction/runTransaction read the Transaction, there is no
/// evmone::state::Transaction conversion on the new path).
///
/// Contract (unchanged from the legacy helpers): each function returns a
/// mismatch description, or std::nullopt when consistent; the CALLER wraps a
/// mismatch into OpConsensusError with its txHash/capacity tags.

#pragma once

#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <optional>
#include <string>

namespace bcos::executor_v1::opstack
{
/// Bind the envelope's execution fields (nonce, gas, fees, to, value, data,
/// and element-wise accessList / blobVersionedHashes / authorizationList) to
/// the mirror the executor will run. The tx-type byte comes from the
/// ENVELOPE and must agree with the mirror's web3TypedTxKind.
[[nodiscard]] std::optional<std::string> opEthEnvelopeExecutionFieldsMismatch(
    bcos::bytesConstRef envelope, protocol::Transaction const& mirror);

/// Convenience overload: forwards the mirror's own envelope bytes (the mirror
/// is never trusted — only extraTransactionBytes is read).
[[nodiscard]] inline std::optional<std::string> opEthEnvelopeExecutionFieldsMismatch(
    protocol::Transaction const& tx)
{
    return opEthEnvelopeExecutionFieldsMismatch(tx.extraTransactionBytes(), tx);
}

/// The SIGNED envelope's chainId must equal the node chainId — never the
/// forgeable tars mirror. Legacy envelopes: only a genuinely unprotected form
/// (6-field preimage or v=27/28) is exempt; a malformed v or unparseable tail
/// fails closed. Deposit (0x7E) envelopes carry no chainId and skip the gate.
[[nodiscard]] std::optional<std::string> opEthEnvelopeChainIdMismatch(
    bcos::bytesConstRef envelope, uint64_t nodeChainId);

/// Convenience overload: deliberately forwards bytes instead of calling the
/// virtual parser, keeping the per-tx path identical to the block path.
[[nodiscard]] inline std::optional<std::string> opEthEnvelopeChainIdMismatch(
    protocol::Transaction const& tx, uint64_t nodeChainId)
{
    return opEthEnvelopeChainIdMismatch(tx.extraTransactionBytes(), nodeChainId);
}

/// Block-path only: address(0) is the eth_call default, but a sealed block
/// must not execute with that sender. The sender is read with the same
/// accessor the executor uses (eth::ethSender).
[[nodiscard]] std::optional<std::string> opEthBlockPathZeroSender(
    protocol::Transaction const& tx);

/// Block-path only: 7702 authorizationList[].signer is copied from the tars
/// mirror and is not recovered from the signed envelope. A non-empty list on
/// a non-0x04 tx would let processAuthorizationList apply unbound signers;
/// set_code (0x04) tuples are bound by opEthEnvelopeExecutionFieldsMismatch.
[[nodiscard]] std::optional<std::string> opEthBlockPathUnboundAuthorizationList(
    protocol::Transaction const& tx);
}  // namespace bcos::executor_v1::opstack
