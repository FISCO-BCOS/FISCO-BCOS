/// @file EthereumState.h
/// @brief Ported evmone state machine (eth/state/state.{hpp,cpp}) adapted to
///        read/write BCOS storage directly — no evmone::state::StateView /
///        StateDiff adapter layer.
///
/// The in-memory journal (checkpoint/rollback, EIP-6780 SELFDESTRUCT semantics,
/// EIP-2929 access tracking, transient storage, ...) is preserved verbatim from
/// evmone so consensus behavior is unchanged. What differs from upstream:
///   * the initial state is read straight from BCOS storage via
///     ledger::account::EVMAccount + storage2 (synchronous, fail-safe — the
///     evmc::Host interface is noexcept, so a failed read is reported as
///     "absent / empty" like the old StorageStateView adapter did);
///   * the final write-back is applyToStorage(), which writes the modified
///     accounts directly to BCOS storage via EVMAccount/storage2 — there is no
///     evmone::state::StateDiff struct and no separate applyStateDiff function.
///
/// Types are renamed (EthAccount / EthStorageValue / EthereumState) so this
/// port never collides with the unchanged bcos-evm library's
/// evmone::state::{Account, StorageValue, State} (both may be linked into the
/// same binary — bcos-evm keeps serving the opstack layer and its tests).
///
/// Known limitation — code hash algorithm:
/// applyToStorage() keys the SYS_CODE_BINARY table by the code hash, and the
/// host always computes keccak256(code) (Ethereum consensus hashing). BCOS's
/// v0/v1 execution layers key that table by the chain's global hash algorithm
/// (SM3 on 国密 chains). executor_version=2 therefore targets keccak256
/// (Ethereum-standard) chains only — it must not run on an SM3 chain, nor
/// share a code-binary table with a v0/v1 layer using a different hash
/// algorithm (see the note at the setCode call in applyToStorage()).

#pragma once

#include "EVMSupport.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage2/RollbackableStorage.h"
#include "bcos-task/TBBWait.h"
#include <evmc/evmc.h>
#include <cassert>
#include <evmc/evmc.hpp>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace bcos::executor_v1::eth
{
using evmc::address;
using evmc::bytes;
using evmc::bytes32;
using evmc::bytes_view;
using uint256 = bcos::u256;

/// The representation of the account storage value (ported evmone StorageValue).
struct EthStorageValue
{
    /// The current value.
    bytes32 current;

    /// The original value.
    bytes32 original;

    evmc_access_status access_status = EVMC_ACCESS_COLD;
};

/// The state account (ported evmone Account).
struct EthAccount
{
    /// The maximum allowed nonce value.
    static constexpr auto NonceMax = std::numeric_limits<uint64_t>::max();

    /// The keccak256 hash of the empty input. Used to identify empty account's code.
    static constexpr auto EMPTY_CODE_HASH = evmc::bytes32{{0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23,
        0x3c, 0x92, 0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0, 0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82,
        0x27, 0x3b, 0x7b, 0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70}};

    /// The account nonce.
    uint64_t nonce = 0;

    /// The account balance.
    uint256 balance;

    bytes32 code_hash = EMPTY_CODE_HASH;

    /// If the account has non-empty initial storage (when accessing the cold account).
    bool has_initial_storage = false;

    /// The cached and modified account storage entries.
    std::unordered_map<bytes32, EthStorageValue> storage;

    /// The EIP-1153 transient (transaction-level lifetime) storage.
    std::unordered_map<bytes32, bytes32> transient_storage;

    /// The cache of the account code.
    ///
    /// Check code_hash to know if an account code is empty.
    /// Empty here only means it has not been loaded from the initial storage.
    bytes code;

    /// The account has been destructed and should be erased at the end of a transaction.
    bool destructed = false;

    /// The account should be erased if it is empty at the end of a transaction.
    /// This flag means the account has been "touched" as defined in EIP-161,
    /// or it is a newly created temporary account.
    bool erase_if_empty = false;

    /// The account has been created in the current transaction.
    bool just_created = false;

    /// This account's code has been modified.
    bool code_changed = false;

    evmc_access_status access_status = EVMC_ACCESS_COLD;

    [[nodiscard]] bool is_empty() const noexcept
    {
        return nonce == 0 && balance == 0 && code_hash == EMPTY_CODE_HASH;
    }
};

namespace eth_state_detail
{
/// The read-only view of an account loaded from BCOS storage.
struct ReadAccount
{
    uint64_t nonce = 0;
    uint256 balance;
    bytes32 code_hash = EthAccount::EMPTY_CODE_HASH;
    bool has_storage = false;
};
}  // namespace eth_state_detail

/// Remove ALL storage rows of an account — the storage slots AND the three core
/// Ethereum fields (nonce/balance/codeHash).
/// Used when an account self-destructs: its full state (including storage) must
/// be cleared so that a later CREATE/CREATE2 at the same address is not treated
/// as an EIP-7610 collision.
///
/// NOTE: this deletes EVERY row of the account table, including the three core
/// Ethereum fields (nonce/balance/codeHash). The MPT builder (finalizeAccount)
/// recognizes a tombstone exactly as "all three core rows deleted", so a
/// self-destructed (or EIP-161 emptied) account must present DELETED_TYPE rows,
/// not zero-valued rows — writing zeros would re-insert an empty account leaf
/// into the trie and fork the state root.
template <class Storage>
task::Task<void> clearAccountStorage(
    Storage& storage, bcos::ledger::account::EVMAccount<Storage>& acc)
{
    using namespace bcos::ledger;
    using namespace bcos::ledger::account;
    auto tableName = co_await acc.path();

    std::vector<executor_v1::StateKey> keysToRemove;
    auto it = co_await storage2::range(
        storage, storage2::RANGE_SEEK, executor_v1::StateKey{tableName, std::string_view{}});
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        executor_v1::StateKeyView view(k);
        if (view.m_table != tableName)
            break;  // Left this account's table.
        keysToRemove.emplace_back(k);
    }
    if (!keysToRemove.empty())
        co_await storage2::removeSome(storage, keysToRemove);
}

/// Ported evmone::state::State, reading/writing BCOS storage directly.
///
/// @tparam Storage the storage backend (raw scheduler storage or a
///                 Rollbackable wrapper). Accessed through EVMAccount.
template <class Storage>
class EthereumState
{
    struct JournalBase
    {
        address addr;
    };

    struct JournalBalanceChange : JournalBase
    {
        uint256 prev_balance;
    };

    struct JournalTouched : JournalBase
    {
    };

    struct JournalStorageChange : JournalBase
    {
        bytes32 key;
        bytes32 prev_value;
        evmc_access_status prev_access_status;
    };

    struct JournalTransientStorageChange : JournalBase
    {
        bytes32 key;
        bytes32 prev_value;
    };

    struct JournalNonceBump : JournalBase
    {
    };

    struct JournalCreate : JournalBase
    {
        bool existed;
    };

    struct JournalDestruct : JournalBase
    {
    };

    struct JournalAccessAccount : JournalBase
    {
    };

    using JournalEntry =
        std::variant<JournalBalanceChange, JournalTouched, JournalStorageChange, JournalNonceBump,
            JournalCreate, JournalTransientStorageChange, JournalDestruct, JournalAccessAccount>;

    /// The accounts loaded from the initial state and potentially modified.
    std::unordered_map<address, EthAccount> m_modified;

    /// The state journal: the list of changes made to the state
    /// with information how to revert them.
    std::vector<JournalEntry> m_journal;

    Storage& m_storage;

    // ---- Direct BCOS storage reads (synchronous, fail-safe) ----

    task::Task<std::optional<eth_state_detail::ReadAccount>> readAccountImpl(address addr) const
    {
        using namespace bcos::ledger::account;
        auto& storage = m_storage;

        EVMAccount<Storage> evmAccount(storage, addr, false, /*treatSystemAsUser=*/true);

        // Do NOT gate on SYS_TABLES existence alone: the PoW reward path writes
        // the flat BALANCE row but (historically) never registers the account
        // table, so an address that only ever received block rewards reads as
        // non-existent through EVMAccount::exists() even though it holds a real
        // balance. Prefer the flat fields: an account with a non-default nonce,
        // balance or code hash exists regardless of the SYS_TABLES marker.
        auto nonceVal = co_await evmAccount.nonce();
        auto balance = co_await evmAccount.balance();
        auto codeHashVal = co_await evmAccount.codeHash();
        bool hasCodeHash = false;
        {
            auto const* d = codeHashVal.data();
            for (size_t i = 0; i < 32; ++i)
                if (d[i] != 0)
                {
                    hasCodeHash = true;
                    break;
                }
        }
        if (!nonceVal.has_value() && balance == 0 && !hasCodeHash)
        {
            co_return std::nullopt;  // no account at all
        }

        eth_state_detail::ReadAccount acc;
        if (nonceVal.has_value())
            acc.nonce = static_cast<uint64_t>(bcos::u256(nonceVal.value()));

        acc.balance = balance;

        if (hasCodeHash)
            std::copy_n(codeHashVal.data(), sizeof(evmc_bytes32), acc.code_hash.bytes);
        else
            acc.code_hash = EthAccount::EMPTY_CODE_HASH;

        acc.has_storage = co_await hasStorageImpl(evmAccount);
        co_return acc;
    }

    /// Check whether the account table contains any storage slot (a key that
    /// is not one of the fixed account field names).
    ///
    /// Known limitation (inherited from the old StorageStateView adapter): this
    /// scans the account's table via storage2::range(), and ReadWriteSetStorage
    /// does NOT record range reads in the read-write set. Under
    /// SchedulerParallelImpl, a chunk that only WRITES storage slots of an
    /// account may therefore be scheduled in parallel with — and race against —
    /// a chunk that reads that account's has_storage here (the writer's slots
    /// are not in the reader's read set, so the two chunks are not ordered).
    ///
    /// This is a LIVE risk, not a hypothetical one: a node configured for
    /// parallel baseline scheduling (SchedulerParallelImpl) would run the v2
    /// pipeline on it and could hit exactly this race on EIP-7610
    /// CREATE-collision inputs.
    ///
    /// Mitigation: the v2 pipeline must be built SERIAL-ONLY (SchedulerSerialImpl
    /// in both branches of the baseline-scheduler wiring in libinitializer). That
    /// change lands with split 4/4 — the split that first wires EthereumState
    /// into a live path; until then this race is documented but unmitigated
    /// (splits 1/4 and 2/4 are safe to merge alone because nothing instantiates
    /// EthereumState on a production path — the only instantiation is the smoke
    /// test). Revisit, ideally by recording the range read in the read/write
    /// set, before v2 is allowed to run on a parallel scheduler.
    task::Task<bool> hasStorageImpl(bcos::ledger::account::EVMAccount<Storage>& evmAccount) const
    {
        using namespace bcos::ledger;
        using namespace bcos::ledger::account;
        auto& storage = m_storage;
        auto tableName = co_await evmAccount.path();

        bool hasStorage = false;
        auto it = co_await storage2::range(
            storage, storage2::RANGE_SEEK, executor_v1::StateKey{tableName, std::string_view{}});

        while (auto kv = co_await it.next())
        {
            auto const& [k, v] = *kv;
            executor_v1::StateKeyView view(k);
            if (view.m_table != tableName)
                break;  // Left this account's table.

            auto key = view.m_key;
            if (key != ACCOUNT_TABLE_FIELDS::NONCE && key != ACCOUNT_TABLE_FIELDS::BALANCE &&
                key != ACCOUNT_TABLE_FIELDS::CODE_HASH && key != ACCOUNT_TABLE_FIELDS::CODE &&
                key != ACCOUNT_TABLE_FIELDS::ABI && key != ACCOUNT_TABLE_FIELDS::ALIVE &&
                key != ACCOUNT_TABLE_FIELDS::FROZEN && key != ACCOUNT_TABLE_FIELDS::SHARD)
            {
                hasStorage = true;
                break;
            }
        }
        co_return hasStorage;
    }

    std::optional<eth_state_detail::ReadAccount> readAccount(const address& addr) const noexcept
    {
        try
        {
            return task::tbb::syncWait(readAccountImpl(addr));
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    task::Task<bytes> readCodeImpl(address addr) const
    {
        using namespace bcos::ledger::account;
        auto& storage = m_storage;
        EVMAccount<Storage> evmAccount(storage, addr, false, /*treatSystemAsUser=*/true);

        if (!co_await evmAccount.exists())
            co_return {};

        auto codeEntry = co_await evmAccount.code();
        if (!codeEntry.has_value())
            co_return {};

        auto view = codeEntry->get();
        co_return bytes(view.begin(), view.end());
    }

    bytes readCode(const address& addr) const noexcept
    {
        try
        {
            return task::tbb::syncWait(readCodeImpl(addr));
        }
        catch (...)
        {
            return {};
        }
    }

    task::Task<bytes32> readStorageImpl(address addr, bytes32 key) const
    {
        using namespace bcos::ledger::account;
        auto& storage = m_storage;
        EVMAccount<Storage> evmAccount(storage, addr, false, /*treatSystemAsUser=*/true);

        // SLOAD on a non-existent account returns 0 per EVM spec.
        // EVMAccount::storage() already returns empty bytes32 for missing
        // accounts/keys.
        co_return co_await evmAccount.storage(key);
    }

    bytes32 readStorage(const address& addr, const bytes32& key) const noexcept
    {
        try
        {
            return task::tbb::syncWait(readStorageImpl(addr, key));
        }
        catch (...)
        {
            return {};
        }
    }

public:
    explicit EthereumState(Storage& storage) noexcept : m_storage(storage) {}

    /// Inserts the new account at the address.
    /// There must not exist any account under this address before.
    EthAccount& insert(const address& addr, EthAccount account = {});

    /// Returns the pointer to the account at the address if the account exists. Null otherwise.
    EthAccount* find(const address& addr) noexcept;

    /// Gets the account at the address (the account must exist).
    EthAccount& get(const address& addr) noexcept;

    /// Gets an existing account or inserts new account.
    EthAccount& get_or_insert(const address& addr, EthAccount account = {});

    bytes_view get_code(const address& addr);

    EthStorageValue& get_storage(const address& addr, const bytes32& key);

    /// Returns the state journal checkpoint. It can be later used to in rollback()
    /// to revert changes newer than the checkpoint.
    [[nodiscard]] size_t checkpoint() const noexcept { return m_journal.size(); }

    /// Reverts state changes made after the checkpoint.
    void rollback(size_t checkpoint);

    /// Methods performing changes to the state which can be reverted by rollback().
    /// @{

    /// Touches (as in EIP-161) an existing account or inserts new erasable account.
    EthAccount& touch(const address& addr);

    void journal_balance_change(const address& addr, const uint256& prev_balance);

    void journal_storage_change(
        const address& addr, const bytes32& key, const EthStorageValue& value);

    void journal_transient_storage_change(
        const address& addr, const bytes32& key, const bytes32& value);

    void journal_bump_nonce(const address& addr);

    void journal_create(const address& addr, bool existed);

    void journal_destruct(const address& addr);

    void journal_access_account(const address& addr);

    /// @}

    /// Write the modified accounts back to BCOS storage directly (no StateDiff
    /// struct, no separate applyStateDiff function). Handles created/modified
    /// accounts, self-destructed accounts and empty-touched accounts (EIP-161).
    task::Task<void> applyToStorage(evmc_revision rev);

    /// The accounts modified in this state (for diagnostics / dry-run checks).
    std::unordered_map<address, EthAccount> const& modified() const noexcept { return m_modified; }
};

template <class Storage>
EthAccount& EthereumState<Storage>::insert(const address& addr, EthAccount account)
{
    const auto r = m_modified.insert({addr, std::move(account)});
    assert(r.second);
    return r.first->second;
}

template <class Storage>
EthAccount* EthereumState<Storage>::find(const address& addr) noexcept
{
    if (const auto it = m_modified.find(addr); it != m_modified.end())
        return &it->second;
    if (const auto cacc = readAccount(addr); cacc)
    {
        EthAccount acc;
        acc.nonce = cacc->nonce;
        acc.balance = cacc->balance;
        acc.code_hash = cacc->code_hash;
        acc.has_initial_storage = cacc->has_storage;
        return &insert(addr, std::move(acc));
    }
    return nullptr;
}

template <class Storage>
EthAccount& EthereumState<Storage>::get(const address& addr) noexcept
{
    auto acc = find(addr);
    assert(acc != nullptr);
    return *acc;
}

template <class Storage>
EthAccount& EthereumState<Storage>::get_or_insert(const address& addr, EthAccount account)
{
    if (const auto acc = find(addr); acc != nullptr)
        return *acc;
    return insert(addr, std::move(account));
}

template <class Storage>
bytes_view EthereumState<Storage>::get_code(const address& addr)
{
    auto* a = find(addr);
    if (a == nullptr)
        return {};
    if (a->code_hash == EthAccount::EMPTY_CODE_HASH)
        return {};
    if (a->code.empty())
        a->code = readCode(addr);
    return a->code;
}

template <class Storage>
EthAccount& EthereumState<Storage>::touch(const address& addr)
{
    EthAccount fresh;
    fresh.erase_if_empty = true;
    auto& acc = get_or_insert(addr, std::move(fresh));
    if (!acc.erase_if_empty && acc.is_empty())
    {
        acc.erase_if_empty = true;
        m_journal.emplace_back(JournalTouched{addr});
    }
    return acc;
}

template <class Storage>
EthStorageValue& EthereumState<Storage>::get_storage(const address& addr, const bytes32& key)
{
    auto& acc = get(addr);
    const auto [it, missing] = acc.storage.try_emplace(key);
    if (missing)
    {
        const auto initial_value = readStorage(addr, key);
        it->second = {initial_value, initial_value};
    }
    return it->second;
}

template <class Storage>
void EthereumState<Storage>::journal_balance_change(
    const address& addr, const uint256& prev_balance)
{
    m_journal.emplace_back(JournalBalanceChange{{addr}, prev_balance});
}

template <class Storage>
void EthereumState<Storage>::journal_storage_change(
    const address& addr, const bytes32& key, const EthStorageValue& value)
{
    m_journal.emplace_back(JournalStorageChange{{addr}, key, value.current, value.access_status});
}

template <class Storage>
void EthereumState<Storage>::journal_transient_storage_change(
    const address& addr, const bytes32& key, const bytes32& value)
{
    m_journal.emplace_back(JournalTransientStorageChange{{addr}, key, value});
}

template <class Storage>
void EthereumState<Storage>::journal_bump_nonce(const address& addr)
{
    m_journal.emplace_back(JournalNonceBump{addr});
}

template <class Storage>
void EthereumState<Storage>::journal_create(const address& addr, bool existed)
{
    m_journal.emplace_back(JournalCreate{{addr}, existed});
}

template <class Storage>
void EthereumState<Storage>::journal_destruct(const address& addr)
{
    m_journal.emplace_back(JournalDestruct{addr});
}

template <class Storage>
void EthereumState<Storage>::journal_access_account(const address& addr)
{
    m_journal.emplace_back(JournalAccessAccount{addr});
}

template <class Storage>
void EthereumState<Storage>::rollback(size_t checkpoint)
{
    while (m_journal.size() != checkpoint)
    {
        std::visit(
            [this](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, JournalNonceBump>)
                {
                    get(e.addr).nonce -= 1;
                }
                else if constexpr (std::is_same_v<T, JournalTouched>)
                {
                    get(e.addr).erase_if_empty = false;
                }
                else if constexpr (std::is_same_v<T, JournalDestruct>)
                {
                    get(e.addr).destructed = false;
                }
                else if constexpr (std::is_same_v<T, JournalAccessAccount>)
                {
                    get(e.addr).access_status = EVMC_ACCESS_COLD;
                }
                else if constexpr (std::is_same_v<T, JournalCreate>)
                {
                    if (e.existed)
                    {
                        auto& a = get(e.addr);
                        a.nonce = 0;
                        a.code_hash = EthAccount::EMPTY_CODE_HASH;
                        a.code.clear();
                    }
                    else
                    {
                        m_modified.erase(e.addr);
                    }
                }
                else if constexpr (std::is_same_v<T, JournalStorageChange>)
                {
                    auto& s = get(e.addr).storage.find(e.key)->second;
                    s.current = e.prev_value;
                    s.access_status = e.prev_access_status;
                }
                else if constexpr (std::is_same_v<T, JournalTransientStorageChange>)
                {
                    auto& s = get(e.addr).transient_storage.find(e.key)->second;
                    s = e.prev_value;
                }
                else if constexpr (std::is_same_v<T, JournalBalanceChange>)
                {
                    get(e.addr).balance = e.prev_balance;
                }
                else
                {
                    static_assert(std::is_void_v<T>, "unhandled journal entry type");
                }
            },
            m_journal.back());
        m_journal.pop_back();
    }
}

template <class Storage>
task::Task<void> EthereumState<Storage>::applyToStorage(evmc_revision rev)
{
    using namespace bcos::ledger::account;

    // Phase 1: Process modified/created accounts FIRST.
    // This ensures created accounts exist before we potentially delete them.
    for (auto& [addr, acc] : m_modified)
    {
        // Deleted accounts are handled in phase 2 (they may also be modified —
        // evmone's build_diff reports destructed accounts under deleted_accounts).
        if (acc.destructed)
            continue;
        // Match evmone build_diff: an empty touched account is never written in
        // phase 1 — not just_created ones are deleted in phase 2, just_created
        // ones are dropped entirely.
        if (acc.erase_if_empty && rev >= EVMC_SPURIOUS_DRAGON && acc.is_empty())
            continue;

        EVMAccount<Storage> bcosAcc(m_storage, addr, false, /*treatSystemAsUser=*/true);
        if (!co_await bcosAcc.exists())
            co_await bcosAcc.create();
        co_await bcosAcc.setNonce(std::to_string(acc.nonce));
        co_await bcosAcc.setBalance(acc.balance);
        // ALWAYS write the codeHash row, not just on code_changed. A previous
        // transaction in the same block may have self-destructed this address
        // (clearAccountStorage deletes EVERY account row incl. codeHash as
        // DELETED_TYPE) and a later transaction re-touched it as an EOA with
        // code_changed == false. Without this unconditional write the flat
        // state would carry a deleted codeHash row alongside a live balance/
        // nonce — the MPT builder rejects exactly that shape
        // ("core-field row deleted outside a tombstone") and the state root
        // would fork. setCode() only touches SYS_CODE_BINARY when the hash is
        // absent, so re-writing an unchanged contract's codeHash is a no-op
        // there; for an EOA it (re)creates the row with emptyCodeHash.
        // (The SYS_CODE_BINARY table is keyed by keccak256(code) — Ethereum
        // consensus hashing; see the "Known limitation — code hash algorithm"
        // note at the top of this file.)
        {
            bcos::bytes code(acc.code.begin(), acc.code.end());
            bcos::bytes codeHash(acc.code_hash.bytes, acc.code_hash.bytes + sizeof(evmc_bytes32));
            co_await bcosAcc.setCode(std::move(code), std::string{}, bcos::h256(codeHash));
        }
        for (auto const& [key, value] : acc.storage)
        {
            if (value.current == value.original)
                continue;
            // NOTE: EVMAccount::setStorage always writes the entry (a zero
            // value leaves a zero-valued row; there is no delete-storage API).
            // Such zero rows are only cleaned on self-destruct via
            // clearAccountStorage(). This matches evmone's state semantics (a
            // zero slot reads back as zero).
            co_await bcosAcc.setStorage(key, value.current);
        }
    }

    // Phase 2: deleted / emptied accounts.
    for (auto& [addr, acc] : m_modified)
    {
        if (acc.destructed)
        {
            // Genuine deletion: clear account state (including storage, so that
            // a later CREATE/CREATE2 at this address is not an EIP-7610 collision).
            EVMAccount<Storage> bcosAcc(m_storage, addr, false, /*treatSystemAsUser=*/true);
            if (co_await bcosAcc.exists())
            {
                co_await bcosAcc.setBalance(0);
                co_await bcosAcc.setNonce("0");
                co_await bcosAcc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
                co_await clearAccountStorage(m_storage, bcosAcc);
            }
        }
        else if (acc.erase_if_empty && rev >= EVMC_SPURIOUS_DRAGON && acc.is_empty() &&
                 !acc.just_created)
        {
            // EIP-161: empty touched account is deleted.
            EVMAccount<Storage> bcosAcc(m_storage, addr, false, /*treatSystemAsUser=*/true);
            if (co_await bcosAcc.exists())
            {
                co_await bcosAcc.setBalance(0);
                co_await bcosAcc.setNonce("0");
                co_await bcosAcc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
                co_await clearAccountStorage(m_storage, bcosAcc);
            }
        }
    }
}

}  // namespace bcos::executor_v1::eth
