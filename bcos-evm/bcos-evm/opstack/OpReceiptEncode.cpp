#include <bcos-evm/opstack/OpReceiptEncode.h>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <bcos-evm/eth/utils/rlp_encode.hpp>

namespace bcos::evmref::opstack
{
evmc::bytes encodeReceiptForRoot(const OpDepositReceipt& r)
{
    // depositReceiptRLP (receipt.go:136-148) + EncodeIndex typed prefix (:575 WriteByte).
    // The status-encoding idiom follows evmone rlp_encode(TransactionReceipt)
    // (rlp_encode.cpp:86-90).
    return evmc::bytes{0x7e} + evmone::rlp::encode_tuple(r.receipt.status == EVMC_SUCCESS,
                                   static_cast<uint64_t>(r.receipt.cumulative_gas_used),
                                   evmone::bytes_view(r.receipt.logs_bloom_filter), r.receipt.logs,
                                   r.deposit_nonce, r.deposit_receipt_version);
}

evmc::bytes encodeReceiptForRoot(const OpTxReceipt& r)
{
    return evmone::state::rlp_encode(r.receipt);
}

evmc::bytes encodeReceiptForRoot(const std::variant<OpDepositReceipt, OpTxReceipt>& r)
{
    return std::visit([](const auto& x) { return encodeReceiptForRoot(x); }, r);
}
}  // namespace bcos::evmref::opstack
