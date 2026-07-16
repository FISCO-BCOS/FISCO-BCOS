#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <bcos-evm-ref/opstack/RollupCost.h>

namespace bcos::evmref::opstack
{
std::variant<OpTxProperties, std::error_code> opValidate(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::Transaction& tx,
    evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg, const OpFeeParams& fee,
    int64_t blockGasLeft)
{
    if (tx.type == evmone::state::Transaction::Type::blob)
        return make_error_code(std::errc::not_supported);

    if (signedTxEnvelope.empty())
        return make_error_code(std::errc::invalid_argument);

    auto base = evmone::state::validate_transaction(view, block, tx, cfg.rev, blockGasLeft, 0);
    if (auto* err = std::get_if<std::error_code>(&base))
        return *err;

    uint32_t flzLen = 0;
    intx::uint256 l1Cost;
    if (cfg.has_ecotone_l1_formula)
    {
        l1Cost = computeL1Cost(fee, signedTxEnvelope, cfg);
    }
    else
    {
        flzLen = flzCompressLen(signedTxEnvelope);
        l1Cost = computeL1CostFromFlz(fee, flzLen, cfg);
    }
    const auto opCost = cfg.has_operator_fee ?
                            computeOperatorCost(fee, static_cast<uint64_t>(tx.gas_limit), cfg) :
                            intx::uint256{0};
    const auto acc = view.get_account(tx.sender);
    const auto balance = acc ? acc->balance : intx::uint256{0};
    const auto maxCost = intx::uint256{static_cast<uint64_t>(tx.gas_limit)} * tx.max_gas_price +
                         tx.value + l1Cost + opCost;
    if (balance < maxCost)
        return make_error_code(std::errc::result_out_of_range);

    return OpTxProperties{
        std::get<evmone::state::TransactionProperties>(base), l1Cost, opCost, fee, flzLen};
}

std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft)
{
    return opValidate(view, block, tx, signedTxEnvelope, cfg, loadOpFeeParams(view), blockGasLeft);
}
}  // namespace bcos::evmref::opstack
