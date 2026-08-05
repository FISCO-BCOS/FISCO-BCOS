#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-evm/opstack/OpReceipt.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <algorithm>
#include <cstring>
#include <functional>

namespace bcos::evm::opstack
{
evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Secure-trie over one account's live slot map, built with FISCO computeTrieRoot (same
    // construction as the retired evmone mpt_hash.cpp:13-24: key = keccak256(slot), value =
    // rlp(trimmed value)). Defensive continue on zero values.
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
        bcos::bytes leaf(value.bytes + first, value.bytes + sizeof(value.bytes));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    evmone::hash256 root{};
    std::memcpy(root.bytes, result.root.data(), sizeof(root.bytes));
    return root;
}

OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage)
{
    OpBlockSeal seal{};

    // receipts-root: key = rlp(index), leaf = EncodeIndex-semantics encoding. Built with FISCO
    // computeTrieRootVarKey (variable-length keys rlp(0..N), ascending, non-prefix-of-each-other —
    // the same construction as the retired evmone list-trie mpt_hash.cpp:38-46).
    std::vector<std::pair<bcos::bytes, bcos::bytes>> receiptsEntries;
    receiptsEntries.reserve(result.receipts.size());
    for (size_t i = 0; i < result.receipts.size(); ++i)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, static_cast<uint64_t>(i));
        auto const leaf = encodeReceiptForRoot(result.receipts[i]);
        receiptsEntries.emplace_back(std::move(key), bcos::bytes{leaf.begin(), leaf.end()});
    }
    auto receiptsResult = bcos::ledger::mpt::computeTrieRootVarKey(receiptsEntries);
    std::memcpy(seal.receiptsRoot.bytes, receiptsResult.root.data(), sizeof(seal.receiptsRoot.bytes));

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
        // does not exist in the CANCUN family. FISCO emptyRootHash() == keccak256(RLP("")).
        auto const emptyRoot = bcos::ledger::mpt::emptyRootHash();
        std::memcpy(seal.withdrawalsRoot.bytes, emptyRoot.data(), sizeof(seal.withdrawalsRoot.bytes));
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
