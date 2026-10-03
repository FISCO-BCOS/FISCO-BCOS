/// @file OpExecutionPolicy.h
/// @brief The OP Stack execution policy for the ethereum-executor: an EthL1Policy
///        extension carrying the per-block OpForkSpec + OpFeeParams and the
///        per-transaction envelope/snapshot, overriding the hooks OP diverges
///        on — tx-type admission (deposit handled outside; blob rejected),
///        the L1-data/operator fee model (withhold/settle), BLOBBASEFEE,
///        the precompile override tables, and the receipt shape.
///
/// Ported from bcos-evm/opstack (OpTransition.cpp's opValidate/opTransition
/// and OpHost.cpp's three overrides) onto the EthExecutionPolicy hook surface;
/// every formula and constant is byte-for-byte the bcos-evm original (the
/// dual-run harness in tests/ pins the equivalence against the old path).
///
/// Unlike the stateless EthL1Policy, OpPolicy carries runtime state, so its
/// hooks are non-static member functions (the hook call sites — `policy.hook(...)`
/// — accept either). Lifetime: the policy is constructed per transaction by the
/// block executor (OpEthBlockExecute.h) and outlives the EthereumHost that
/// references it.

#pragma once

#include <ethereum-executor/EthExecutionPolicy.h>
#include <ethereum-executor/EthereumHost.h>
#include <ethereum-executor/EthereumState.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpForkSpec.h>
#include <opstack-executor/OpRollupCost.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/protocol/TxGasModel.h>
#include <bcos-utilities/Bloom.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;
using eth::uint256;

/// OP Stack chain policy. See the file header for the porting contract.
class OpPolicy
{
public:
    /// @param spec     the resolved fork spec for the executing block (OpForkSpec.h).
    /// @param fee      the L1Block fee parameters, loaded AFTER this block's L1
    ///                 attributes deposit executed (consensus-critical ordering).
    /// @param block    the executing block (base fee / blob-base-fee invariant).
    /// @param envelope the signed tx envelope bytes (the L1/DA fee input).
    /// @param tx       the decoded transaction (sender/nonce/to/prices are resolved here,
    ///                 after the dry-run overrides in @p callParams).
    /// @param snapshot caller-owned per-tx snapshot; written by additionalMaxCost,
    ///                 read by settleFees / buildReceipt (the validate/transition
    ///                 fork-boundary discipline — see OpTxSnapshot).
    OpPolicy(const OpForkSpec& spec, const OpFeeParams& fee, const eth::EthBlockInfo& block,
        evmc::bytes_view envelope, protocol::Transaction const& tx,
        eth::EthCallParams const& callParams, OpTxSnapshot& snapshot) noexcept
      : m_spec{spec},
        m_fee{fee},
        m_block{block},
        m_envelope{envelope},
        m_snapshot{&snapshot},
        m_sender{eth::ethSender(tx)},
        m_nonce{eth::effectiveNonce(tx, callParams)},
        m_isCreate{!protocol::ethToAddress(tx).has_value()},
        m_maxGasPrice{eth::ethMaxGasPrice(tx, callParams)},
        m_maxPriorityGasPrice{eth::ethMaxPriorityGasPrice(tx, callParams)}
    {}

    /// Deposit-mode constructor (B6's opRunDeposit): no protocol::Transaction, no fee
    /// snapshot — deposits pay nothing and build their own receipt. Only the fork
    /// spec matters here: the precompile override tables, BLOBBASEFEE=1 and the
    /// zero-price GASPRICE semantics apply inside deposit execution too.
    OpPolicy(const OpForkSpec& spec, const eth::EthBlockInfo& block) noexcept
      : m_spec{spec}, m_block{block}
    {}

    /// Transaction-type admission (ported opValidate's whitelist + the L1 hierarchy
    /// gates for the admitted kinds). OP rejects blob txs outright and every kind
    /// above set_code — the 0x7E deposit never reaches this hook (the block executor
    /// dispatches it to runDeposit first).
    std::optional<std::error_code> validateTxType(protocol::Transaction const& tx,
        uint8_t txKind, evmc_revision rev, eth::EthBlockInfo const& /*block*/,
        int64_t /*blobGasLeft*/, eth::EthCallParams const& callParams) const
    {
        // Whitelist, not a blacklist (ported opValidate comment): every value above
        // set_code — which includes the 0x7E deposit type — and the blob type are
        // rejected; an accepted out-of-range kind would fall through the revision
        // gates and be priced as legacy.
        if (txKind == 3 || txKind > 4)
            return make_error_code(eth::evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);

        const auto maxGasPrice = eth::ethMaxGasPrice(tx, callParams);
        const auto maxPriorityGasPrice = eth::ethMaxPriorityGasPrice(tx, callParams);
        const auto hasTo = protocol::ethToAddress(tx).has_value();

        switch (txKind)  // Validate "special" transaction types.
        {
        case 4:  // set_code
            if (rev < EVMC_PRAGUE)
                return make_error_code(eth::evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);
            if (!hasTo)
                return make_error_code(eth::evm::ErrorCode::CREATE_SET_CODE_TX);
            if (tx.authorizationList().empty())
                return make_error_code(eth::evm::ErrorCode::EMPTY_AUTHORIZATION_LIST);
            break;

        default:;
        }

        switch (txKind)  // Validate the "regular" transaction type hierarchy.
        {
        case 4:  // set_code
        case 2:  // eip1559
            if (rev < EVMC_LONDON)
                return make_error_code(eth::evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);

            if (maxPriorityGasPrice > maxGasPrice)
                return make_error_code(
                    eth::evm::ErrorCode::TIP_GT_FEE_CAP);  // Priority gas price is too high.
            [[fallthrough]];

        case 1:  // access_list
            if (rev < EVMC_BERLIN)
                return make_error_code(eth::evm::ErrorCode::TX_TYPE_NOT_SUPPORTED);
            [[fallthrough]];

        case 0:;  // legacy
        }
        return std::nullopt;
    }

    /// OP addition to the sender's theoretical maximum cost (ported opValidate's
    /// 512-bit cap inputs): l1Cost (four-branch fork selection) + operatorCost(gasLimit).
    /// Writes the validate-time snapshot the transition half reads.
    bcos::u512 additionalMaxCost(protocol::Transaction const& tx, eth::EthBlockInfo const& /*block*/,
        evmc_revision /*rev*/, eth::EthCallParams const& callParams) const
    {
        const auto gasLimit = eth::effectiveGasLimit(tx, callParams);

        uint32_t flzLen = 0;
        intx::uint256 l1Cost;
        std::optional<uint64_t> legacyL1GasUsed;
        if (m_spec.has_legacy_l1_formula)
        {
            // Bedrock–Delta: (txDataGas + overhead) * l1BaseFee * l1FeeScalar / 1e6, with the
            // +68 phantom non-zero bytes pre-Regolith. m_spec.fork is the ladder-resolved fork,
            // so the comparison is exactly IsRegolith(blockTime).
            const auto legacy =
                computeLegacyL1Cost(m_fee, m_envelope, m_spec.fork >= OpFork::Regolith);
            l1Cost = legacy.fee;
            legacyL1GasUsed = legacy.gas_used;
        }
        else if (ecotoneParamsUnset(m_fee))
        {
            // First-Ecotone-block fallback (op-geth rollup_cost.go NewL1CostFunc selectFunc):
            // Ecotone is active but the L1Block Ecotone parameters read all-zero, so the
            // Bedrock legacy formula applies — with isRegolith=true. Checked before the
            // Ecotone/Fjord split: "the first block of Fjord and Ecotone could be the same
            // block".
            const auto legacy = computeLegacyL1Cost(m_fee, m_envelope, /*regolithActive=*/true);
            l1Cost = legacy.fee;
            legacyL1GasUsed = legacy.gas_used;
        }
        else if (m_spec.has_ecotone_l1_formula)
        {
            l1Cost = computeL1Cost(m_fee, m_envelope, m_spec);
        }
        else
        {
            flzLen = flzCompressLen(m_envelope);
            l1Cost = computeL1CostFromFlz(m_fee, flzLen, m_spec);
        }
        const auto opCost = m_spec.has_operator_fee ?
                                computeOperatorCost(m_fee, static_cast<uint64_t>(gasLimit), m_spec) :
                                intx::uint256{0};

        auto& snapshot = *m_snapshot;
        snapshot.l1_cost = l1Cost;
        snapshot.operator_cost_at_gas_limit = opCost;
        snapshot.fee = m_fee;
        snapshot.flz_len = flzLen;
        snapshot.has_operator_fee = m_spec.has_operator_fee;
        snapshot.jovian_operator_formula = m_spec.has_jovian_operator_formula;
        snapshot.has_da_footprint = m_spec.has_da_footprint;
        // Under the Ecotone formula, snapshot the envelope's bedrockCalldataGasUsed for
        // deriveOpReceiptMeta to read as l1_gas_used -- preserving the no-spec invariant.
        // The first-Ecotone-block fallback is legacy-priced, so it takes the legacy snapshot.
        snapshot.ecotone_calldata_gas_used =
            (m_spec.has_ecotone_l1_formula && !legacyL1GasUsed.has_value()) ?
                std::optional<uint64_t>{bedrockCalldataGasUsed(m_envelope)} :
                std::nullopt;
        snapshot.legacy_l1_gas_used = legacyL1GasUsed;

        // Widened like the L1 blob fee: the caller's 512-bit comparison is the only guard
        // for the transition's unchecked subtractions.
        return bcos::u512(intxToBcosU256(l1Cost)) + bcos::u512(intxToBcosU256(opCost));
    }

    /// Intrinsic gas (g0) and the EIP-7623 floor cost — identical to L1 (the canonical
    /// formula lives in bcos-framework; see TxGasModel.h).
    protocol::gas::TransactionCost intrinsicCost(
        evmc_revision rev, protocol::Transaction const& tx) const
    {
        return eth::EthL1Policy::intrinsicCost(rev, tx);
    }

    /// The EIP-1559 effective gas price with the OP zero-price override (ported
    /// OpHost::get_tx_context ①): a zero max price — deposits and zero-fee txs —
    /// collapses to GASPRICE 0 instead of the base fee.
    uint256 effectiveGasPrice(uint256 maxGasPrice, uint256 maxPriorityGasPrice,
        eth::EthBlockInfo const& block, evmc_revision rev, uint256& priorityGasPriceOut) const
    {
        if (maxGasPrice == 0)
        {
            priorityGasPriceOut = 0;
            return 0;
        }
        return eth::EthL1Policy::effectiveGasPrice(
            maxGasPrice, maxPriorityGasPrice, block, rev, priorityGasPriceOut);
    }

    /// OP Stack Ecotone+: the L2 serves no blobs, so BLOBBASEFEE (0x4a) must always push 1
    /// (EIP-4844 MIN_BLOB_GASPRICE) — never the L1 blob market price, and never 0. The OP
    /// block-info builder never sets blob_base_fee; the assert pins that invariant so a
    /// future path that populates the field cannot silently change BLOBBASEFEE semantics.
    uint256 blobBaseFee(eth::EthBlockInfo const& block) const noexcept
    {
        assert(!block.blob_base_fee.has_value());
        return block.blob_base_fee.value_or(1);
    }

    /// Up-front fee withholding (ported opTransition's buyGas): the gas prepayment plus
    /// the L1 data fee and the operator fee at gas_limit. @return tx_max_cost.
    ///
    /// The subtractions are unchecked, and what makes them safe lives in
    /// validateTransaction: the 512-bit cap has already verified
    ///   balance >= gasLimit*maxGasPrice + value + l1Cost + opCost(gasLimit).
    /// That comparison always runs. What is deducted here is gasLimit*effective + l1 +
    /// opCost(gasLimit) with effective <= maxGasPrice, so the amount taken never exceeds
    /// the bound that was checked. Changing either side means changing both.
    uint256 withholdUpfront(eth::EthAccount& senderAcc, protocol::Transaction const& /*tx*/,
        eth::EthBlockInfo const& /*block*/, evmc_revision /*rev*/, int64_t gasLimit,
        uint256 effectiveGasPrice) const
    {
        const auto tx_max_cost = uint256(static_cast<uint64_t>(gasLimit)) * effectiveGasPrice;

        assert(m_snapshot != nullptr);  // normal-tx mode
        senderAcc.balance -= tx_max_cost;
        senderAcc.balance -= intxToBcosU256(m_snapshot->l1_cost);
        if (m_snapshot->has_operator_fee)
            senderAcc.balance -= intxToBcosU256(m_snapshot->operator_cost_at_gas_limit);
        return tx_max_cost;
    }

    /// Post-execution fee settlement (ported opTransition's tail): the L1 refund/floor
    /// math, then OP's vault routing — base fee to OP_BASE_FEE_VAULT, the L1 data fee to
    /// OP_L1_FEE_VAULT, and the operator fee at gas_used to OP_OPERATOR_FEE_VAULT with the
    /// pre-charge difference refunded to the sender. @return the final gas_used.
    template <class Storage>
    int64_t settleFees(eth::EthereumState<Storage>& state, eth::EthBlockInfo const& block,
        evmc_revision rev, int64_t minGasCost, int64_t gasLimit, int64_t gasLeft,
        int64_t delegationRefund, int64_t evmRefund, uint256 txMaxCost, uint256 effectiveGasPrice,
        uint256 priorityGasPrice, eth::EthAccount& senderAcc) const
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

        // Operator fee: charge the vault with the SAME formula/params that priced the
        // sender's pre-charge (operator_cost_at_gas_limit), taken from the validate-time
        // snapshot — NOT from this call's spec. This makes the two sides conserve even if
        // validate and transition straddle a fork boundary: cap = opCost(gas_limit,
        // snapshot), used = opCost(gas_used, snapshot); opCost is monotonic in gas and
        // gas_used <= gas_limit, so cap >= used holds and the refund never underflows.
        auto& snapshot = *m_snapshot;
        const auto opAtUsed = snapshot.has_operator_fee ?
                                  computeOperatorCost(snapshot.fee,
                                      static_cast<uint64_t>(gas_used),
                                      snapshot.jovian_operator_formula) :
                                  intx::uint256{0};
        const auto base_fee = (rev >= EVMC_LONDON) ? block.base_fee : 0;
        state.touch(OP_BASE_FEE_VAULT).balance +=
            uint256(static_cast<uint64_t>(gas_used)) * uint256{base_fee};
        state.touch(OP_L1_FEE_VAULT).balance += intxToBcosU256(snapshot.l1_cost);
        if (snapshot.has_operator_fee)
        {
            state.touch(OP_OPERATOR_FEE_VAULT).balance += intxToBcosU256(opAtUsed);
            assert(snapshot.operator_cost_at_gas_limit >= opAtUsed);
            senderAcc.balance += intxToBcosU256(snapshot.operator_cost_at_gas_limit - opAtUsed);
        }
        snapshot.operator_fee_at_used = opAtUsed;
        return gas_used;
    }

    /// OP precompile dispatch (ported OpHost::call's override interception).
    /// Override-table addresses count as precompiles even where the revision's own
    /// table disagrees (0x100 p256verify: Prague's is_precompile says false, OP
    /// enables it from Fjord).
    bool isPrecompile(evmc_revision rev, evmc::address const& addr) const noexcept
    {
        return findOpPrecompileOverride(m_spec.precompile_overrides, addr) != nullptr ||
               eth::evm::is_precompile(rev, addr);
    }

    /// Execute the precompile for @p msg (ported OpHost::call's override semantics):
    /// the input-size cap rejects with {EVMC_FAILURE, 0} BEFORE any gas accounting; a
    /// gas-override entry (only 0x100 p256verify @3450) self-dispatches with op-geth's
    /// gas semantics (failure keeps the remaining gas, unlike evmone's generic
    /// call_precompile which zeroes it); length-limit-only entries fall through to the
    /// revision's own table.
    evmc::Result callPrecompile(evmc_revision rev, evmc_message const& msg) const noexcept
    {
        const auto* entry = findOpPrecompileOverride(m_spec.precompile_overrides, msg.code_address);
        if (entry == nullptr)
            return eth::evm::call_precompile(rev, msg);

        if (entry->max_input_size > 0 && static_cast<size_t>(msg.input_size) > entry->max_input_size)
            return evmc::Result{EVMC_FAILURE, 0};

        // Length-limit-only (0x08 / BLS): the base table handles pricing and execution.
        if (entry->gas_cost_override < 0)
            return eth::evm::call_precompile(rev, msg);

        // Gas-override (0x100): the base table's is_precompile(PRAGUE) is false, so the
        // override dispatches it itself (ported executeGasOverridePrecompile).
        if (msg.gas < entry->gas_cost_override)
            return evmc::Result{EVMC_OUT_OF_GAS, 0};
        const int64_t gas_left = msg.gas - entry->gas_cost_override;

        // The gas-override branch currently implements only P256Verify; any future additional
        // gas-override address must be given an explicit branch here -- better to fail loudly
        // than to silently run P256Verify logic against an unknown address.
        if (entry->addr != OP_P256_VERIFY_ADDRESS)
        {
            assert(false && "unhandled gas-override precompile address");
            return evmc::Result{EVMC_INTERNAL_ERROR, gas_left};
        }

        constexpr size_t kMaxOutput = 32;
        // Heap buffer with a release callback, matching eth::evm::call_precompile's
        // ownership convention (the caller's evmc::Result releases it).
        const auto output_data = new (std::nothrow) uint8_t[kMaxOutput];
        const auto exec = eth::evm::p256verify_execute(
            msg.input_data, msg.input_size, output_data, kMaxOutput);

        if (exec.status_code == EVMC_SUCCESS && exec.output_size > 0)
        {
            const evmc_result result{EVMC_SUCCESS, gas_left, 0, output_data, exec.output_size,
                [](const evmc_result* res) noexcept { delete[] res->output_data; },
                evmc::address{},  // create_address
                {}                // padding
            };
            return evmc::Result{result};
        }
        delete[] output_data;
        // op-geth charges only the 3450 on an input error — the remaining gas survives
        // (unlike evmone's generic path, which zeroes it).
        return evmc::Result{exec.status_code, gas_left};
    }

    /// Addresses in the override table are always warm: op-geth statedb.Prepare warms
    /// every active precompile (Isthmus includes 0x100, whereas the base table gates
    /// 0x100 at OSAKA); the hook also avoids the ghost empty account that the host's
    /// get_or_insert(erase_if_empty) would otherwise push into the state diff.
    bool isAlwaysWarmPrecompile(evmc_revision /*rev*/, evmc::address const& addr) const noexcept
    {
        return findOpPrecompileOverride(m_spec.precompile_overrides, addr) != nullptr;
    }

    /// The OP receipt shape (ported opTransition's projection): FISCO 0/1 status, the
    /// return-data output, the 256-byte logsBloom (encodeReceiptLeaf requires it), the
    /// creation contractAddress, the OP metadata snapshot and the hex effective gas price.
    template <class Host>
    protocol::TransactionReceipt::Ptr buildReceipt(Host& host, evmc::Result const& result,
        int64_t gasUsed, protocol::TransactionReceiptFactory const& rf, int64_t blockNumber) const
    {
        // gas_used is int64_t; a runtime non-negative check guards the cast against wrap
        // (evmone guarantees it; under NDEBUG the assert is gone and the consensus surface
        // tolerates no wrap).
        if (gasUsed < 0)
            throw std::runtime_error("opTransition: negative gas_used");

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
        // Guard output_data nullptr (void-return calls): `nullptr + 0` is UB.
        const bcos::bytes outputBytes{result.output_size != 0 ?
                                          bcos::bytes{result.output_data,
                                              result.output_data + result.output_size} :
                                          bcos::bytes{}};
        auto out = rf.createReceipt(bcos::u256{static_cast<uint64_t>(gasUsed)},
            m_isCreate ? toFiscoContractAddress(m_sender, m_nonce) : std::string{},
            std::move(logs), toFiscoOpStatus(result.status_code),
            bcos::bytesConstRef{outputBytes.data(), outputBytes.size()}, blockNumber);
        const auto bloom = bcos::getLogsBloom(out->logEntries());
        out->setLogsBloom(bcos::bytesConstRef{bloom.data(), bloom.size()});

        // Both the meta and the effective price describe what the transaction was actually
        // priced and charged under — the validate-time snapshot, never a fresh fork spec.
        auto meta =
            deriveOpReceiptMeta(*m_snapshot, m_snapshot->operator_fee_at_used,
                /*fill_operator_scalars=*/true);
        out->setOpStackMeta(toOpStackMeta(meta));

        uint256 priorityGasPrice;
        const auto effectivePrice = effectiveGasPrice(
            m_maxGasPrice, m_maxPriorityGasPrice, m_block, m_spec.rev, priorityGasPrice);
        // "0x" + lowercase hex, no leading zeros (op-geth hexutil.Big).
        out->setEffectiveGasPrice("0x" + intx::to_string(bcosU256ToIntx(effectivePrice), 16));
        return out;
    }

private:
    OpForkSpec m_spec{};
    OpFeeParams m_fee{};
    eth::EthBlockInfo const& m_block;
    evmc::bytes_view m_envelope{};
    // Null in deposit mode; the normal-tx hooks (additionalMaxCost /
    // withholdUpfront / settleFees / buildReceipt) require it.
    OpTxSnapshot* m_snapshot = nullptr;
    evmc::address m_sender{};
    uint64_t m_nonce = 0;
    bool m_isCreate = false;
    uint256 m_maxGasPrice = 0;
    uint256 m_maxPriorityGasPrice = 0;
};
}  // namespace bcos::executor_v1::opstack
