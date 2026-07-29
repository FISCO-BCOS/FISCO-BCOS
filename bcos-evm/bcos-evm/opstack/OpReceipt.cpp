#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceipt.h>
#include <bcos-evm/opstack/RollupCost.h>
// TODO(eth-utils-removal): rlp/rlp_encode(eth/utils)→bcos-codec/rlp/RLPEncode.h;
// 回执编码输出是建根输入,必须与 op-geth 逐字节等价(由 33 向量 gate 判定)。
#include <test/utils/rlp.hpp>
#include <test/utils/rlp_encode.hpp>

namespace bcos::evm::opstack
{
OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee, uint32_t flzLen,
    intx::uint256 l1_cost, intx::uint256 operator_fee_at_used, bool fill_operator_scalars,
    std::optional<bool> has_operator_fee) noexcept
{
    // The charge decision, not this call's cfg — see the declaration.
    const bool charged_operator_fee = has_operator_fee.value_or(cfg.has_operator_fee);
    OpReceiptMeta m;
    m.l1_gas_price = fee.l1_base_fee;
    m.l1_blob_base_fee = fee.blob_base_fee;
    m.l1_base_fee_scalar = fee.base_fee_scalar;
    m.l1_blob_base_fee_scalar = fee.blob_base_fee_scalar;
    m.l1_fee = l1_cost;
    if (charged_operator_fee)
    {
        m.operator_fee = operator_fee_at_used;
        if (fill_operator_scalars &&
            (fee.operator_fee_scalar != 0 || fee.operator_fee_constant != 0))
        {
            m.operator_fee_scalar = fee.operator_fee_scalar;
            m.operator_fee_constant = fee.operator_fee_constant;
        }
    }
    if (cfg.has_da_footprint)
    {
        const auto scalar = static_cast<uint64_t>(fee.da_footprint_gas_scalar);
        m.da_footprint_gas_scalar = scalar;
        m.da_footprint = estimatedDaSizeFromFlz(flzLen) * scalar;
    }
    return m;
}

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
}  // namespace bcos::evm::opstack
