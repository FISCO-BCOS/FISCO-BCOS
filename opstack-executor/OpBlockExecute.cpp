#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-rlp-protocol/Web3TxEnvelope.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpstackExecutor.h>  // envelopeExecutionFieldsMismatch (shared gate)
#include <algorithm>
#include <bcos-evm/eth/state/state.hpp>  // evmone::state::finalize
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <charconv>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <system_error>

// isL1AttributesTx lives in OpBlockExecute.h; narrowGasUsed / decimalCumulative live in OpCommon.h
// (both shared with the per-tx loop — one implementation, no copy drift).

namespace bcos::evm::opstack
{
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg)
{
    if (!cfg.has_da_footprint)  // Jovian-only (op-geth CalcDAFootprint)
        return;
    if (txs.empty())  // rejected by processOpBlock anyway
        return;
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        return;
    validateJovianL1AttributesShape(
        std::span<uint8_t const>{firstDep->data.data(), firstDep->data.size()},
        std::holds_alternative<DepositTx>(txs.back().tx), cfg);
}

evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        // runtime_error (not logic_error): block-level rejection (INVALID), not a local fault.
        throw OpConsensusError("op finalize: prague requests unsupported on OP chains");
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {}));
}

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // Storage write-back failures must leave as OpStorageError (-32603), never a bare
    // runtime_error — the same classification the per-tx path applies in m_finish /
    // executeDeposit / finalizeBlock (Storage2State::applyDiff poisons AND rethrows raw).
    // Without this normalization, a storage fault on the block path would escape unclassified
    // and diverge from the documented error contract.
    auto applyDiffChecked = [&](const evmone::state::StateDiff& diff) {
        try
        {
            applyDiff(diff);
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw bcos::evm::engine::OpStorageError(
                std::string("op block: storage write-back failed: ") + e.what());
        }
        catch (...)
        {
            throw bcos::evm::engine::OpStorageError(
                "op block: storage write-back failed: unknown exception");
        }
    };

    // Step 1: pre-block system call (4788/2935; gating/skip handled inside evmone).
    applyDiffChecked(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // Step 2: first tx must be a deposit (hard reject) + L1-attributes content (warn, op-geth
    // accept-at-validation) + Jovian shape. Accept set mirrors preBlockOpSteps / ExecuteContext;
    // seenNonDeposit is set pre-validation here while ExecuteContext sets it after a successful
    // prepare — any validation failure aborts the whole block on this path, so the timing
    // difference has no effect.
    if (txs.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        throw OpConsensusError("op block: first tx is not a deposit");
    if (!isL1AttributesTx(*firstDep))
        BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                          << "op block: first tx is a deposit but not the L1 attributes tx — "
                             "accepted";
    validateJovianBlockShape(txs, cfg);

    OpBlockResult result;
    result.receipts.reserve(txs.size());
    result.txTypes.reserve(txs.size());
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    OpFeeParams fee{};

    size_t transactionIndex = 0;
    for (const auto& btx : txs)
    {
        if (const auto* dep = std::get_if<DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                                  << "deposit after non-deposit in block — accepted";
            evmone::state::StateDiff diff;
            auto receipt = [&]() {
                try
                {
                    return runDeposit(view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft,
                        receiptFactory, diff);
                }
                catch (const OpConsensusError&)
                {
                    throw;
                }
                catch (const std::runtime_error& e)
                {
                    throw OpConsensusError(
                        std::string("op block: deposit execution failed: ") + e.what());
                }
            }();
            applyDiffChecked(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            // Store cumulative gas as decimal; RPC parses that field as decimal.
            receipt->setCumulativeGasUsed(decimalCumulative(static_cast<uint64_t>(cumulative)));
            receipt->setTransactionIndex(transactionIndex++);
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(classifyTxType(static_cast<uint8_t>(kDepositTxType)));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // Fee params lazily loaded at the first normal tx (op-geth's per-block cache).
                // loadOpFeeParams reads the L1Block predeploy storage slots — same source as
                // op-geth's rollup cost, which also reads the L1Block predeploy (written from
                // the L1 attributes deposit) rather than re-parsing the deposit calldata. The
                // Jovian-only DA scalar is the one deliberate exception: it is read directly
                // from calldata[176:178] so it stays authoritative even if the attributes
                // deposit rolled back L1Block slot8; the activation block (176B) forces 0.
                fee = loadOpFeeParams(view);
                if (cfg.has_da_footprint)
                {
                    const auto& attrData = std::get<DepositTx>(txs[0].tx).data;
                    if (auto scalar = jovianDaFootprintGasScalar(
                            std::span<uint8_t const>{attrData.data(), attrData.size()}))
                        fee.da_footprint_gas_scalar = *scalar;
                }
                feeLoaded = true;
            }
            const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
            const evmc::bytes_view env{btx.signedEnvelope.data(), btx.signedEnvelope.size()};
            auto const envRef =
                bcos::bytesConstRef(btx.signedEnvelope.data(), btx.signedEnvelope.size());
            // Pool eviction keys on OpConsensusError::txHash (keccak of the signed envelope),
            // never a substring of what(). That hash equals Transaction::hash() for Web3 txs.
            // `validateErrorCode` carries the opValidate table's typed classification for
            // validate-class rejects — empty for every other reject shape.
            auto rejectNonDeposit = [&](std::string message,
                                        std::error_code validateErrorCode = {}) {
                OpConsensusError err(std::move(message));
                err.txHash = bcos::crypto::keccak256Hash(envRef);
                err.validateErrorCode = std::move(validateErrorCode);
                throw err;
            };
            if (auto mismatch =
                    bcos::executor_v1::opstack::envelopeChainIdMismatch(envRef, chainId))
            {
                rejectNonDeposit("op block: " + *mismatch);
            }
            // Fail-closed mirror↔envelope cross-check — the SAME gate the per-tx path runs in
            // m_prepare (OpstackExecutor.h): execution fields (nonce/gasLimit/to/value/data)
            // must match the signed envelope, never the forgeable mirror. txTypes committed to
            // the receipts root below depend on tx.type, so an unbound mirror here would poison
            // the block's header commitment exactly like the per-tx path.
            if (auto mismatch =
                    bcos::executor_v1::opstack::envelopeExecutionFieldsMismatch(envRef, tx))
            {
                rejectNonDeposit(
                    "op block: tx execution fields diverge from the signed envelope: " + *mismatch);
            }
            if (auto missing = bcos::executor_v1::opstack::blockPathZeroSender(tx.sender))
            {
                rejectNonDeposit("op block: " + *missing);
            }
            if (auto unbound = bcos::executor_v1::opstack::blockPathUnboundAuthorizationList(tx))
            {
                rejectNonDeposit("op block: " + *unbound);
            }
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
            {
                // A full remaining-gas pool is a capacity fault, not a poisoned tx —
                // the same classification m_prepare gives GAS_LIMIT_REACHED on the
                // per-tx path (OpBlockGasPoolFull): the tx is VALID but does not fit
                // this candidate and must stay pooled for a later block. The txHash
                // tag names the culprit whose bytes (plus its sender's nonce tail)
                // the wired build loop trims from THIS candidate; `capacity` alone
                // decides evict-vs-skip (keep the tx pooled). Without the tag the
                // consumer's only skip path is unreachable: it would fall through to
                // the internal-error throw and forkchoiceUpdated would answer
                // -32603 for a full gas pool on every retry.
                // The block itself is still voided — op-geth has no failed-receipt
                // mechanism for normal txs.
                if (*err == evmone::state::make_error_code(evmone::state::GAS_LIMIT_REACHED))
                {
                    OpConsensusError capacityFault(
                        "op block: tx does not fit the remaining block gas");
                    capacityFault.txHash = bcos::crypto::keccak256Hash(envRef);
                    capacityFault.capacity = true;
                    throw capacityFault;
                }
                // No failed-receipt mechanism for normal txs: void the whole block (op-geth).
                // The classification survives as the typed `validateErrorCode` field on the
                // thrown OpConsensusError, not only as message text.
                rejectNonDeposit("op block: invalid non-deposit tx: " + err->message(), *err);
            }
            // opTransition charges from props.fee (the validate-time snapshot — no second read).
            evmone::state::StateDiff diff;
            auto receipt = [&]() {
                try
                {
                    return opTransition(view, block, hashes, tx, cfg, vm,
                        std::get<OpTxProperties>(v), chainId, receiptFactory, diff);
                }
                catch (const OpConsensusError&)
                {
                    throw;
                }
                catch (const std::runtime_error& e)
                {
                    // Tag with the signed envelope hash: the build loop keys culprit
                    // eviction on OpConsensusError::txHash, so an untagged throw leaves the
                    // tx in the pool and the next forkchoiceUpdated selects it again. The
                    // per-tx path (rethrowExecError) needs no tag because it has no eviction
                    // loop. Capacity / validate rejects above stay tagged too.
                    OpConsensusError err(
                        std::string("op block: transaction execution failed: ") + e.what());
                    err.txHash = bcos::crypto::keccak256Hash(envRef);
                    throw err;
                }
            }();
            applyDiffChecked(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(decimalCumulative(static_cast<uint64_t>(cumulative)));
            receipt->setTransactionIndex(transactionIndex++);
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(classifyTxType(static_cast<uint8_t>(tx.type)));
        }
    }

    // Step 4: end-of-block finalize.
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiffChecked(result.finalizeDiff);

    result.gasUsed = cumulative;
    return result;
}

// ---- block-header seal ----

evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Secure trie over the live slot map (key = keccak256(slot), leaf = rlp(trimmed value)).
    std::map<bcos::h256, bcos::bytes> entries;
    for (const auto& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        size_t first = 0;
        while (first < sizeof(value.bytes) && value.bytes[first] == 0)
        {
            ++first;
        }
        bcos::bytes leaf;
        bcos::codec::rlp::encode(
            leaf, bcos::bytes(value.bytes + first, value.bytes + sizeof(value.bytes)));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    evmone::hash256 root{};
    std::memcpy(root.bytes, result.root.data(), sizeof(root.bytes));
    return root;
}

bcos::bytes encodeReceiptForRoot(const bcos::protocol::TransactionReceipt& r, uint8_t txType)
{
    // The encoder lives in ledger/mpt so the engine's header paths (EngineServiceImpl /
    // EthEngineService) and this block seal share one implementation; translate its
    // malformed-receipt error into the consensus-rejection type this path's callers map.
    try
    {
        return bcos::ledger::mpt::encodeReceiptLeaf(r, txType);
    }
    catch (bcos::ledger::mpt::EthReceiptEncodeError const& e)
    {
        throw OpConsensusError("op block: " + std::string(e.what()));
    }
}

OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage)
{
    // Guard before any indexing: a caller that builds an OpBlockResult by hand (part-5's
    // OpScheduler, or a test) could otherwise feed mismatched receipts/txTypes — txTypes[i] is
    // indexed below and its bytes become the receipts-root leaf's type prefix, so a length
    // mismatch would silently produce a wrong header commitment (an out-of-bounds read). This is
    // a caller programming error (internal invariant), not a block-content rejection — mapped to
    // std::logic_error, never INVALID.
    if (result.txTypes.size() != result.receipts.size())
        throw std::logic_error("op block: receipts/txTypes length mismatch (caller bug)");
    OpBlockSeal seal{};

    // receipts-root: index-keyed trie over the RLP receipt leaves (op-geth Receipts.EncodeIndex).
    // Shared with the engine header paths through ledger/mpt::calculateReceiptsRoot so the two
    // producers cannot drift on the root construction; encodeReceiptForRoot owns the leaf bytes.
    std::vector<bcos::bytes> receiptLeaves;
    receiptLeaves.reserve(result.receipts.size());
    for (size_t i = 0; i < result.receipts.size(); ++i)
    {
        receiptLeaves.push_back(encodeReceiptForRoot(*result.receipts[i], result.txTypes[i]));
    }
    std::vector<bcos::bytesConstRef> receiptLeafRefs;
    receiptLeafRefs.reserve(receiptLeaves.size());
    for (auto const& leaf : receiptLeaves)
    {
        receiptLeafRefs.emplace_back(leaf.data(), leaf.size());
    }
    auto const receiptsRoot = bcos::ledger::mpt::calculateReceiptsRoot(receiptLeafRefs);
    std::memcpy(seal.receiptsRoot.bytes, receiptsRoot.data(), sizeof(seal.receiptsRoot.bytes));

    // Block-level logsBloom = bitwise-OR of each receipt's 256-byte bloom.
    for (const auto& r : result.receipts)
    {
        const auto bloom = r->logsBloom();
        if (bloom.size() != 256)
        {
            throw OpConsensusError("op block: receipt logsBloom must be 256 bytes, got " +
                                   std::to_string(bloom.size()));
        }
        for (size_t i = 0; i < 256; ++i)
            seal.logsBloom.bytes[i] |= bloom[i];
    }

    // Isthmus+: withdrawalsRoot = MessagePasser storage root, requestsHash = sha256("").
    // Pre-Isthmus: withdrawals list is always empty → empty-trie root; no requests field.
    if (cfg.fork >= OpFork::Isthmus)
    {
        seal.withdrawalsRoot = opStorageRoot(messagePasserStorage);
        seal.requestsHash = OP_EMPTY_REQUESTS_HASH;
    }
    else
    {
        auto const emptyRoot = bcos::ledger::mpt::emptyRootHash();
        std::memcpy(
            seal.withdrawalsRoot.bytes, emptyRoot.data(), sizeof(seal.withdrawalsRoot.bytes));
    }

    // Jovian: header blobGasUsed slot = DA footprint (Σ da_footprint over non-deposit receipts).
    // Deposits legitimately carry nullopt and are skipped. A missing optional on a non-deposit
    // receipt must not silently contribute 0 — that under-counts the header commitment the
    // same way a missing deposit nonce used to under-encode the receipts-root leaf.
    if (cfg.has_da_footprint)
    {
        uint64_t footprint = 0;
        for (size_t i = 0; i < result.receipts.size(); ++i)
        {
            if (result.txTypes[i] == static_cast<uint8_t>(kDepositTxType))
                continue;
            const auto& meta = result.receipts[i]->opStackMeta();
            if (!meta || !meta->da_footprint)
                throw OpConsensusError(
                    "op block: non-deposit receipt missing da_footprint under Jovian");
            const auto term = *meta->da_footprint;
            if (footprint > std::numeric_limits<uint64_t>::max() - term)
                throw OpConsensusError("op block: DA footprint overflows uint64");
            footprint += term;
        }
        seal.blobGasUsed = footprint;
    }
    return seal;
}
}  // namespace bcos::evm::opstack
