#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpDepositTx.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpHost.h>
#include <cassert>
#include <limits>
#include <optional>
#include <stdexcept>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/errors.hpp>
#include <bcos-evm/eth/state/state.hpp>

namespace bcos::evm::opstack
{
namespace
{
/// View mask for validate_transaction: presents the depositor as "sufficient balance + empty
/// code", mirroring op-geth skipping the EOA/balance checks in preCheck for deposits. Value
/// affordability is checked explicitly inside runDeposit (against the post-mint balance,
/// op-geth state_transition.go:578 clause 6).
class DepositValidationView final : public evmone::state::StateView
{
public:
    DepositValidationView(
        const evmone::state::StateView& base, const evmc::address& sender) noexcept
      : m_base{base}, m_sender{sender}
    {}

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        auto acc = m_base.get_account(addr);
        if (addr != m_sender)
            return acc;
        if (!acc.has_value())
            acc.emplace();
        acc->balance = std::numeric_limits<intx::uint256>::max();  // mask INSUFFICIENT_FUNDS
        acc->code_hash = evmone::state::Account::EMPTY_CODE_HASH;  // mask EIP-3607
        return acc;
    }

    evmone::state::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        return addr == m_sender ? evmone::state::bytes{} : m_base.get_account_code(addr);
    }

    evmone::state::bytes32 get_storage(
        const evmc::address& addr, const evmone::state::bytes32& key) const noexcept override
    {
        return m_base.get_storage(addr, key);
    }

private:
    const evmone::state::StateView& m_base;
    evmc::address m_sender;
};
}  // namespace
OpDepositReceipt runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    int64_t blockGasLeft)
{
    if (dep.is_system_tx)
        throw std::runtime_error("op deposit: is_system_tx not supported (block error)");

    evmone::state::State state{view};
    auto& fromAcc = state.get_or_insert(dep.from);
    const uint64_t preNonce = fromAcc.nonce;
    if (dep.mint.has_value())
        fromAcc.balance += *dep.mint;

    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::legacy;  // internal execution shell; receipt uses kDepositTxType
    tx.sender = dep.from;
    tx.to = dep.to;
    tx.gas_limit = dep.gas_limit;
    tx.value = dep.value;
    tx.data = dep.data;
    tx.max_gas_price = 0;
    tx.max_priority_gas_price = 0;
    tx.nonce = preNonce;

    // Deposit skips the fee cap validation; validate is used only to compute intrinsic gas / the EIP-7623 floor.
    evmone::state::BlockInfo validateBlock = block;
    validateBlock.base_fee = 0;
    const DepositValidationView maskedView{view, dep.from};
    const auto props = evmone::state::validate_transaction(
        maskedView, validateBlock, tx, cfg.rev, blockGasLeft, 0);

    evmone::state::TransactionReceipt receipt;
    receipt.type = kDepositTxType;

    if (const auto* err = std::get_if<std::error_code>(&props))
    {
        if (*err == evmone::state::make_error_code(evmone::state::GAS_LIMIT_REACHED))
            throw std::runtime_error("op deposit: block gas limit reached (block error)");
        // Processing-level failure (op-geth Regolith, state_transition.go:486-513):
        // mint is retained, nonce is force-incremented, gasUsed = gasLimit in full (:498).
        state.get(dep.from).nonce = preNonce + 1;
        receipt.status = EVMC_FAILURE;
        receipt.gas_used = dep.gas_limit;
    }
    else
    {
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        if (state.get(dep.from).balance < dep.value)
        {
            // op-geth clause 6 (state_transition.go:578): consensus-layer error → failed-deposit
            // branch (:486-513), gasUsed = gasLimit in full (:498). Same as a validate failure.
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = EVMC_FAILURE;
            receipt.gas_used = dep.gas_limit;
        }
        else
        {
            // Host::prepare_message does not bump the nonce itself for depth==0 messages (the
            // upstream evmone assumes the caller already bumped it, and CREATE address derivation
            // uses nonce-1 to obtain the "pre-execution" nonce) — retains the fix from 2327532.
            assert(fromAcc.nonce < evmone::state::Account::NonceMax);
            ++fromAcc.nonce;
            OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
            auto outcome = runTxMessage(state, host, tx, cfg.rev, block.coinbase,
                p.execution_gas_limit, p.min_gas_cost, /*delegation_refund=*/0);
            receipt.status = outcome.result.status_code;
            receipt.gas_used = outcome.gas_used;
            receipt.logs = host.take_logs();
        }
    }
    receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(receipt.logs);
    receipt.state_diff = bcos::evm::sanitizeStateDiff(view, state.build_diff(cfg.rev));
    return OpDepositReceipt{std::move(receipt), preNonce, 1};
}
}  // namespace bcos::evm::opstack
