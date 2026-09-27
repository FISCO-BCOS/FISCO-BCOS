/// @file EthExecutionPolicy.h
/// @brief Chain-level execution policy for the ethereum-executor: the points
///        where an L2 (OP Stack) diverges from L1 Ethereum — transaction-type
///        admission, the fee model (withhold/settle), intrinsic gas and
///        precompile dispatch — are factored into a policy type so an L2 can
///        extend this executor instead of re-porting it.
///
/// The policy is a compile-time template parameter (default EthL1Policy) on
/// validateTransaction / runTransaction (EthereumTransition.h) and
/// EthereumHost (EthereumHost.h); a policy *object* may additionally carry
/// runtime per-block configuration (EthL1Policy is an empty, zero-cost tag;
/// an OpPolicy can hold e.g. the resolved OpForkConfig). All hooks are static
/// member functions, so a stateless policy costs nothing.
///
/// EthL1Policy's bodies are the verbatim L1 logic moved out of
/// EthereumTransition.h / EthereumHost.h — the default instantiation is
/// bit-for-bit the previous behaviour (covered by the EEST regression run).
///
/// Include contract: this header needs the complete EthBlockInfo /
/// EthCallParams (EthereumHost.h) and EthereumState / EthAccount
/// (EthereumState.h) types, while EthereumHost.h takes EthL1Policy as a
/// default template argument from a forward declaration only. Therefore:
///   - EthereumHost.h forward-declares EthL1Policy and never includes this
///     header;
///   - every translation unit that instantiates EthereumHost (or calls
///     validateTransaction / runTransaction) MUST include this header —
///     EthereumTransition.h, EthSystemCalls.h and EthereumExecutor.h do.

#pragma once

#include "EVMPrecompiles.h"
#include "EVMSupport.h"
#include "EthereumHost.h"
#include "EthereumState.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/protocol/TxGasModel.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>
#include <system_error>

namespace bcos::executor_v1::eth
{
/// Map an evmc status code to the FISCO internal TransactionStatus convention
/// (0 = success / None, non-zero = failure). Lives next to the policy because
/// the buildReceipt hook consumes it.
inline int32_t mapEvmcStatusToBcosStatus(evmc_status_code status)
{
    switch (status)
    {
    case EVMC_SUCCESS:
        return static_cast<int32_t>(protocol::TransactionStatus::None);
    case EVMC_REVERT:
        return static_cast<int32_t>(protocol::TransactionStatus::RevertInstruction);
    case EVMC_OUT_OF_GAS:
        return static_cast<int32_t>(protocol::TransactionStatus::OutOfGas);
    case EVMC_UNDEFINED_INSTRUCTION:
    case EVMC_INVALID_INSTRUCTION:
        return static_cast<int32_t>(protocol::TransactionStatus::BadInstruction);
    case EVMC_BAD_JUMP_DESTINATION:
        return static_cast<int32_t>(protocol::TransactionStatus::BadJumpDestination);
    case EVMC_STACK_OVERFLOW:
        return static_cast<int32_t>(protocol::TransactionStatus::OutOfStack);
    case EVMC_STACK_UNDERFLOW:
        return static_cast<int32_t>(protocol::TransactionStatus::StackUnderflow);
    case EVMC_INSUFFICIENT_BALANCE:
        return static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash);
    default:
        return static_cast<int32_t>(protocol::TransactionStatus::Unknown);
    }
}

/// The Ethereum L1 execution policy: every hook reproduces the behaviour that
/// was previously hard-coded in validateTransaction / runTransaction /
/// EthereumHost. An L2 policy overrides only the hooks it diverges on.
struct EthL1Policy
{
    /// Transaction-type admission: the EIP-2718 type-byte fork gating and the
    /// blob / set-code specific checks (moved verbatim from validateTransaction).
    /// @return the validation error, or std::nullopt when the type is admitted.
    static std::optional<std::error_code> validateTxType(protocol::Transaction const& tx,
        uint8_t txKind, evmc_revision rev, EthBlockInfo const& block, int64_t blobGasLeft,
        EthCallParams const& callParams)
    {
        // Reject unknown / out-of-range typed-tx kinds (only 0-4 exist). geth
        // rejects unknown type bytes at RLP decode; the port has no decode layer,
        // so without this a crafted kind (>=5) would skip both type gates below,
        // trip the maxPriorityGasPrice assert (debug) or silently run as legacy
        // (release), diverging from geth.
        if (txKind > 4)
            return make_error_code(evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);
        const auto maxGasPrice = ethMaxGasPrice(tx, callParams);
        const auto maxPriorityGasPrice = ethMaxPriorityGasPrice(tx, callParams);
        const auto hasTo = protocol::ethToAddress(tx).has_value();
        const auto& blobHashes = tx.blobVersionedHashes();

        switch (txKind)  // Validate "special" transaction types.
        {
        case 3:  // blob
            if (rev < EVMC_CANCUN)
                return make_error_code(evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);
            if (!hasTo)
                return make_error_code(evm::ErrorCode::CREATE_BLOB_TX);
            if (blobHashes.empty())
                return make_error_code(evm::ErrorCode::EMPTY_BLOB_HASHES_LIST);
            if (rev >= EVMC_OSAKA && blobHashes.size() > evm::MAX_TX_BLOB_COUNT)
                return make_error_code(evm::ErrorCode::BLOB_GAS_LIMIT_EXCEEDED);

            assert(block.blob_base_fee.has_value());
            if (ethMaxBlobGasPrice(tx) < *block.blob_base_fee)
                return make_error_code(evm::ErrorCode::BLOB_FEE_CAP_LESS_THAN_BLOCKS);

            if (std::ranges::any_of(blobHashes, [](const auto& h) { return h[0] != 0x01; }))
                return make_error_code(evm::ErrorCode::INVALID_BLOB_HASH_VERSION);
            if (static_cast<uint64_t>(evm::GAS_PER_BLOB) * blobHashes.size() >
                static_cast<uint64_t>(blobGasLeft))
                return make_error_code(evm::ErrorCode::BLOB_GAS_LIMIT_EXCEEDED);
            break;

        case 4:  // set_code
            if (rev < EVMC_PRAGUE)
                return make_error_code(evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);
            if (!hasTo)
                return make_error_code(evm::ErrorCode::CREATE_SET_CODE_TX);
            if (tx.authorizationList().empty())
                return make_error_code(evm::ErrorCode::EMPTY_AUTHORIZATION_LIST);
            break;

        default:;
        }

        switch (txKind)  // Validate the "regular" transaction type hierarchy.
        {
        case 4:  // set_code
        case 3:  // blob
        case 2:  // eip1559
            if (rev < EVMC_LONDON)
                return make_error_code(evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);

            if (maxPriorityGasPrice > maxGasPrice)
                return make_error_code(
                    evm::ErrorCode::TIP_GT_FEE_CAP);  // Priority gas price is too high.
            [[fallthrough]];

        case 1:  // access_list
            if (rev < EVMC_BERLIN)
                return make_error_code(evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);
            [[fallthrough]];

        case 0:;  // legacy
        }
        return std::nullopt;
    }

    /// Chain-level addition to the sender's theoretical maximum transaction
    /// cost checked against the balance in validateTransaction. L1: the blob
    /// fee (moved verbatim); an L2 would add its L1 data fee here.
    static bcos::u512 additionalMaxCost(protocol::Transaction const& tx,
        EthBlockInfo const& /*block*/, evmc_revision /*rev*/,
        EthCallParams const& /*callParams*/)
    {
        bcos::u512 extra = 0;
        if (tx.web3TypedTxKind() == 3)  // blob
        {
            const auto total_blob_gas =
                static_cast<uint64_t>(evm::GAS_PER_BLOB) * tx.blobVersionedHashes().size();
            // 256-bit product then widened — matches the original intx expression bit for bit.
            extra += bcos::u512(uint256(total_blob_gas) * ethMaxBlobGasPrice(tx));
        }
        return extra;
    }

    /// The EIP-1559 effective gas price (and the priority price via the out
    /// parameter). Single implementation of the formula that was duplicated in
    /// runTransaction and EthereumHost::get_tx_context; both callers agree on
    /// the result by construction now.
    ///
    /// Takes the resolved max prices (ethMaxGasPrice / ethMaxPriorityGasPrice,
    /// after the EthCallParams dry-run overrides) rather than the Transaction
    /// itself: an L2 transaction kind that is not a protocol::Transaction
    /// (OP's deposit) can still flow through the host's get_tx_context with a
    /// policy that prices it.
    static uint256 effectiveGasPrice(uint256 maxGasPrice, uint256 maxPriorityGasPrice,
        EthBlockInfo const& block, evmc_revision rev, uint256& priorityGasPriceOut)
    {
        const auto base_fee = (rev >= EVMC_LONDON) ? block.base_fee : 0;
        const auto max_gas_price = maxGasPrice;
        const auto max_priority_gas_price = maxPriorityGasPrice;
        assert(max_gas_price >= base_fee);                // Required for valid tx.
        assert(max_gas_price >= max_priority_gas_price);  // Required for valid tx.
        priorityGasPriceOut = std::min(max_priority_gas_price, max_gas_price - base_fee);
        const auto effective_gas_price = base_fee + priorityGasPriceOut;
        assert(effective_gas_price <= max_gas_price);  // Required for valid tx.
        return effective_gas_price;
    }

    /// The BLOBBASEFEE value reported by get_tx_context. L1: the header's
    /// blob base fee, zero when the field is absent (pre-Cancun blocks).
    static uint256 blobBaseFee(EthBlockInfo const& block) noexcept
    {
        return block.blob_base_fee.value_or(0);
    }

    /// Addresses that are always warm WITHOUT materializing an account entry
    /// (checked by access_account before the get_or_insert, so no ghost
    /// account is journalled). L1: none — plain precompiles are warm via
    /// isPrecompile after insertion, matching evmone.
    static bool isAlwaysWarmPrecompile(evmc_revision /*rev*/, evmc::address const& /*addr*/) noexcept
    {
        return false;
    }

    /// Up-front fee withholding before execution: the gas prepayment plus the
    /// blob fee (moved verbatim from runTransaction). @return tx_max_cost.
    static uint256 withholdUpfront(EthAccount& senderAcc, protocol::Transaction const& tx,
        EthBlockInfo const& block, evmc_revision /*rev*/, int64_t gasLimit,
        uint256 effectiveGasPrice)
    {
        const auto tx_max_cost = uint256(static_cast<uint64_t>(gasLimit)) * effectiveGasPrice;

        senderAcc.balance -= tx_max_cost;  // Modify sender balance after all checks.

        if (tx.web3TypedTxKind() == 3)  // blob
        {
            // This uint64 * uint256 cannot overflow, because tx.blob_gas_used has limits
            // enforced before this stage.
            assert(block.blob_base_fee.has_value());
            const auto blob_gas_used =
                static_cast<uint64_t>(evm::GAS_PER_BLOB) * tx.blobVersionedHashes().size();
            const auto blob_fee = bcos::u512(blob_gas_used) * bcos::u512(*block.blob_base_fee);
            assert(blob_fee <= bcos::u512(std::numeric_limits<uint256>::max()));
            assert(bcos::u512(senderAcc.balance) >= blob_fee);  // Required for valid tx.
            senderAcc.balance -= uint256(blob_fee);
        }
        return tx_max_cost;
    }

    /// Post-execution fee settlement (moved verbatim from runTransaction):
    /// refund capped at gas_used / quotient, EIP-7623 floor, prepayment
    /// refund and the coinbase tip. @return the final gas_used.
    template <class Storage>
    static int64_t settleFees(EthereumState<Storage>& state, EthBlockInfo const& block,
        evmc_revision rev, int64_t minGasCost, int64_t gasLimit, int64_t gasLeft,
        int64_t delegationRefund, int64_t evmRefund, uint256 txMaxCost,
        uint256 effectiveGasPrice, uint256 priorityGasPrice, EthAccount& senderAcc)
    {
        auto gas_used = gasLimit - gasLeft;

        const auto max_refund_quotient = rev >= EVMC_LONDON ? 5 : 2;
        const auto refund_limit = gas_used / max_refund_quotient;
        const auto refund = std::min(delegationRefund + evmRefund, refund_limit);
        gas_used -= refund;
        assert(gas_used > 0);

        // EIP-7623: The gas used by the transaction must be at least the min_gas_cost.
        gas_used = std::max(gas_used, minGasCost);

        senderAcc.balance += txMaxCost - uint256(static_cast<uint64_t>(gas_used)) * effectiveGasPrice;
        state.touch(block.coinbase).balance +=
            uint256(static_cast<uint64_t>(gas_used)) * priorityGasPrice;
        return gas_used;
    }

    /// Intrinsic gas (g0) and the EIP-7623 floor cost — the canonical formula
    /// lives in bcos-framework (shared with admission); see TxGasModel.h.
    static protocol::gas::TransactionCost intrinsicCost(
        evmc_revision rev, protocol::Transaction const& tx)
    {
        return protocol::gas::compute_tx_intrinsic_cost(rev, tx);
    }

    /// Precompile dispatch overrides. L1: the plain evmone-ported table. An
    /// L2 policy can widen the address set (e.g. OP Isthmus enables 0x100
    /// p256verify ahead of Osaka) or override gas/implementation per address.
    static bool isPrecompile(evmc_revision rev, evmc::address const& addr) noexcept
    {
        return evm::is_precompile(rev, addr);
    }

    /// Execute the precompile for @p msg. Only called when isPrecompile
    /// returned true for msg.code_address; the L1 default forwards to the
    /// shared table.
    static evmc::Result callPrecompile(evmc_revision rev, evmc_message const& msg) noexcept
    {
        return evm::call_precompile(rev, msg);
    }

    /// Build a BCOS receipt from the executed EVM result (no evmone receipt
    /// intermediate; moved verbatim from EthereumTransition.h's former
    /// buildBcosReceipt). Return data is not retained by the host, matching
    /// the v2 executor's documented limitation.
    ///
    /// Templated on the host type (not on Storage/Policy separately) so the
    /// hook works for any policy's host instantiation — only take_logs() is
    /// consumed. An L2 policy overrides this to impose its own receipt shape
    /// (OP collapses status to 0/1 and stamps the rollup fee metadata).
    template <class Host>
    static protocol::TransactionReceipt::Ptr buildReceipt(Host& host, evmc::Result const& result,
        int64_t gasUsed, protocol::TransactionReceiptFactory const& rf, int64_t blockNumber)
    {
        std::vector<protocol::LogEntry> logs;
        for (auto const& l : host.take_logs())
        {
            bcos::bytes addr(l.addr.bytes, l.addr.bytes + sizeof(evmc_address));
            bcos::h256s topics;
            for (auto const& t : l.topics)
                topics.emplace_back(bcos::bytesConstRef(t.bytes, sizeof(evmc_bytes32)));
            bcos::bytes data(l.data.begin(), l.data.end());
            logs.emplace_back(std::move(addr), std::move(topics), std::move(data));
        }
        bcos::bytes output;
        return rf.createReceipt(bcos::u256(static_cast<uint64_t>(gasUsed)), std::string{}, logs,
            mapEvmcStatusToBcosStatus(result.status_code), bcos::ref(output), blockNumber);
    }
};
}  // namespace bcos::executor_v1::eth
