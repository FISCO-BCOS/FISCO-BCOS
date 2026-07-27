#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpBlock.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/eth/state/system_contracts.hpp>
// TODO(eth-utils-removal): mpt/rlp(eth/utils)→自研 MPT + bcos-codec/rlp/RLPEncode.h。
// 注意:opStorageRoot 与 receipts_root 循环照抄自 evmone test/utils/mpt_hash.cpp
// (官方 v0.21.0),替换后须重验 33/33 向量。
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <test/utils/mpt.hpp>
#include <test/utils/rlp.hpp>

namespace bcos::evm::opstack
{
namespace
{
[[nodiscard]] bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    // stricter-than-spec (spec §6 decision point 2, user ruling): validate by content. Spec
    // constants for cross-checking against op-node derive/l1_block_info.go:40 (DEPOSITOR); op-geth
    // EL does not perform this validation (responsibility pushed down to the CL layer).
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}
}  // namespace

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // §4.1 step 1: pre-block system call (4788/2935; revision-gating and silent skip on missing
    // code are both handled inside evmone).
    applyDiff(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // §4.1 step 2 precondition: the first tx must be the L1 attributes deposit (stricter-than-spec,
    // spec §6 decision point 1/2).
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !isL1AttributesTx(*firstDep))
        throw std::runtime_error("op block: first tx is not the L1 attributes deposit");

    OpBlockResult result;
    result.receipts.reserve(txs.size());
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    OpFeeParams fee{};

    for (const auto& btx : txs)
    {
        if (const auto* dep = std::get_if<DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                throw std::runtime_error("op block: deposit after non-deposit tx");
            auto receipt = runDeposit(view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft);
            applyDiff(receipt.receipt.state_diff);
            blockGasLeft -= receipt.receipt.gas_used;
            cumulative += receipt.receipt.gas_used;
            receipt.receipt.cumulative_gas_used = cumulative;
            result.receipts.emplace_back(std::move(receipt));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // §4.1 step 2: fee params are read from the storage slot values after this block's
                // attributes tx executes; deferred lazily to the first normal tx, equivalent to
                // op-geth's per-block cache (rollup_cost.go:162-164/:199-207).
                fee = loadOpFeeParams(view);
                feeLoaded = true;
            }
            const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
            const evmc::bytes_view env{btx.signedEnvelope.data(), btx.signedEnvelope.size()};
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
                // op-geth: a normal tx that fails validation has no failed-receipt mechanism;
                // Process voids the whole block outright (state_transition preCheck →
                // state_processor.go:109-113).
                throw std::runtime_error("op block: invalid non-deposit tx: " + err->message());
            auto receipt = opTransition(
                view, block, hashes, tx, cfg, vm, std::get<OpTxProperties>(v), chainId, env);
            applyDiff(receipt.receipt.state_diff);
            blockGasLeft -= receipt.receipt.gas_used;
            cumulative += receipt.receipt.gas_used;
            receipt.receipt.cumulative_gas_used = cumulative;
            result.receipts.emplace_back(std::move(receipt));
        }
    }

    // §4.1 step 4: end-of-block finalize (D-10 wiring closure point).
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiff(result.finalizeDiff);

    result.gasUsed = cumulative;
    return result;
}

evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        throw std::invalid_argument("op finalize: prague requests unsupported on OP chains");
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {}));
}

evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Aligned with evmone mpt_hash.cpp:13-24 (private helper, not exported): secure-trie key,
    // trimmed value; upstream asserts that zero values are removed beforehand, here a defensive
    // continue — semantically equivalent to "called after removal".
    evmone::state::MPT trie;
    for (const auto& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        trie.insert(evmone::keccak256(evmc::bytes_view(key)),
            evmone::rlp::encode(evmone::rlp::trim(evmc::bytes_view(value))));
    }
    return trie.hash();
}

OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage)
{
    OpBlockSeal seal{};

    // receipts-root: key = rlp(index), leaf = EncodeIndex-semantics encoding (Task 1).
    // Structurally aligned with evmone's list-trie template (mpt_hash.cpp:38-46); that template
    // takes the leaf value via rlp_encode(T) and is only explicitly instantiated for 3 upstream
    // types, so the OP custom leaf encoding cannot reuse it — hence these 4 lines are reproduced
    // here.
    evmone::state::MPT trie;
    for (size_t i = 0; i < result.receipts.size(); ++i)
        trie.insert(evmone::rlp::encode(i), encodeReceiptForRoot(result.receipts[i]));
    seal.receiptsRoot = trie.hash();

    // Block-level logsBloom: bitwise-OR each receipt's bloom (aligned with the
    // span<TransactionReceipt> overload in bloom_filter.cpp:46-53; a variant sequence cannot be fed
    // to it directly).
    for (const auto& r : result.receipts)
    {
        const auto& bloom = std::visit(
            [](const auto& x) -> const evmone::state::BloomFilter& {
                return x.receipt.logs_bloom_filter;
            },
            r);
        std::transform(std::begin(seal.logsBloom.bytes), std::end(seal.logsBloom.bytes),
            std::begin(bloom.bytes), std::begin(seal.logsBloom.bytes), std::bit_or<>());
    }

    // withdrawalsRoot / requestsHash: semantics switch starting at Isthmus (pinned by spec §4.2
    // rev.2).
    if (cfg.fork >= OpFork::Isthmus)
    {
        seal.withdrawalsRoot = opStorageRoot(messagePasserStorage);
        seal.requestsHash = OP_EMPTY_REQUESTS_HASH;
    }
    else
    {
        // Canyon+ withdrawals list is always empty → empty-trie root; the requests header field
        // does not exist in the CANCUN family
        seal.withdrawalsRoot = evmone::state::EMPTY_MPT_HASH;
    }

    // Jovian block-header BlobGasUsed reuse slot (equivalent to CalcDAFootprint, see header
    // comment; reclaimed by M-B2 decision record 2)
    if (cfg.has_da_footprint)
    {
        uint64_t footprint = 0;
        for (const auto& r : result.receipts)
            if (const auto* txr = std::get_if<OpTxReceipt>(&r))
                footprint += txr->meta.da_footprint.value_or(0);
        seal.blobGasUsed = footprint;
    }
    return seal;
}
}  // namespace bcos::evm::opstack
