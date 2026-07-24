#pragma once

#include <bcos-evm/opstack/OpDepositTx.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <variant>

namespace bcos::evmref::opstack
{
/// receipts-root leaf encoding (op-geth Receipts.EncodeIndex semantics, receipt.go:568-592 --
/// note this is NOT MarshalBinary :279-288; the two deliberately differ for a receipt that
/// "has nonce, has no version", and the function-header comment :564-567 explicitly forbids
/// changing that; this module's supported surface always comes in pairs so it has no such fork,
/// yet still follows EncodeIndex).
/// deposit: 0x7E || rlp([status, cumulativeGasUsed, logsBloom, logs, depositNonce,
/// depositReceiptVersion]) (depositReceiptRLP :136-148, the last two fields come as a pair).
/// The prefix is always 0x7e rather than reading r.receipt.type -- equivalent under the module
/// invariant (runDeposit, the sole construction point, always sets kDepositTxType,
/// OpDepositTx.cpp:89/:128).
/// normal tx: delegates to evmone rlp_encode (typed raw-byte prefix + [status, cumGas, bloom,
/// logs], byte-for-byte identical to EncodeIndex for type 0/1/2/4).
[[nodiscard]] evmc::bytes encodeReceiptForRoot(const OpDepositReceipt& r);
[[nodiscard]] evmc::bytes encodeReceiptForRoot(const OpTxReceipt& r);
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const std::variant<OpDepositReceipt, OpTxReceipt>& r);
}  // namespace bcos::evmref::opstack
