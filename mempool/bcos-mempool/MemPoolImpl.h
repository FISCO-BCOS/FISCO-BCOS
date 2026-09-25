#pragma once

#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include "bcos-framework/bcos-framework/engine/Types.h"
#include "bcos-framework/bcos-framework/protocol/BlobSchedule.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <queue>
#include <string_view>
#include <unordered_set>

namespace bcos::txpool
{

DERIVE_BCOS_EXCEPTION(InvalidNonce);
DERIVE_BCOS_EXCEPTION(InvalidTaintedTransaction);
DERIVE_BCOS_EXCEPTION(InvalidBlobTransaction);
/// tryAdd() was handed no transaction. Its siblings above describe a transaction the pool
/// refuses; this one describes a caller that has nothing to offer, which is a wiring defect and
/// not something to report back as a transaction status.
DERIVE_BCOS_EXCEPTION(NullTransaction);

struct TransactionData
{
    protocol::Transaction::Ptr m_transaction;
    int64_t m_nonce;

    int64_t importTime() const;
    crypto::HashType hash() const;
    std::string_view sender() const;
    int64_t nonce() const;

    TransactionData(protocol::Transaction::Ptr transaction);
};

/// Which chain shape the pool serves: L2 (the OP Stack default: no blob transactions, no
/// fee market) or L1 (vanilla Ethereum: blob transactions admitted, fee-market ordering
/// and replacement rules apply).
enum class ChainKind : std::uint8_t
{
    L2,
    L1,
};

struct MemPoolConfig
{
    ChainKind chainKind = ChainKind::L2;
    /// Max pooled transactions; 0 = unlimited (the L2 default). When full, the cheapest
    /// transaction by static tip key is evicted.
    std::size_t capacity = 0;
    /// Milliseconds a transaction may sit in the pool before it is evicted; 0 = never
    /// expires (the L2 default).
    int64_t txLifetimeMs = 0;
    /// EIP-4844: max blob versioned hashes one transaction may carry (L1 only).
    std::size_t maxBlobsPerTransaction = 6;
    /// When set (EL mode), the per-transaction blob limit resolves dynamically instead of
    /// reading the static field above: the EIP-7840 schedule (blobForks) is evaluated at
    /// the chain-head timestamp this provider returns, in seconds. A BPO bump activating
    /// while the node runs takes effect without a restart, and a chain still syncing
    /// behind wall-clock time is gated by the fork its head is actually on.
    std::function<uint64_t()> headTimestampSeconds{};
    protocol::BlobForkTimes blobForks{};
};

/// Static fee keys of a transaction, without the block's base fee: the most the
/// transaction can pay. BCOS-native transactions carry no fee market and key to 0.
inline u256 feeTipKey(protocol::Transaction const& transaction)
{
    if (auto const& tip = transaction.maxPriorityFeePerGas(); tip.has_value())
    {
        return *tip;
    }
    if (auto const& gasPrice = transaction.gasPrice(); gasPrice.has_value())
    {
        return *gasPrice;
    }
    return 0;
}

inline u256 feeCapKey(protocol::Transaction const& transaction)
{
    if (auto const& feeCap = transaction.maxFeePerGas(); feeCap.has_value())
    {
        return *feeCap;
    }
    if (auto const& gasPrice = transaction.gasPrice(); gasPrice.has_value())
    {
        return *gasPrice;
    }
    return 0;
}

/// The priority fee the transaction effectively pays under @p baseFee (geth's
/// effectiveTip): min(tipCap, feeCap - baseFee), clamped at zero.
inline u256 effectivePriorityTip(protocol::Transaction const& transaction, u256 const& baseFee)
{
    auto const feeCap = feeCapKey(transaction);
    if (feeCap <= baseFee)
    {
        return 0;
    }
    return std::min(feeTipKey(transaction), feeCap - baseFee);
}

/// The EIP-1559 replacement rule (geth's PriceBump = 10%): the incoming transaction must
/// bump BOTH its tip cap and its fee cap by at least 10% over the stored one. Computed
/// exactly in u512 so no u256 product overflows.
inline bool bumpsFee(protocol::Transaction const& stored, protocol::Transaction const& incoming)
{
    return u512(feeTipKey(incoming)) * 10 >= u512(feeTipKey(stored)) * 11 &&
           u512(feeCapKey(incoming)) * 10 >= u512(feeCapKey(stored)) * 11;
}

template <class TransactionsType>
concept InputTransactions =
    ::ranges::input_range<TransactionsType> &&
    std::same_as<::ranges::range_value_t<TransactionsType>, protocol::Transaction::Ptr>;

template <class InputHashesType>
concept InputHashes =
    ::ranges::input_range<InputHashesType> &&
    std::same_as<::ranges::range_value_t<InputHashesType>, bcos::crypto::HashType>;

template <class SenderNonceTuple>
concept SenderNonce = requires(SenderNonceTuple senderNonce) {
    {
        std::get<0>(senderNonce)
    } -> std::convertible_to<std::string_view>;
    {
        std::get<1>(senderNonce)
    } -> std::convertible_to<int64_t>;
};


template <class SenderNoncesType>
concept SenderNonces = ::ranges::input_range<SenderNoncesType> &&
                       SenderNonce<::ranges::range_value_t<SenderNoncesType>>;


class MemPoolImpl
{
private:
    // Transactions: A boost::multi_index_container that maintains multiple indexes over
    // TransactionData for different query patterns and ordering policies.
    //
    // 该容器为交易缓存的多索引结构，针对不同访问/遍历需求建立多个索引，便于快速按哈希查找、
    // 按账户与 nonce 顺序扫描，以及按发送者聚合遍历，同时保留插入顺序。
    //
    // Index layout 索引布局（get<N>() 对应关系）：
    //   0 -> SenderNonceIndex（ordered_unique by (sender, nonce)）
    //        - 保证同一 sender 下 nonce 的唯一性与有序性；
    //        - 便于按照 (sender, currentNonce..) 连续扫描，用于 seal / remove等流程；
    //   1 -> HashIndex（hashed_unique by tx hash）
    //        - 按交易哈希 O(1) 近似查找/去重，用于交易按 hash 去重；
    //   2 -> SenderIndex（hashed_non_unique by sender）
    //        - 快速按发送者分组遍历（一个 sender 对应多笔交易），用于按 sender 遍历；
    //   3 -> SequenceIndex（sequenced）
    //        - 维护插入顺序（FIFO），便于基于时间/先来先服务的策略，用于超时淘汰；
    //
    // Notes:
    // - ordered_unique composite key uses (sender, nonce) to avoid duplicates and keep
    //   per-sender nonce strictly increasing when scanning.
    // - hashed_unique by hash prevents duplicate transactions with the same hash.
    // - hashed_non_unique by sender supports grouping operations across all txs of a sender.
    // - sequenced keeps push order; useful for strategies relying on arrival order.
    //
    // 注意：本结构仅添加索引与顺序语义，不改变交易对象本身；增删改查均通过对应索引视图完成。
    using Transactions = boost::multi_index_container<TransactionData,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::composite_key<TransactionData,
                    boost::multi_index::const_mem_fun<TransactionData, std::string_view,
                        &TransactionData::sender>,
                    boost::multi_index::const_mem_fun<TransactionData, int64_t,
                        &TransactionData::nonce>>,
                boost::multi_index::composite_key_compare<std::less<>, std::less<>>>,
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<TransactionData,
                bcos::crypto::HashType, &TransactionData::hash>>,
            boost::multi_index::hashed_non_unique<boost::multi_index::const_mem_fun<TransactionData,
                std::string_view, &TransactionData::sender>>,
            boost::multi_index::sequenced<>>>;

    Transactions m_transactions;
    /// EIP-4844 blob sidecars of pooled blob transactions, keyed by transaction hash (L1
    /// only; the L2 pool never admits blob transactions). Registered atomically with the
    /// admission that carries them and dropped at every eviction point below, so the map
    /// can never hold a sidecar whose transaction is gone. Guarded by m_mutex.
    std::unordered_map<crypto::HashType, engine::BlobTxSidecar> m_blobSidecars;
    // mutable: the const query entries (blobSidecar / blobsByVersionedHashes) lock it too.
    mutable std::mutex m_mutex;
    bool m_rawAddress{};
    MemPoolConfig m_config;

    /// The mempool stores the sender as raw address bytes (TransactionImpl::sender());
    /// convert them to an evmc_address so EVMAccount resolves the same lower-case hex
    /// account path (/apps/<hex>) the executor writes and reads.
    static evmc_address senderToAddress(std::string_view sender)
    {
        evmc_address addr{};
        if (sender.size() >= sizeof(addr.bytes))
        {
            std::copy_n(sender.begin(), sizeof(addr.bytes), addr.bytes);
        }
        return addr;
    }

    /// What the pool does when (sender, nonce) is already taken: the one policy difference
    /// between its two entries, kept as a value so admission has one implementation, not two.
    enum class OnTakenNonce : std::uint8_t
    {
        Replace,  ///< add(): the newer transaction takes the slot
        Refuse,   ///< tryAdd(): first come first served
    };
    /// Shared body of add() and tryAdd(); the caller holds m_mutex. A blob sidecar handed
    /// in with the transaction is registered on the same admission step (and the replaced
    /// transaction's sidecar dropped on a taken-nonce replacement), so the pool never
    /// observes a blob transaction without its sidecar.
    protocol::TransactionStatus insertLocked(protocol::Transaction::Ptr transaction,
        OnTakenNonce onTakenNonce, std::optional<engine::BlobTxSidecar> sidecar);

    /// Drop transactions older than the configured lifetime; a no-op when the lifetime
    /// is 0 (the default, always on L2). The sequenced index is insertion order and every
    /// insert stamps importTime, so the expired transactions form a prefix of it.
    void evictExpiredLocked()
    {
        if (m_config.txLifetimeMs <= 0)
        {
            return;
        }
        auto const cutoff = static_cast<int64_t>(utcTime()) - m_config.txLifetimeMs;
        auto& sequenceIndex = m_transactions.get<3>();
        while (!sequenceIndex.empty() && sequenceIndex.front().importTime() < cutoff)
        {
            m_blobSidecars.erase(sequenceIndex.front().hash());
            sequenceIndex.pop_front();
        }
    }

    /// Enforce the configured capacity by evicting the cheapest transaction (lowest
    /// static tip key; the first minimum found wins the tie) until the pool fits. A no-op
    /// when the capacity is 0 (unlimited — the default, always on L2).
    void evictOverCapacityLocked()
    {
        if (m_config.capacity == 0)
        {
            return;
        }
        auto& sequenceIndex = m_transactions.get<3>();
        while (m_transactions.size() > m_config.capacity)
        {
            auto cheapest = sequenceIndex.begin();
            for (auto it = sequenceIndex.begin(); it != sequenceIndex.end(); ++it)
            {
                if (feeTipKey(*it->m_transaction) < feeTipKey(*cheapest->m_transaction))
                {
                    cheapest = it;
                }
            }
            m_blobSidecars.erase(cheapest->hash());
            sequenceIndex.erase(cheapest);
        }
    }

    /// Read the sender's account nonce out of @p state; shared by both seal() variants.
    template <class StateType>
    int64_t accountNonce(StateType& state, std::string_view sender) const
    {
        // The mempool stores the sender as raw address bytes (forceSender), while the
        // executor persists accounts under the lower-case hex path (/apps/<hex>) via the
        // evmc_address EVMAccount overload, so the raw bytes must go through the
        // evmc_address overload for the nonce read to find the executor's account.
        ledger::account::EVMAccount account(state, senderToAddress(sender), m_rawAddress);
        int64_t currentNonce = 0;
        if (auto nonceStr = task::syncWait(account.nonce()))
        {
            if (auto result = std::from_chars(
                    nonceStr->data(), nonceStr->data() + nonceStr->size(), currentNonce);
                result.ec != std::errc{})
            {
                bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
            }
        }
        return currentNonce;
    }

    void add(protocol::Transaction::Ptr transaction);
    void removeBySenderNonces(SenderNonces auto senderNonces)
    {
        auto& senderNonceIndex = m_transactions.get<0>();

        for (auto&& [sender, nonce] : senderNonces)
        {
            auto start = senderNonceIndex.lower_bound(std::make_tuple(sender, 0));
            auto end = senderNonceIndex.upper_bound(std::make_tuple(sender, nonce));
            for (auto it = start; it != end;)
            {
                m_blobSidecars.erase(it->hash());
                it = senderNonceIndex.erase(it);
            }
        }
    }

public:
    /// The default config is the L2 pool exactly as before; L1 wiring passes a config.
    explicit MemPoolImpl(MemPoolConfig config = {}) : m_config(std::move(config)) {}

    MemPoolConfig const& config() const { return m_config; }

    /// The per-transaction blob limit in force now: the EIP-7840 schedule resolved at the
    /// chain head's timestamp when the config wires a provider (EL mode), else the static
    /// field. Pre-Cancun heads resolve to the zero schedule (maxBlobs 0: no blobs).
    std::size_t maxBlobsPerTransaction() const
    {
        if (m_config.headTimestampSeconds)
        {
            return static_cast<std::size_t>(protocol::blobScheduleForTimestamp(
                m_config.blobForks, m_config.headTimestampSeconds())
                                                .maxBlobs);
        }
        return m_config.maxBlobsPerTransaction;
    }

    void add(InputTransactions auto transactions)
    {
        std::unique_lock lock(m_mutex);
        for (auto&& transaction : transactions)
        {
            add(std::forward<decltype(transaction)>(transaction));
        }
    }

    /// Admit one transaction and SAY what happened. add() above answers nothing, and its four
    /// silent endings are not equally harmless:
    ///
    ///   no computable hash / unreadable nonce -- nothing is stored, so a caller that reports
    ///   success on the strength of add() having returned hands its user a transaction hash for
    ///   a transaction the pool does not hold, and the user polls for a receipt until they give
    ///   up;
    ///   taken (sender, nonce) -- the newer transaction REPLACES the stored one, so it is an
    ///   EARLIER submitter whose hash quietly stops being pollable;
    ///   duplicate hash -- the transaction is in the pool (someone already submitted it), and
    ///   the only thing lost is the caller's ability to tell that from a fresh admission.
    ///
    /// The two entries also differ on that taken pair: this one refuses. First come first
    /// served is what the other pool does (MemoryStorage's insertMemoryNonce), and a
    /// replacement policy is a fee-market decision this pool has no fee market to make.
    ///
    /// Refusing and reserving are ONE step here, under the same lock as the lookup. Asking the
    /// pool whether a pair is free and then adding it would be two, and two admissions racing
    /// through that gap both pass -- the TOCTOU FIB-51 removed from the other pool.
    ///
    /// @return None when the transaction is now in the pool; AlreadyInTxPool, NonceCheckFail or
    /// Malformed when it is not. THROWS on a null or tainted transaction, and on a blob
    /// transaction on L2: none of those is a verdict about an otherwise well-formed
    /// transaction -- they mean the caller skipped admission, so they must not be reportable as
    /// one of its statuses. On L1 a blob transaction is admitted like any other, and a
    /// taken-nonce replacement that does not meet the fee bump reports AlreadyInTxPool.
    ///
    /// A blob transaction's EIP-4844 sidecar (network-wrapper payload) is registered
    /// atomically with the admission: passing it separately would open a window in which a
    /// seal could pick the transaction without its sidecar.
    protocol::TransactionStatus tryAdd(protocol::Transaction::Ptr transaction,
        std::optional<engine::BlobTxSidecar> sidecar = std::nullopt);

    /// The sidecar of a pooled blob transaction, or nullopt when the transaction is not
    /// pooled or carries none.
    std::optional<engine::BlobTxSidecar> blobSidecar(crypto::HashType const& txHash) const
    {
        std::unique_lock lock(m_mutex);
        if (auto it = m_blobSidecars.find(txHash); it != m_blobSidecars.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    /// Whether a sidecar is registered for @p txHash — the existence probe for callers that
    /// do not need the payload (blobSidecar() copies the whole sidecar, blobs included).
    bool hasBlobSidecar(crypto::HashType const& txHash) const
    {
        std::unique_lock lock(m_mutex);
        return m_blobSidecars.contains(txHash);
    }

    /// Invoke @p visitor with the registered sidecar under the pool lock, avoiding the
    /// whole-sidecar copy blobSidecar() makes. @return false when none is registered.
    bool visitBlobSidecar(crypto::HashType const& txHash, auto&& visitor) const
    {
        std::unique_lock lock(m_mutex);
        auto const it = m_blobSidecars.find(txHash);
        if (it == m_blobSidecars.end())
        {
            return false;
        }
        visitor(it->second);
        return true;
    }

    /// engine_getBlobsV1's pool half: for every requested versioned hash, the pooled blob
    /// item whose commitment hashes to it (sha256(commitment) with the 0x01 version byte).
    /// Versioned hashes are recomputed per call; the pool is small and this is a rare CL
    /// recovery path, so no versioned-hash index is kept.
    std::vector<std::optional<engine::BlobItem>> blobsByVersionedHashes(
        std::span<const crypto::HashType> versionedHashes) const;

    void seal(int64_t limit,
        storage2::ReadWriteStorage<executor_v1::StateKeyView, executor_v1::StateValue> auto& state,
        std::output_iterator<protocol::Transaction::Ptr> auto out)
    {
        int64_t count = 0;
        std::unique_lock lock(m_mutex);
        evictExpiredLocked();
        auto& senderNonceIndex = m_transactions.get<0>();
        auto& senderIndex = m_transactions.get<2>();
        // senderIndex is hashed_non_unique: a sender appears once per transaction, so the same
        // sender is visited multiple times while iterating. Track the senders whose gapless
        // prefix has already been sealed to avoid re-sealing the same transactions on the
        // subsequent entries of that sender. (The legacy implementation achieved this by
        // writing the advanced nonce back into `state`; keeping seal() read-only with respect
        // to `state` requires the dedup to live here instead.)
        std::unordered_set<std::string_view> sealedSenders;
        for (const auto& data : senderIndex)
        {
            auto sender = data.sender();
            if (!sealedSenders.emplace(sender).second)
            {
                continue;
            }
            // The mempool stores the sender as raw address bytes (forceSender), while the
            // executor persists accounts under the lower-case hex path (/apps/<hex>) via the
            // evmc_address EVMAccount overload. Passing the raw bytes through the string_view
            // overload would treat them as a hex string and compute a wrong table path, so the
            // nonce read below would miss the account entirely. Build an evmc_address instead
            // so the same hex path is used as the executor.
            ledger::account::EVMAccount account(state, senderToAddress(sender), m_rawAddress);

            int64_t currentNonce = 0;
            if (auto nonceStr = task::syncWait(account.nonce()))
            {
                if (auto result = std::from_chars(
                        nonceStr->data(), nonceStr->data() + nonceStr->size(), currentNonce);
                    result.ec != std::errc{})
                {
                    bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
                }
            }
            // seal() is read-only with respect to `state`: it only reads the sender's current
            // nonce to pick the executable (gapless) prefix in nonce order, and never writes the
            // advanced nonce back. The authoritative nonce advance happens during execution
            // itself, so writing it here would cause the executor (evmone) to reject every
            // just-sealed transaction with NONCE_TOO_LOW. This matches how geth's legacypool
            // (in-memory noncer) and reth's best_transactions() select block transactions
            // without touching state.
            for (auto nonceIt = senderNonceIndex.lower_bound(std::make_tuple(sender, currentNonce));
                 nonceIt != senderNonceIndex.end() && nonceIt->sender() == sender &&
                 nonceIt->nonce() == currentNonce;
                 ++nonceIt)
            {
                ++currentNonce;
                ++count;
                *out++ = nonceIt->m_transaction;

                if (count >= limit)
                {
                    break;
                }
            }
            if (count >= limit)
            {
                break;
            }
        }
    }

    /// L1 fee-market seal: the per-sender selection is the same gapless prefix as
    /// seal() above, but the prefixes are emitted ordered by effective priority fee —
    /// geth's best_transactions(): one cursor per sender walks that sender's executable
    /// nonces, and a heap picks the best cursor head by (effectivePriorityTip desc, hash
    /// asc). @p baseFee is the block's base fee; transactions whose fee cap does not
    /// cover it sort last with tip 0 (they are not dropped — admission already enforces
    /// FeeCapLessThanBaseFee against its own base fee).
    void seal(int64_t limit,
        storage2::ReadWriteStorage<executor_v1::StateKeyView, executor_v1::StateValue> auto& state,
        std::output_iterator<protocol::Transaction::Ptr> auto out, u256 const& baseFee)
    {
        int64_t count = 0;
        std::unique_lock lock(m_mutex);
        evictExpiredLocked();
        auto& senderNonceIndex = m_transactions.get<0>();
        auto& senderIndex = m_transactions.get<2>();

        using SenderNonceIt = decltype(senderNonceIndex.begin());
        struct Cursor
        {
            u256 tip;
            crypto::HashType hash;
            std::string_view sender;
            SenderNonceIt it;
            int64_t nextNonce;
        };
        // std::priority_queue keeps the "largest" element on top: the highest effective
        // tip, ties broken by the smaller hash for determinism.
        auto cursorLess = [](Cursor const& a, Cursor const& b) {
            if (a.tip != b.tip)
            {
                return a.tip < b.tip;
            }
            return a.hash > b.hash;
        };
        std::priority_queue<Cursor, std::vector<Cursor>, decltype(cursorLess)> heap(
            cursorLess);

        // senderIndex is hashed_non_unique: a sender appears once per transaction, so
        // track the senders whose cursor is already on the heap (same dedup seal()
        // documents for its prefix pass).
        std::unordered_set<std::string_view> queuedSenders;
        for (const auto& data : senderIndex)
        {
            auto sender = data.sender();
            if (!queuedSenders.emplace(sender).second)
            {
                continue;
            }
            int64_t currentNonce = accountNonce(state, sender);
            auto it = senderNonceIndex.lower_bound(std::make_tuple(sender, currentNonce));
            if (it != senderNonceIndex.end() && it->sender() == sender &&
                it->nonce() == currentNonce)
            {
                heap.push(Cursor{.tip = effectivePriorityTip(*it->m_transaction, baseFee),
                    .hash = it->hash(),
                    .sender = sender,
                    .it = it,
                    .nextNonce = currentNonce + 1});
            }
        }

        while (!heap.empty() && count < limit)
        {
            auto cursor = heap.top();
            heap.pop();
            *out++ = cursor.it->m_transaction;
            ++count;
            // Advance the cursor inside the same sender's gapless prefix; a nonce gap
            // stops this sender for the block, exactly like seal()'s prefix walk.
            auto next = std::next(cursor.it);
            if (next != senderNonceIndex.end() && next->sender() == cursor.sender &&
                next->nonce() == cursor.nextNonce)
            {
                heap.push(Cursor{.tip = effectivePriorityTip(*next->m_transaction, baseFee),
                    .hash = next->hash(),
                    .sender = cursor.sender,
                    .it = next,
                    .nextNonce = cursor.nextNonce + 1});
            }
        }
    }

    void remove(storage2::ReadableStorage<executor_v1::StateKeyView> auto& state)
    {
        std::unique_lock lock(m_mutex);
        auto& senderIndex = m_transactions.get<2>();
        auto& senderNonceIndex = m_transactions.get<0>();

        for (auto it = senderIndex.begin(); it != senderIndex.end();)
        {
            auto sender = it->sender();
            auto nextIt = senderIndex.equal_range(sender).second;
            // Same hex-path note as in seal(): the raw sender bytes must go through the
            // evmc_address overload so the account nonce read finds the executor's account.
            ledger::account::EVMAccount account(state, senderToAddress(sender), m_rawAddress);
            if (auto nonceStr = task::syncWait(account.nonce()))
            {
                int64_t nonce = 0;
                if (auto result = std::from_chars(
                        nonceStr->data(), nonceStr->data() + nonceStr->size(), nonce);
                    result.ec != std::errc{})
                {
                    bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
                }

                if (nonce > 0)
                {
                    auto start = senderNonceIndex.lower_bound(std::make_tuple(sender, 0));
                    auto end = senderNonceIndex.upper_bound(std::make_tuple(sender, nonce - 1));
                    for (auto eraseIt = start; eraseIt != end;)
                    {
                        m_blobSidecars.erase(eraseIt->hash());
                        eraseIt = senderNonceIndex.erase(eraseIt);
                    }
                }
            }

            it = nextIt;
        }
    }

    /// Drop txs by hash during OP payload building.
    void removeByHash(std::span<bcos::crypto::HashType const> hashes)
    {
        std::unique_lock lock(m_mutex);
        auto& hashIndex = m_transactions.get<1>();
        for (auto const& hash : hashes)
        {
            m_blobSidecars.erase(hash);
            hashIndex.erase(hash);
        }
    }

    void remove(InputHashes auto hashes)
    {
        std::unordered_map<std::string_view, int64_t> senderNonceMap;
        std::unique_lock lock(m_mutex);
        auto& hashIndex = m_transactions.get<1>();
        for (const auto& hash : hashes)
        {
            if (auto it = hashIndex.find(hash); it != hashIndex.end())
            {
                if (auto nonceIt = senderNonceMap.find(it->sender());
                    nonceIt != senderNonceMap.end())
                {
                    nonceIt->second = std::max(it->nonce(), nonceIt->second);
                }
                else
                {
                    senderNonceMap.emplace(it->sender(), it->nonce());
                }
            }
        }
        removeBySenderNonces(::ranges::views::all(senderNonceMap));
    }

    template <InputHashes TransactionHashes>
    std::vector<protocol::Transaction::Ptr> get(TransactionHashes hashes)
    {
        std::vector<protocol::Transaction::Ptr> transactions;
        if constexpr (::ranges::sized_range<TransactionHashes>)
        {
            transactions.reserve(hashes.size());
        }
        std::unique_lock lock(m_mutex);
        auto& hashIndex = m_transactions.get<1>();
        for (const auto& hash : hashes)
        {
            if (auto it = hashIndex.find(hash); it != hashIndex.end())
            {
                transactions.emplace_back(it->m_transaction);
            }
            else
            {
                transactions.emplace_back();
            }
        }
        return transactions;
    }
};

}  // namespace bcos::txpool