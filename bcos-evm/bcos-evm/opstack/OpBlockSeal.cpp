#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-evm/opstack/OpReceiptEncode.h>
#include <algorithm>
// TODO(eth-utils-removal): mpt/rlp(eth/utils)→自研 MPT + bcos-codec/rlp/RLPEncode.h。
// 注意:opStorageRoot 与 receipts_root 循环属 upstream-diff 追踪段(op_storage_root/
// receipts_root_loop),替换后须 --regenerate-goldens 并重验 33/33 向量。
#include <bcos-evm/eth/utils/mpt.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <functional>

namespace bcos::evm::opstack
{
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
    // here (added to the manifest).
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
