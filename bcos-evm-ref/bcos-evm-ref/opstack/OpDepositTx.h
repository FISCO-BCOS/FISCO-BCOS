#pragma once

#include <cstdint>
#include <evmc/bytes.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <optional>
#include <test/state/transaction.hpp>

namespace evmone::state
{
class StateView;
struct BlockInfo;
class BlockHashes;
}  // namespace evmone::state

namespace bcos::evmref::opstack
{
struct OpForkConfig;

/// 0x7E deposit tx (not an evmone Transaction). When mint has a value it is added unconditionally to the from balance (nullopt = not added);
/// value is transferred normally in the call -- two independent fields. is_system_tx must be false after Regolith.
struct DepositTx
{
    evmc::bytes32 source_hash;
    evmc::address from;
    std::optional<evmc::address> to;   // nullopt = contract creation (address derived from from + pre-execution nonce)
    std::optional<intx::uint256> mint; // nullopt = no mint (matches op-geth *big.Int which can be nil)
    intx::uint256 value;
    int64_t gas_limit;
    bool is_system_tx;
    evmc::bytes data;
};

/// OP 0x7E deposit transaction/receipt type (EIP-2718 typed envelope prefix).
constexpr auto kDepositTxType = static_cast<evmone::state::Transaction::Type>(0x7e);

struct OpDepositReceipt
{
    evmone::state::TransactionReceipt receipt;
    uint64_t deposit_nonce;            // pre-execution depositor nonce
    uint64_t deposit_receipt_version;  // = 1 (Canyon+)
};

/// Execute a single 0x7E deposit: skips buyGas; adds balance when mint has a value; charges intrinsic + 7623 floor;
/// both failure paths retain mint and force nonce++; is_system_tx==true throws std::runtime_error (block-level error).
/// gas_limit exceeding blockGasLeft throws std::runtime_error (op-geth ErrGasLimitReached, block-level error).
OpDepositReceipt runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    int64_t blockGasLeft);
}  // namespace bcos::evmref::opstack
