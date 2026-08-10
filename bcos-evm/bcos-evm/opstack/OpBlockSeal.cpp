#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <algorithm>
#include <cstring>
#include <functional>

namespace bcos::evm::opstack
{
namespace
{
/// Parse the receipt's cumulativeGasUsed string (set by processOpBlock as "0x" + lowercase hex,
/// op-geth hexutil.Uint64 convention) back to the exact uint64 the EncodeIndex leaf needs.
[[nodiscard]] uint64_t parseHexUint64(std::string_view s)
{
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s.remove_prefix(2);
    uint64_t v = 0;
    for (const char c : s)
    {
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<uint8_t>(c - 'A' + 10);
        else
            throw std::runtime_error(
                "op block: invalid cumulativeGasUsed in receipt (not hex): " + std::string(s));
    }
    return v;
}

/// Appends the RLP list of logs: for each log, [address(20B), [topics...], data]; the whole
/// collection is itself wrapped in a list. Byte-identical to evmone's rlp::encode_container over
/// std::vector<Log> (rlp.hpp encode_container + rlp_encode(Log) = encode_tuple(addr, topics,
/// data)); rebuilt from bcos::protocol::LogEntry (the same raw bytes mapOpLogs stored).
inline void encodeLogsList(bcos::bytes& to, gsl::span<const bcos::protocol::LogEntry> logs)
{
    bcos::bytes content;
    for (const auto& log : logs)
    {
        bcos::bytes entry;
        // address (raw 20 bytes, LogEntry::address() reinterprets as binary, not hex)
        bcos::codec::rlp::encode(entry, log.address());
        // topics list
        bcos::bytes topics;
        for (const auto& topic : log.topics())
            bcos::codec::rlp::encode(topics, topic);  // h256 -> 32-byte string
        bcos::codec::rlp::encodeHeader(entry, {.isList = true, .payloadLength = topics.size()});
        entry.insert(entry.end(), topics.begin(), topics.end());
        // data
        bcos::codec::rlp::encode(entry, log.data());
        // wrap one log entry as a list
        bcos::codec::rlp::encodeHeader(content, {.isList = true, .payloadLength = entry.size()});
        content.insert(content.end(), entry.begin(), entry.end());
    }
    // wrap the log collection as a list
    bcos::codec::rlp::encodeHeader(to, {.isList = true, .payloadLength = content.size()});
    to.insert(to.end(), content.begin(), content.end());
}
}  // namespace

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
        // ⚠️ op-geth 的 storage-trie leaf value = rlp(trimmed value)（trie/secure_trie.go
        // UpdateStorage: v,_ := rlp.EncodeToBytes(value) 后再入 trie），即 leaf 内是
        // rlp(trim) 的字节串，相对 trim 是二次 RLP。本函数旧实现把 raw trim 直接当 leaf value，
        // 单槽时产出 d00be84d… 而非 op-geth 的 02dffd0c…（W6 L2 message_passer_write 暴露）。
        // 与 accountStorageRoot（adapter/StateRootCompute.cpp:21-22，stateRoot 据此匹配 golden）
        // 对齐：这里先把 trim 值 rlp 编码成 leaf 字节串。
        bcos::bytes leaf;
        bcos::codec::rlp::encode(leaf,
            bcos::bytes(value.bytes + first, value.bytes + sizeof(value.bytes)));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    evmone::hash256 root{};
    std::memcpy(root.bytes, result.root.data(), sizeof(root.bytes));
    return root;
}

evmc::bytes encodeReceiptForRoot(const bcos::protocol::TransactionReceipt& r, uint8_t txType)
{
    // FISCO 0 == success. RLP bool semantics (op-geth statusEncoding): true → 0x01, false → 0x80
    // (empty string). Pushed manually because bcos::codec::rlp::encode's UnsignedByte path
    // instantiates toCompactBigEndian(bool|byte), which is either shift-overflow or
    // -Wundefined-inline; the value space is exactly {0x01, 0x80} so a raw push is equivalent.
    const bool success = (r.status() == 0);
    const uint64_t cumGas = parseHexUint64(r.cumulativeGasUsed());
    const auto bloom = r.logsBloom();
    const auto logs = r.logEntries();

    // payload = rlp([status, cumGas, bloom, logs]) + (deposit) [nonce, version]
    bcos::bytes payload;
    payload.push_back(success ? 0x01 : 0x80);
    bcos::codec::rlp::encode(payload, cumGas);
    bcos::codec::rlp::encode(payload, bloom);
    encodeLogsList(payload, logs);

    if (txType == static_cast<uint8_t>(kDepositTxType))
    {
        const auto& meta = r.opStackMeta();
        const uint64_t nonce =
            (meta && meta->deposit_nonce) ? *meta->deposit_nonce : uint64_t{0};
        const uint64_t version = (meta && meta->deposit_receipt_version) ?
                                     *meta->deposit_receipt_version :
                                     uint64_t{0};
        bcos::codec::rlp::encode(payload, nonce);
        bcos::codec::rlp::encode(payload, version);
    }

    bcos::bytes out;
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    // typed raw-byte prefix (legacy has none; deposit's 0x7e is the EIP-2718 type byte).
    if (txType != static_cast<uint8_t>(evmone::state::Transaction::Type::legacy))
        out.insert(out.begin(), txType);
    return evmc::bytes(out.begin(), out.end());
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
        auto const leaf = encodeReceiptForRoot(*result.receipts[i], result.txTypes[i]);
        receiptsEntries.emplace_back(std::move(key), bcos::bytes{leaf.begin(), leaf.end()});
    }
    auto receiptsResult = bcos::ledger::mpt::computeTrieRootVarKey(receiptsEntries);
    std::memcpy(seal.receiptsRoot.bytes, receiptsResult.root.data(), sizeof(seal.receiptsRoot.bytes));

    // Block-level logsBloom: bitwise-OR each receipt's 256-byte bloom (the FISCO receipt carries
    // its own logsBloom, set by the execution layer — same projection the evmone span overload
    // in bloom_filter.cpp:46-53 produces).
    for (const auto& r : result.receipts)
    {
        const auto bloom = r->logsBloom();
        for (size_t i = 0; i < 256 && i < bloom.size(); ++i)
            seal.logsBloom.bytes[i] |= bloom[i];
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
    // comment; reclaimed by M-B2 decision record 2). Only non-deposit receipts carry
    // da_footprint in their opStackMeta — deposits always have it nullopt, so summing over every
    // receipt is equivalent to the old OpTxReceipt-only loop.
    if (cfg.has_da_footprint)
    {
        uint64_t footprint = 0;
        for (const auto& r : result.receipts)
        {
            const auto& meta = r->opStackMeta();
            if (meta && meta->da_footprint)
                footprint += *meta->da_footprint;
        }
        seal.blobGasUsed = footprint;
    }
    return seal;
}
}  // namespace bcos::evm::opstack
