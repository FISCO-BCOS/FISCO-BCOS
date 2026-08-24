// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Storage2State — evmone::state::StateView over the storage2 (StateKey/EVMAccount) key space.
// One instance per block, single-threaded (mutable caches, no locks). The shared block-wide
// error slot (dbErr analogue) is the one cross-instance state and is deliberately mutex-guarded
// (SharedErrorSlot): part-3 parallel execution shares one slot across per-tx instances, and
// every access to it (poison write, poisoned()/firstError() read) takes the lock.
//
// Core invariants:
//   * exists-but-empty accounts return Account{defaults}, never nullopt (EIP-7610 collision
//     fidelity);
//   * zero-valued storage slot == nonexistent slot (Ethereum trie semantics), uniform across
//     probeHasStorage/fetchAllStorage/fetchStorage;
//   * poison-flag channel: reads are noexcept and swallow storage errors into poisoned()/
//     firstError(); consumers must fail the whole block on poisoned() — never degrade a storage
//     fault to “account missing”. applyDiff write-back failures poison AND rethrow (tripwire);
//   * account tables are “/apps/<hex(addr)>” paths (mainline MPT classifier), same for every
//     address incl. c_systemTxsAddress; requires feature_raw_address=off;
//   * nested syncWait is safe only inside the x_state-serialized segment (backends complete
//     synchronously in-thread).

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/Storage2StateHelpers.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::evm::evmstate
{

/// Block-wide error slot shared by per-tx execution instances (op-geth's dbErr analogue), safe
/// for the part-3 parallel-execution sharing pattern: every access is mutex-guarded, and the
/// stored message is first-write-wins so the block-level check observes the FIRST error across
/// all instances. Constructed once per block by the scheduler, shared via shared_ptr.
struct SharedErrorSlot
{
    std::mutex mutex;
    std::string message;
};


template <class Storage>
class Storage2State final : public evmone::state::StateView
{
public:
    /// One instance per block; no reset().
    /// @param sharedError optional block-wide error slot (op-geth's dbErr analogue): every
    /// Storage2State constructed with the same shared slot reports poison to it (mutex-guarded,
    /// first-write-wins), so a read error in ANY per-tx execution instance is visible to the
    /// block-level check that owns the slot.
    explicit Storage2State(
        Storage& storage, std::shared_ptr<SharedErrorSlot> sharedError = {}) noexcept
      : m_storage(storage), m_sharedError(std::move(sharedError))
    {}

    Storage2State(const Storage2State&) = delete;
    Storage2State(Storage2State&&) = delete;
    Storage2State& operator=(const Storage2State&) = delete;
    Storage2State& operator=(Storage2State&&) = delete;
    ~Storage2State() override = default;

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        if (auto it = m_accountCache.find(addr); it != m_accountCache.end())
            return it->second;

        try
        {
            auto fetched = task::syncWait(fetchAccount(accountTableName(addr)));
            m_accountCache.emplace(addr, fetched);
            return fetched;
        }
        // Exception-matching ladder shared by all read methods (and applyDiff): this repo's
        // binaries have unreliable typed-catch behavior — bcos-crypto PUBLICly links
        // wedprcrypto's Rust static libs, whose bundled runtime breaks libc++ exception matching
        // binary-wide (documented at bcos-rpc/test/CMakeLists.txt:29-34). Empirically in the
        // bcos-evm-opstack-tests binary, std::runtime_error-family throws only bind at an
        // exact-type runtime_error handler (they escape catch(std::exception) into catch(...)),
        // while std::logic_error-family throws do bind at catch(std::exception). Keep the
        // runtime_error level so firstError() retains the original message for triage;
        // catch(...) guarantees the poison flag is always set.
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2State::get_account: unknown exception");
        }
        return std::nullopt;
    }

    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        if (auto it = m_codeCache.find(addr); it != m_codeCache.end())
            return it->second;

        try
        {
            auto code = task::syncWait(fetchCode(accountTableName(addr)));
            m_codeCache.emplace(addr, code);
            return code;
        }
        // Exception-matching ladder; see get_account's comment.
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2State::get_account_code: unknown exception");
        }
        return {};
    }

    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override
    {
        auto cacheKey = std::make_pair(addr, key);
        if (auto it = m_storageCache.find(cacheKey); it != m_storageCache.end())
            return it->second;

        try
        {
            auto value = task::syncWait(fetchStorage(accountTableName(addr), key));
            m_storageCache.emplace(cacheKey, value);
            return value;
        }
        // Exception-matching ladder; see get_account's comment.
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2State::get_storage: unknown exception");
        }
        return {};
    }

    /// Consumer contract: check after block execution; once set the whole block must fail.
    /// Also true when the shared block error slot (if any) has been poisoned by ANOTHER instance
    /// over the same view — op-geth's dbErr accumulating across the block. The shared-slot read
    /// takes its mutex; a lock failure (unreachable in correct usage) degrades to false rather
    /// than terminating the noexcept path.
    [[nodiscard]] bool poisoned() const noexcept
    {
        if (m_poisoned)
            return true;
        if (!m_sharedError)
            return false;
        try
        {
            std::lock_guard lock(m_sharedError->mutex);
            return !m_sharedError->message.empty();
        }
        catch (...)
        {
            return false;
        }
    }
    /// Records only the first error; per-instance unless a shared slot is present (then the
    /// block-wide first error is returned — the slot's first-write-wins message, read under its
    /// mutex). Returns std::string, not a view: a view into the shared slot's message would
    /// dangle as soon as the lock is released. The string copies are guarded so a bad_alloc
    /// degrades to {} instead of std::terminate on the noexcept path.
    [[nodiscard]] std::string firstError() const noexcept
    {
        try
        {
            if (m_sharedError)
            {
                std::lock_guard lock(m_sharedError->mutex);
                if (!m_sharedError->message.empty())
                    return m_sharedError->message;
            }
            return m_firstError;
        }
        catch (...)
        {
            return {};
        }
    }

    /// Write-back (single strict form): every modified entry is unconditionally ensure-existed
    /// and serialized through ledger::account::EVMAccount (zero-value slot deletion and account
    /// deletion go via storage2 removeOne/range); each step write-throughs the three read caches
    /// (deleting an account invalidates all three for that address). Not noexcept: strict
    /// tripwire — a deleted_accounts entry missing on the ledger throws.
    ///
    /// PRECONDITION: @p diff must already be sanitized (sanitizeStateDiff) — evmone's diff model
    /// routinely emits phantom deleted_accounts entries (zero-value CALL touch of a never-created
    /// address, access-list/EIP-7702 get_or_insert with erase_if_empty), and the ghost-delete
    /// tripwire below turns an unsanitized diff into a hard block failure on ordinary tx
    /// patterns. opTransition/runDeposit always sanitize before calling; part-3 callers must too.
    ///
    /// Any write-back failure ALSO sets the poison flag before rethrowing. This is error
    /// CLASSIFICATION, not style: OpSchedulerSeam maps poisoned() -> OpStorageError (-32603) and
    /// anything else -> OpConsensusError (INVALID); every failure here (ghost delete, system-
    /// address routing, contract-② zero-slot leak, nonce/width violations, the storage backend
    /// itself) is a LOCAL fault, and the diff comes from evmone itself (malformed payloads were
    /// already rejected at the decode/block-shape gates), so none must ever be answered INVALID.
    /// The whole body is wrapped in one try/catch so every present and future throw point inherits
    /// that invariant; catch(...) guarantees the flag is set even when the standard exception
    /// families cannot be matched. `seeding` (true only for SeedPreState, a
    /// genesis snapshot) exempts the new-EIP-161-empty-account guard, which is otherwise on for
    /// the execution path.
    void applyDiff(const evmone::state::StateDiff& diff, bool seeding = false)
    {
        try
        {
            for (const auto& entry : diff.modified_accounts)
                task::syncWait(applyModifiedEntry(entry, seeding));
            for (const auto& addr : diff.deleted_accounts)
                task::syncWait(applyDeletedEntry(addr));
        }
        // Exception-matching ladder; see get_account's comment. Every write-back failure poisons
        // AND rethrows (tripwire); catch(...) guarantees the flag is set even when the standard
        // exception families cannot be matched. Classification only depends on the flag, not the
        // message.
        catch (const std::runtime_error& e)
        {
            poison(e.what());
            throw;
        }
        catch (const std::exception& e)
        {
            poison(e.what());
            throw;
        }
        catch (...)
        {
            poison("Storage2State::applyDiff: unknown exception on the write-back path");
            throw;
        }
    }

    /// AccountVisitor payload: nonce/balance/codeHash + a lazily-evaluated code
    /// getter (state-root computation never calls it — avoids an unconditional SYS_CODE_BINARY
    /// read per account) + the account's already-materialized, tombstone-filtered,
    /// poison-checked live storage slot map (see fetchAllStorage). Field names mirror
    /// MemoryState::AccountView exactly, and code() returns by value on both backends, so
    /// bcos::evm::stateRootOf<Ledger> and generic visitors work unmodified against either.
    struct AccountView
    {
        const evmc::address& addr;
        uint64_t nonce;
        const intx::uint256& balance;
        evmc::bytes32 codeHash;
        const std::map<evmc::bytes32, evmc::bytes32>& storage;

        [[nodiscard]] evmc::bytes code() const noexcept { return m_bridge->get_account_code(addr); }

        const Storage2State* m_bridge;
    };

    /// Traverses every live account under the /apps/ namespace. noexcept + poison-flag contract
    /// — the same shape as get_account/get_account_code/get_storage: storage2 errors (including
    /// layout-invariant violations — an unknown key in an account table, or a slot value whose
    /// length is not 32 bytes; a stored *zero-valued* slot is NOT one of them) are caught here,
    /// poison() is called, and the traversal stops early returning false. The visitor is invoked
    /// synchronously per live account and must itself return bool: false aborts the traversal
    /// early without poisoning (a plain "visitor is done" signal, distinct from a
    /// poisoned/failed traversal). Consumer contract: after this returns, check poisoned()
    /// before trusting anything the visitor produced.
    template <class Visitor>
    bool visitAccounts(Visitor&& visitor) const noexcept
    {
        try
        {
            return task::syncWait(visitAccountsImpl(visitor));
        }
        // Exception-matching ladder; see get_account's comment. This is the most important of the
        // four read methods: visitAccounts is on the mandatory stateRootOf path — its poison
        // message is the only clue for triaging a -32603.
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2State::visitAccounts: unknown exception");
        }
        return false;
    }

private:
    task::Task<void> applyModifiedEntry(const evmone::state::StateDiff::Entry& entry, bool seeding)
    {
        const std::string tableName = accountTableName(entry.addr);

        // The table name is derived exactly once and shared by reads and writes. Deliberately
        // built via EVMAccount's `FromTableName` constructor rather than
        // `EVMAccount(storage, addr, false)`: the latter routes the 8 c_systemTxsAddress
        // addresses into `/sys/` (EVMAccount.h:239-245), while every read/write here goes to
        // `/apps/` — a mismatch would split-brain (read /apps/, write /sys/). There must be a
        // single derivation of this rule, not two independent copies (two copies of one rule is
        // how the repo's yParity incident happened).
        bcos::ledger::account::EVMAccount<Storage> account(
            m_storage.get(), bcos::ledger::account::FromTableName{}, tableName);

        // Unconditional ensure-exists of the SYS_TABLES marker row (not "skip if nothing to
        // write"): pre-state fully-empty accounts (EIP-161 touch-delete vectors) rely on it, and
        // evmone build_diff puts read-only touched accounts in modified too, where rewriting the
        // same nonce/balance is harmless.
        const bool createdNew = !co_await account.exists();

        // EIP-161: creating a NEW empty account (nonce=0, balance=0, no code) is a protocol
        // violation. build_diff already routes touch-empty accounts to deleted_accounts, so only
        // a default get_or_insert ending empty reaches here. `seeding` (SeedPreState snapshot)
        // exempts; the execution path always guards.
        if (!seeding && createdNew && entry.nonce == 0 && entry.balance == 0 &&
            (!entry.code.has_value() || entry.code->empty()))
        {
            throw std::runtime_error(
                "Storage2State::applyDiff: EIP-161-empty account would be created in the ledger "
                "by a diff entry that never bumped nonce (address table '" +
                tableName + "', §6.4 D-6)");
        }
        if (createdNew)
            co_await account.create();

        // intx → bcos balance via big-endian byte store + fromBigEndian (full-width, no decimal
        // string round-trip) — same path as OpTransition.h's intxToBcosU256.
        auto const balanceBe = intx::be::store<evmc::uint256be>(entry.balance);
        co_await account.setBalance(bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef{
            reinterpret_cast<const bcos::byte*>(balanceBe.bytes), sizeof(balanceBe.bytes)}));
        co_await account.setNonce(std::to_string(entry.nonce));

        // Contract ③: code written only when has_value(); codeHash = keccak(code) (StateDiff has
        // no code_hash field); ABI is not written.
        if (entry.code.has_value())
        {
            const auto codeHash = evmone::keccak256(*entry.code);
            bcos::h256 codeHashValue(
                reinterpret_cast<const bcos::byte*>(codeHash.bytes), sizeof(codeHash.bytes));
            co_await account.setCode(
                bcos::bytes(entry.code->begin(), entry.code->end()), std::string{}, codeHashValue);
        }

        // Contract ②: zero-valued slot deletes the row (removeOne — EVMAccount has no delete
        // API, and a zero row would be skipped by reads but linger); non-zero via setStorage.
        for (const auto& [key, value] : entry.modified_storage)
        {
            if (evmc::is_zero(value))
            {
                std::string_view keyView(
                    reinterpret_cast<const char*>(key.bytes), sizeof(key.bytes));
                co_await storage2::removeOne(
                    m_storage.get(), executor_v1::StateKeyView{tableName, keyView});

                // Contract ② guard: after removeOne, re-read and throw if the row survived —
                // checks the RESULT (a removeOne that no-ops or degrades to a write is caught).
                // existsOne (not liveContent): logical-deletion writes DELETED_TYPE tombstones,
                // which must not count as "present" or every normal zero-write false-positives.
                // The read path no longer guards this (production ledgers write zero values
                // without deleting, HostContext.h:288 / Ledger.cpp:1844).
                if (co_await storage2::existsOne(
                        m_storage.get(), executor_v1::StateKeyView{tableName, keyView}))
                    throw std::runtime_error(
                        "Storage2State::applyDiff: zero-valued slot write left the row alive in "
                        "account table '" +
                        tableName +
                        "' (contract ② write-back leak: the bridge's write-back must never leave a "
                        "zero-valued storage slot row behind — zero means the slot is deleted)");
            }
            else
            {
                co_await account.setStorage(key, value);
            }
        }

        // Write-through: refresh caches via fetchAccount/fetchCode (one set of field-default/
        // has_storage rules); slot cache gets this round's exact written value (zero ->
        // all-zero bytes32, matching read-path normalization of deleted slots).
        // TODO(perf): fetchAccount's probeHasStorage is a full range scan per
        // modified account and fetchCode re-reads SYS_CODE_BINARY — both repeat per tx in a
        // block. A local derivation is possible (rebuild the Account from this round's writes;
        // has_storage: non-zero slot written → true, fresh account without one → false, no
        // storage change with a cached account → cached value stays valid, zero-valued
        // deletions / no cached account → probe, the only safe answer — a stale true would
        // wrongly fail CREATE2 collision checks). Deferred to keep the whole-PR valid-insertion
        // count within the check-commit budget.
        m_accountCache.insert_or_assign(entry.addr, co_await fetchAccount(tableName));
        m_codeCache.insert_or_assign(entry.addr, co_await fetchCode(tableName));
        for (const auto& [key, value] : entry.modified_storage)
        {
            m_storageCache.insert_or_assign(
                std::make_pair(entry.addr, key), evmc::is_zero(value) ? evmc::bytes32{} : value);
        }
    }

    task::Task<void> applyDeletedEntry(const evmc::address& addr)
    {
        const std::string tableName = accountTableName(addr);

        // Single strict form: tripwire built in — a deleted_accounts entry missing on the ledger
        // is a usage error (same existsOne criterion as EVMAccount::exists()); no raw variant is
        // offered.
        if (!co_await storage2::existsOne(
                m_storage.get(), executor_v1::StateKeyView(bcos::ledger::SYS_TABLES, tableName)))
            throw std::runtime_error(
                "Storage2State::applyDiff: deleted_accounts entry not found in ledger (ghost "
                "delete, strict tripwire)");

        // Contract ①: range-scan the account table's field rows and live slot rows, collecting
        // keys first and deleting after the range ends (avoids iterator invalidation).
        // SYS_CODE_BINARY/SYS_CONTRACT_ABI are never touched (content-addressed tables outside
        // this table's key space).
        std::vector<std::string> fieldKeys;
        {
            auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
                executor_v1::StateKeyView{tableName, std::string_view{}});
            while (auto item = co_await iterator.next())
            {
                const auto& key = std::get<0>(*item);
                executor_v1::StateKeyView view(key);
                auto [table, fieldKey] = view.get();
                if (table != tableName)
                    break;
                fieldKeys.emplace_back(fieldKey);
            }
        }
        for (const auto& fieldKey : fieldKeys)
            co_await storage2::removeOne(
                m_storage.get(), executor_v1::StateKeyView{tableName, fieldKey});

        co_await storage2::removeOne(
            m_storage.get(), executor_v1::StateKeyView(bcos::ledger::SYS_TABLES, tableName));

        // Write-through: deleting an account invalidates all three caches for that address
        // (account/code go negative — consistent with the existing nullopt-also-cached design,
        // avoiding an extra syncWait on the next read; slots are erased individually so slots not
        // written this round fall back to a cold read after a CREATE2 same-address rebirth).
        m_accountCache.insert_or_assign(addr, std::nullopt);
        m_codeCache.insert_or_assign(addr, evmc::bytes{});
        std::erase_if(
            m_storageCache, [&addr](const auto& item) { return item.first.first == addr; });
    }


    /// Full account-table scan for visitAccounts: classifies every live row under tableName as
    /// one of ACCOUNT_TABLE_FIELDS (skipped — read separately by fetchAccount) / a 32-byte raw
    /// storage slot key (collected into the returned map) / anything else (a storage2 layout
    /// invariant violation the bridge cannot interpret — throws, caught and poisoned by the
    /// public visitAccounts entry point). Two kinds of row are skipped before they can reach the
    /// returned map, and the two filters are cumulative, not alternatives: tombstoned rows (so a
    /// logically-deleted slot cannot resurrect) and zero-valued slot rows (zero ≡ the slot does
    /// not exist, matching what accountStorageRoot/opStorageRoot already do when building the
    /// trie).
    task::Task<std::map<evmc::bytes32, evmc::bytes32>> fetchAllStorage(std::string tableName) const
    {
        std::map<evmc::bytes32, evmc::bytes32> storage;
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            const auto& rawValue = std::get<1>(*item);
            executor_v1::StateKeyView keyView(key);
            auto [table, fieldKey] = keyView.get();
            if (table != tableName)
                break;

            if (isKnownAccountField(fieldKey))
                continue;

            auto content = liveContent(rawValue);
            if (!content.has_value())
                continue;  // skip tombstone (a logically-deleted slot must not resurrect)

            if (fieldKey.size() != kStorageSlotKeySize)
                throw std::runtime_error(
                    "Storage2State::fetchAllStorage: unknown key in account table '" + tableName +
                    "' (neither a known ACCOUNT_TABLE_FIELDS name nor a 32-byte storage slot "
                    "key)");

            evmc::bytes32 slotKey{};
            std::memcpy(slotKey.bytes, fieldKey.data(), fieldKey.size());

            if (content->size() != sizeof(evmc_bytes32::bytes))
                throw std::length_error(
                    "Storage2State::visitAccounts: storage slot value size mismatch in account "
                    "table '" +
                    tableName + "'");

            evmc::bytes32 slotValue{};
            std::memcpy(slotValue.bytes, content->data(), content->size());

            // Zero-valued slot skip: zero ≡ nonexistent under Ethereum semantics (geth's trie
            // deletes zero-valued slots; accountStorageRoot/opStorageRoot also is_zero -> skip,
            // so zero slots never enter the trie). This used to throw+poison on the theory that
            // the write-back never leaves a zero row, but the read path must also serve production
            // ledgers whose setStorage writes zero values without deleting (HostContext.h:288,
            // Ledger.cpp:1844; a genesis alloc "0x00...00" is one such row), so the read path
            // would poison every OP block. The write-back-leak guard moved to its true home:
            // applyModifiedEntry's post-remove re-read.
            if (evmc::is_zero(slotValue))
                continue;

            storage.emplace(slotKey, slotValue);
        }
        co_return storage;
    }

    /// visitAccounts implementation: range-scans SYS_TABLES for the /apps/ prefix (`/sys/` is a
    /// different prefix and is never scanned; a `c_systemTxsAddress` member with an
    /// `/apps/<40hex>` table is an ordinary account here, collected unconditionally — see
    /// accountTableName for why that is the *required* behaviour, not a missing guard), skips
    /// non-account tables under /apps/ and tombstoned marker rows (same discrimination as
    /// fetchAllStorage), and for each surviving candidate delegates to fetchAccount/
    /// fetchAllStorage (which independently re-verify liveness through existsOne/readOne) before
    /// invoking the visitor.
    template <class Visitor>
    task::Task<bool> visitAccountsImpl(Visitor& visitor) const
    {
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{
                bcos::ledger::SYS_TABLES, bcos::ledger::SYS_DIRECTORY::USER_APPS});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            const auto& rawValue = std::get<1>(*item);
            executor_v1::StateKeyView keyView(key);
            auto [table, tableKey] = keyView.get();
            // Stop once the /apps/ prefix range ends (/sys/ is never scanned).
            if (table != bcos::ledger::SYS_TABLES ||
                !tableKey.starts_with(bcos::ledger::SYS_DIRECTORY::USER_APPS))
                break;

            // Value-variant discrimination: skip NOT_EXISTS_TYPE/DELETED_TYPE tombstone rows, or
            // a just-deleted account would resurrect into the stateRoot.
            if (!liveContent(rawValue).has_value())
                continue;

            // Skip non-account tables: `/apps/` also holds `_accessAuth` authorization tables and
            // BFS link tables `<name>/<version>`, which are not accounts (criteria in
            // addressFromTableName). continue, not break: these names interleave with account
            // names within the /apps/ prefix range, so stopping at one would drop the real
            // accounts after it; only the prefix check above ends the range.
            const auto parsedAddr = addressFromTableName(tableKey);
            if (!parsedAddr.has_value())
                continue;

            const std::string tableName{tableKey};
            const auto addr = *parsedAddr;

            auto account = co_await fetchAccountForVisit(addr, tableName);
            if (!account.has_value())
                continue;  // double defense: same existsOne criterion as the tombstone check above

            const auto storage = co_await fetchAllStorage(tableName);

            const AccountView accountView{.addr = addr,
                .nonce = account->nonce,
                .balance = account->balance,
                .codeHash = account->code_hash,
                .storage = storage,
                .m_bridge = this};
            if (!visitor(accountView))
                co_return false;
        }
        co_return true;
    }

    void poison(std::string_view reason) const noexcept
    {
        if (m_poisoned)
            return;
        m_poisoned = true;
        try
        {
            m_firstError.assign(reason);
            if (m_sharedError)
            {
                std::lock_guard lock(m_sharedError->mutex);
                if (m_sharedError->message.empty())  // first-write-wins across instances
                    m_sharedError->message.assign(m_firstError);
            }
        }
        catch (...)
        {
            // A firstError allocation failure must not rethrow; poisoned() is already set, so the
            // consumer can still detect the failure.
        }
    }


    /// `computeHasStorage=false` skips the `probeHasStorage` range scan. The KEEP contract for
    /// `has_storage` (present-but-empty is NOT nullopt) is untouched: this changes only WHETHER
    /// the field is computed, never what a computed value means. An Account produced with `false`
    /// carries a `has_storage` that must not be published: only `fetchAccountForVisit` uses it,
    /// and it deliberately does not put such an Account into `m_accountCache`, so `get_account`
    /// can never observe one.
    task::Task<std::optional<Account>> fetchAccount(
        std::string tableName, bool computeHasStorage = true) const
    {
        if (!co_await storage2::existsOne(
                m_storage.get(), executor_v1::StateKeyView(bcos::ledger::SYS_TABLES, tableName)))
            co_return std::nullopt;

        Account account{};
        // Empty-account normalization defaults: code_hash = keccak(empty); nonce/balance default
        // to 0 via Account's member initializers; has_storage is probed below.
        account.code_hash = evmone::keccak256(evmc::bytes_view{});

        if (auto balanceEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE});
            balanceEntry && !balanceEntry->get().empty())
        {
            account.balance = intx::from_string<intx::uint256>(std::string(balanceEntry->get()));
        }

        if (auto nonceEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE});
            nonceEntry && !nonceEntry->get().empty())
        {
            // Decimal string -> wide type (intx::uint256) -> explicit > UINT64_MAX check -> then
            // narrow. Never parse/narrow straight to uint64_t — avoids relying on undefined
            // overflow behaviour of some conversion (repo precedent: convert_to<int64_t> silently
            // truncates out-of-range values).
            auto nonceValue = intx::from_string<intx::uint256>(std::string(nonceEntry->get()));
            static constexpr intx::uint256 maxUint64{std::numeric_limits<uint64_t>::max()};
            if (nonceValue > maxUint64)
                throw std::overflow_error(
                    "Storage2State::fetchAccount: nonce exceeds uint64_t range (silent-"
                    "truncation guard, design §4.3)");
            account.nonce = static_cast<uint64_t>(nonceValue);
        }

        if (auto codeHashEntry = co_await storage2::readOne(
                m_storage.get(), executor_v1::StateKeyView{tableName,
                                     bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH});
            codeHashEntry && !codeHashEntry->get().empty())
        {
            auto view = codeHashEntry->get();
            if (view.size() != sizeof(account.code_hash.bytes))
                throw std::length_error(
                    "Storage2State::fetchAccount: codeHash field size mismatch");
            std::memcpy(account.code_hash.bytes, view.data(), view.size());
        }

        if (computeHasStorage)
        {
            account.has_storage = co_await probeHasStorage(tableName);
        }
        co_return account;
    }

    /// Account lookup for `visitAccountsImpl`. Two savings over calling `fetchAccount` directly:
    /// a cache HIT reuses the entry `get_account`/`applyModifiedEntry` already populated (the
    /// cache is write-through, so it is authoritative here); a cache MISS skips `probeHasStorage`,
    /// because `has_storage` is not part of the `AccountView` the visitor receives and is not read
    /// on the `stateRootOf` path (and that probe is a full range scan per account). A miss result
    /// is deliberately NOT cached: it lacks `has_storage`, and `get_account` must never be served
    /// an Account with an uncomputed field.
    task::Task<std::optional<Account>> fetchAccountForVisit(
        const evmc::address& addr, const std::string& tableName) const
    {
        if (auto it = m_accountCache.find(addr); it != m_accountCache.end())
        {
            co_return it->second;
        }
        co_return co_await fetchAccount(tableName, /*computeHasStorage=*/false);
    }

    /// has_storage rule: range-seek the account table for the first live (non-tombstone) 32-byte
    /// raw key whose value is non-zero (distinct from the known short ACCOUNT_TABLE_FIELDS names).
    /// PRECONDITION on the Storage parameter: range() must be globally ordered (production:
    /// ordered memory layers over RocksDB). An unordered backend (MemoryStorage CONCURRENT
    /// iterates hash buckets) interleaves foreign tables mid-range and defeats the early exit.
    /// Two cumulative filters, each fixing a real bug:
    ///   * tombstone layer: range scanning without value-variant discrimination would count a
    ///     logically-deleted (DELETED_TYPE) row as a live slot, so has_storage could not flip back
    ///     to false after "delete down to the last slot" — filtered with liveContent().
    ///   * zero-value layer: a live but zero-valued slot row means the slot does not exist under
    ///     Ethereum semantics, and misjudging it as true is a CONSENSUS bug, not a performance one:
    ///     has_storage -> .has_initial_storage -> is_create_collision (state.cpp:259, host.cpp:91),
    ///     so the same CREATE2 succeeds on op-geth but is INVALID here. Production ledgers
    ///     necessarily contain zero-valued slot rows (HostContext.h:288 / Ledger.cpp:1844 write
    ///     zeros without deleting).
    /// The KEEP contract is unaffected: "zero slot = no slot" and "account present with all
    /// default fields = account present" are different; the latter is still guaranteed by
    /// fetchAccount's existsOne returning an Account rather than nullopt.
    task::Task<bool> probeHasStorage(std::string_view tableName) const
    {
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            const auto& rawValue = std::get<1>(*item);
            executor_v1::StateKeyView view(key);
            auto [table, fieldKey] = view.get();
            if (table != tableName)
                co_return false;
            if (fieldKey.size() == kStorageSlotKeySize)
            {
                if (auto content = liveContent(rawValue);
                    content.has_value() && !isZeroSlotValue(*content))
                    co_return true;
            }
            // Known field name (codeHash/code/balance/abi/nonce/alive/frozen/shard), tombstoned
            // slot (logical deletion, liveContent negative), or all-zero slot row (zero ≡
            // nonexistent) — keep scanning; none of these decides has_storage=true early.
        }
        co_return false;
    }

    /// get_account_code: read via CODE_HASH -> SYS_CODE_BINARY. This bridge serves only the
    /// storage2 stack and does not reproduce EVMAccount::code()'s fallback to the legacy CODE
    /// field.
    task::Task<evmc::bytes> fetchCode(std::string tableName) const
    {
        auto codeHashEntry = co_await storage2::readOne(m_storage.get(),
            executor_v1::StateKeyView{tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH});
        if (!codeHashEntry || codeHashEntry->get().empty())
            co_return evmc::bytes{};

        auto codeHashView = codeHashEntry->get();
        if (codeHashView.size() != sizeof(evmc::bytes32::bytes))
            throw std::length_error(
                "Storage2State::fetchCode: codeHash field size mismatch in account table '" +
                tableName + "'");

        // keccak(empty) is the canonical "no code" marker: fetchAccount normalizes an absent
        // CODE_HASH row to exactly this value, SYS_CODE_BINARY holds no blob for it, and an
        // empty code must not trip the corruption tripwire below.
        evmc::bytes32 codeHash{};
        std::memcpy(codeHash.bytes, codeHashView.data(), codeHashView.size());
        if (codeHash == evmone::keccak256(evmc::bytes_view{}))
            co_return evmc::bytes{};

        // A non-empty CODE_HASH with no matching blob is ledger data corruption (setCode writes
        // the blob and the CODE_HASH row together). Silently returning empty would run a
        // code-bearing contract as a CODELESS account — "executed successfully" with a wrong
        // state and poisoned()==false, invisible to the block-level check. Throw instead: the
        // read path's catch ladder poisons, and the block fails as -32603 (OpStorageError) —
        // never a silent wrong state.
        auto codeEntry = co_await storage2::readOne(m_storage.get(),
            executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY, codeHashView});
        if (!codeEntry)
            throw std::runtime_error("Storage2State::fetchCode: account table '" + tableName +
                                     "' has a non-empty CODE_HASH but no matching blob in " +
                                     std::string(bcos::ledger::SYS_CODE_BINARY) +
                                     " (ledger data corruption: code blob missing)");

        auto view = codeEntry->get();
        co_return evmc::bytes(view.begin(), view.end());
    }

    task::Task<evmc::bytes32> fetchStorage(std::string tableName, evmc::bytes32 key) const
    {
        std::string_view keyView(reinterpret_cast<const char*>(key.bytes), sizeof(key.bytes));
        if (auto entry = co_await storage2::readOne(
                m_storage.get(), executor_v1::StateKeyView{tableName, keyView}))
        {
            auto view = entry->get();
            // A slot value whose length != 32 bytes used to silently return an all-zero slot,
            // conflating a storage-layout violation with a legit zero. Now validated like
            // fetchAllStorage — throw, caught by the get_storage read path and poisoned, so
            // callers never treat corrupt data as a zero value.
            if (view.size() != sizeof(evmc_bytes32::bytes))
                throw std::length_error(
                    "Storage2State::fetchStorage: storage slot value size mismatch in account "
                    "table '" +
                    tableName + "'");
            evmc::bytes32 value{};
            std::memcpy(value.bytes, view.data(), view.size());
            co_return value;
        }
        co_return evmc::bytes32{};
    }

    std::reference_wrapper<Storage> m_storage;

    // Three block-level read caches; nullopt/zero values are cached too (kills negative lookups).
    // Write-through maintained by applyDiff; no reset() — caches live and die with the instance.
    mutable std::map<evmc::address, std::optional<Account>> m_accountCache;
    mutable std::map<evmc::address, evmc::bytes> m_codeCache;
    mutable std::map<std::pair<evmc::address, evmc::bytes32>, evmc::bytes32> m_storageCache;

    mutable bool m_poisoned{false};
    mutable std::string m_firstError;
    std::shared_ptr<SharedErrorSlot> m_sharedError;  // optional block-wide error slot (op-geth
                                                     // dbErr)
};

}  // namespace bcos::evm::evmstate
