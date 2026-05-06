#pragma once
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Trait.h>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{

template <class StorageType>
class ReadWriteSetStorage
{
private:
    std::reference_wrapper<std::remove_reference_t<StorageType>> m_storage;

public:
    using Key = std::decay_t<StorageType>::Key;
    using Value = std::decay_t<StorageType>::Value;
    // FIB-109: Savepoint is the size of the mutation log; rollback pops the
    // log entries added after it and restores m_readWriteSet to the state it
    // had when the savepoint was taken.
    using Savepoint = std::size_t;

    ReadWriteSetStorage(StorageType& storage) : m_storage(std::ref(storage)) {}

private:
    struct ReadWriteFlag
    {
        bool read = false;
        bool write = false;
    };
    struct LogEntry
    {
        size_t hash;
        // nullopt => entry did not exist before putSet, so rollback erases it;
        // otherwise rollback restores these flags.
        std::optional<ReadWriteFlag> previous;
    };

    std::unordered_map<size_t, ReadWriteFlag> m_readWriteSet;
    std::vector<LogEntry> m_log;

    using Storage = StorageType;

    void putSet(bool write, size_t hash)
    {
        auto [it, inserted] =
            m_readWriteSet.try_emplace(hash, ReadWriteFlag{.read = !write, .write = write});
        if (inserted)
        {
            m_log.push_back(LogEntry{.hash = hash, .previous = std::nullopt});
            return;
        }
        ReadWriteFlag const previous = it->second;
        it->second.write |= write;
        it->second.read |= (!write);
        if (it->second.read != previous.read || it->second.write != previous.write)
        {
            m_log.push_back(LogEntry{.hash = hash, .previous = previous});
        }
    }
    void putSet(bool write, auto const& key)
    {
        auto hash = std::hash<Key>{}(key);
        putSet(write, hash);
    }

public:
    auto readSomeRaw(::ranges::input_range auto keys, auto&&... args)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.get().readSomeRaw(
            std::move(keys), std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await m_storage.get().readSomeRaw(
            std::move(keys), std::forward<decltype(args)>(args)...);
    }

    auto readOneRaw(auto key, auto&&... args)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.get().readOneRaw(
            std::move(key), std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await m_storage.get().readOneRaw(
            std::move(key), std::forward<decltype(args)>(args)...);
    }

    // Transaction-visible reads must be tracked so that hasRAWIntersection
    // can detect a chunk reading a key written by an earlier chunk.
    auto readSome(::ranges::input_range auto keys) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        for (auto&& key : keys)
        {
            putSet(false, key);
        }
        co_return co_await storage2::readSome(m_storage.get(), std::move(keys));
    }

    auto readOne(auto key)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
            std::add_lvalue_reference_t<Storage>, decltype(key)>>>
    {
        putSet(false, key);
        co_return co_await storage2::readOne(m_storage.get(), std::move(key));
    }

    // FIB-100: DIRECT reads are Rollbackable's pre-image snapshots — internal
    // bookkeeping, not transaction-visible reads. Tracking them would create
    // phantom read dependencies (a chunk that only wrote a key would look
    // like it also read it, triggering false RAW). Rollback correctness is
    // protected instead by WAW detection in hasRAWIntersection: two chunks
    // writing the same key are forced to serialize, so the "read stale
    // pre-image" window cannot open.
    auto readSome(::ranges::input_range auto keys, storage2::DIRECT_TYPE direct)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome, Storage&,
            decltype(keys), storage2::DIRECT_TYPE>>>
    {
        co_return co_await storage2::readSome(m_storage.get(), std::move(keys), direct);
    }

    auto readOne(auto key, storage2::DIRECT_TYPE direct) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key), storage2::DIRECT_TYPE>>>
    {
        co_return co_await storage2::readOne(m_storage.get(), std::move(key), direct);
    }

    auto existsOne(auto key) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ExistsOne, Storage&, decltype(key)>>>
    {
        putSet(false, key);
        co_return co_await storage2::existsOne(m_storage.get(), std::move(key));
    }

    auto writeOne(auto key, auto value) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::WriteOne, Storage&, decltype(key), decltype(value)>>>
    {
        putSet(true, key);
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value));
    }

    // FIB-106: forward_range is required so the two-pass pattern (track keys
    // then forward to backend) does not silently consume a single-pass input.
    auto writeSome(::ranges::forward_range auto keyValues) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::WriteSome, Storage&, decltype(keyValues)>>>
    {
        for (auto&& [key, _] : keyValues)
        {
            putSet(true, key);
        }
        co_return co_await storage2::writeSome(m_storage.get(), std::move(keyValues));
    }

    task::Task<void> removeOne(auto key, auto&&... args)
    {
        putSet(true, key);
        co_await storage2::removeOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    auto removeSome(::ranges::forward_range auto keys, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome, Storage&,
            decltype(keys), decltype(args)...>>>
    {
        for (auto&& key : keys)
        {
            putSet(true, key);
        }
        co_return co_await storage2::removeSome(
            m_storage.get(), std::move(keys), std::forward<decltype(args)>(args)...);
    }

    // NOTE: range() does not track reads because it returns a lazy iterator whose
    // keys are not known until consumption. This is a known limitation (FIB-100).
    // range() on ReadWriteSetStorage is not invoked during parallel chunk
    // execution — the only production caller is BaselineScheduler's
    // calculateStateRoot, which runs after all chunks have merged and hashes
    // the committed storage directly (bypassing this wrapper). EVM /
    // precompiled / ledger / account paths do not use range either. No read
    // tracking required.
    auto range(auto&&... args) -> task::Task<
        storage2::ReturnType<std::invoke_result_t<storage2::Range, Storage&, decltype(args)...>>>
    {
        co_return co_await storage2::range(m_storage.get(), std::forward<decltype(args)>(args)...);
    }


    // FIB-109: Savepoint / rollback API that pairs with Rollbackable. See
    // RollbackableStorage.h — Rollbackable forwards rollback() to its inner
    // storage, so a Savepoint taken on Rollbackable<ReadWriteSetStorage<...>>
    // captures both levels and restores this set when the transaction reverts.
    Savepoint current() const noexcept { return m_log.size(); }

    task::Task<void> rollback(Savepoint savepoint)
    {
        while (m_log.size() > savepoint)
        {
            auto& entry = m_log.back();
            if (entry.previous)
            {
                m_readWriteSet[entry.hash] = *entry.previous;
            }
            else
            {
                m_readWriteSet.erase(entry.hash);
            }
            m_log.pop_back();
        }
        co_return;
    }

    friend auto& readWriteSet(ReadWriteSetStorage& storage) { return storage.m_readWriteSet; }
    friend auto const& readWriteSet(ReadWriteSetStorage const& storage)
    {
        return storage.m_readWriteSet;
    }

    friend void mergeWriteSet(ReadWriteSetStorage& storage, auto& inputWriteSet)
    {
        auto& writeMap = readWriteSet(inputWriteSet);
        for (auto& [key, flag] : writeMap)
        {
            if (flag.write)
            {
                storage.putSet(true, key);
            }
        }
    }

    // Conflict detection between a prior-chunks aggregated write set (lhs) and
    // the current chunk's tracked set (rhs). Returns true when the current
    // chunk either:
    //   * read a key that a prior chunk wrote (RAW), or
    //   * wrote a key that a prior chunk wrote (WAW) — FIB-100: rollback
    //     semantics make writes implicitly read-dependent on the pre-image,
    //     so WAW must serialize to keep Rollbackable from snapshotting a
    //     stale value.
    friend bool hasRAWIntersection(ReadWriteSetStorage const& lhs, const auto& rhs)
    {
        auto const& lhsSet = readWriteSet(lhs);
        auto const& rhsSet = readWriteSet(rhs);

        if (::ranges::empty(lhsSet) || ::ranges::empty(rhsSet))
        {
            return false;
        }

        for (auto const& [key, flag] : rhsSet)
        {
            if ((flag.read || flag.write) && lhsSet.contains(key))
            {
                return true;
            }
        }

        return false;
    }
};

}  // namespace bcos::scheduler_v1
