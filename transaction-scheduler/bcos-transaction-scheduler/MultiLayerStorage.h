#pragma once
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-utilities/AnyHolder.h>
#include <boost/throw_exception.hpp>
#include <range/v3/range/access.hpp>
#include <range/v3/range_fwd.hpp>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace bcos::transaction_scheduler
{

// clang-format off
struct DuplicateMutableStorageError : public bcos::Error {};
struct NotExistsMutableStorageError : public bcos::Error {};
struct NotExistsImmutableStorageError : public bcos::Error {};
// clang-format on

template <transaction_executor::StateStorage MutableStorage, class CachedStorage,
    transaction_executor::StateStorage BackendStorage>
requires((std::is_void_v<CachedStorage> || (transaction_executor::StateStorage<CachedStorage>)) &&
         storage2::SeekableStorage<MutableStorage>) class MultiLayerStorage
{
private:
    static_assert(std::same_as<typename MutableStorage::Key, typename BackendStorage::Key>);
    static_assert(std::same_as<typename MutableStorage::Value, typename BackendStorage::Value>);

    constexpr static bool withCacheStorage = !std::is_void_v<CachedStorage>;

    std::shared_ptr<MutableStorage> m_mutableStorage;
    std::list<std::shared_ptr<MutableStorage>> m_immutableStorages;  // Ledger read data from here
    [[no_unique_address]] std::conditional_t<withCacheStorage,
        std::add_lvalue_reference_t<CachedStorage>, std::monostate>
        m_cacheStorage;
    BackendStorage& m_backendStorage;

    std::mutex m_immutablesMutex;
    std::mutex m_mergeMutex;

public:
    using Key = typename MutableStorage::Key;
    using Value = typename MutableStorage::Value;

    template <RANGES::input_range KeyRange, transaction_executor::StateStorage... StorageType>
    class ReadIterator
    {
    private:
        utilities::AnyHolder<KeyRange> m_keyRange;
        MultiLayerStorage& m_storage;
        RANGES::iterator_t<KeyRange const> m_keyRangeIt;
        mutable std::variant<std::monostate, storage2::ReadIteratorType<StorageType>...> m_innerIt;
        bool m_started = false;

        // Query from top to buttom
        task::Task<void> query(Key key) const
        {
            if (m_innerIt.index() != 0)
            {
                co_return;
            }

            if (m_storage.m_mutableStorage &&
                co_await queryAndSetIt(*m_storage.m_mutableStorage, key))
            {
                co_return;
            }

            for (auto& storage : m_storage.m_immutableStorages)
            {
                if (co_await queryAndSetIt(*storage, key))
                {
                    co_return;
                }
            }

            if constexpr (withCacheStorage)
            {
                if (co_await queryAndSetIt(m_storage.m_cacheStorage, key))
                {
                    co_return;
                }
            }

            if (co_await queryAndSetIt(m_storage.m_backendStorage, key))
            {
                co_return;
            }
        }

        task::Task<bool> queryAndSetIt(auto& storage, auto const& key) const
        {
            auto it = co_await storage.read(storage2::single(key));
            co_await it.next();
            if (co_await it.hasValue())
            {
                m_innerIt.template emplace<decltype(it)>(std::move(it));
                co_return true;
            }
            co_return false;
        }

    public:
        using Key = MultiLayerStorage::Key const&;
        using Value = MultiLayerStorage::Value const&;

        ReadIterator(auto&& keyRange, MultiLayerStorage& storage)
          : m_keyRange(std::forward<decltype(keyRange)>(keyRange)), m_storage(storage)
        {
            if constexpr (withCacheStorage)
            {
                static_assert(
                    std::is_same_v<typename MutableStorage::Key, typename CachedStorage::Key> &&
                    std::is_same_v<typename MutableStorage::Value, typename CachedStorage::Value>);
            }
        }

        ReadIterator(const ReadIterator&) = delete;
        ReadIterator(ReadIterator&& rhs) noexcept = default;
        ReadIterator& operator=(const ReadIterator&) = delete;
        ReadIterator& operator=(ReadIterator&&) noexcept = default;
        ~ReadIterator() noexcept = default;

        task::AwaitableValue<bool> next()
        {
            m_innerIt = std::monostate{};
            if (!m_started)
            {
                m_started = true;

                auto& range = m_keyRange.get();
                m_keyRangeIt = RANGES::begin(range);
                return m_keyRangeIt != RANGES::end(m_keyRange.get());
            }
            return ++m_keyRangeIt != RANGES::end(m_keyRange.get());
        }
        task::Task<bool> hasValue() const
        {
            co_await query(*m_keyRangeIt);
            co_return co_await std::visit(
                [](auto&& iterator) -> task::Task<bool> {
                    using IteratorType = std::remove_cvref_t<decltype(iterator)>;
                    if constexpr (!std::is_same_v<std::monostate, IteratorType>)
                    {
                        co_return co_await iterator.hasValue();
                    }
                    co_return false;
                },
                m_innerIt);
        }
        task::Task<Key> key() const
        {
            co_await query(*m_keyRangeIt);
            co_return co_await std::visit(
                [](auto&& iterator) -> task::Task<Key> {
                    using IteratorType = std::remove_cvref_t<decltype(iterator)>;
                    if constexpr (!std::is_same_v<std::monostate, IteratorType>)
                    {
                        co_return co_await iterator.key();
                    }
                },
                m_innerIt);
        }
        task::Task<Value> value() const
        {
            co_await query(*m_keyRangeIt);
            co_return co_await std::visit(
                [](auto&& iterator) -> task::Task<Value> {
                    using IteratorType = std::remove_cvref_t<decltype(iterator)>;
                    if constexpr (!std::is_same_v<std::monostate, IteratorType>)
                    {
                        co_return co_await iterator.value();
                    }
                },
                m_innerIt);
        }

        void release() {}
    };

    MultiLayerStorage(BackendStorage& backendStorage) requires(!withCacheStorage)
      : m_backendStorage(backendStorage)
    {}

    MultiLayerStorage(BackendStorage& backendStorage,
        std::conditional_t<withCacheStorage, std::add_lvalue_reference_t<CachedStorage>,
            std::monostate>
            cacheStorage) requires(withCacheStorage)
      : m_backendStorage(backendStorage), m_cacheStorage(cacheStorage)
    {}
    MultiLayerStorage(const MultiLayerStorage&) = delete;
    MultiLayerStorage(MultiLayerStorage&&) = delete;
    MultiLayerStorage& operator=(const MultiLayerStorage&) = delete;
    MultiLayerStorage& operator=(MultiLayerStorage&&) = delete;

    auto read(RANGES::input_range auto&& keys) -> task::AwaitableValue<
        ReadIterator<std::remove_cvref_t<decltype(keys)>, BackendStorage, MutableStorage>>
    {
        auto it = ReadIterator<std::remove_cvref_t<decltype(keys)>, BackendStorage, MutableStorage>(
            std::forward<decltype(keys)>(keys), *this);
        return task::AwaitableValue<decltype(it)>(std::move(it));
    }

    task::Task<void> write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }

        co_await m_mutableStorage->write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
        co_return;
    }

    task::Task<void> remove(RANGES::input_range auto const& keys)
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }

        co_await m_mutableStorage->remove(keys);
        co_return;
    }

    std::unique_ptr<MultiLayerStorage> fork()
    {
        std::scoped_lock lock(m_mergeMutex, m_immutablesMutex);  // TODO: 读提交完还是执行完
        auto newMultiLayerStorage = std::make_unique<MultiLayerStorage>(m_backendStorage);
        newMultiLayerStorage->m_immutableStorages = m_immutableStorages;

        return newMultiLayerStorage;
    }

    template <class... Args>
    void newMutable(Args... args)
    {
        std::unique_lock lock(m_immutablesMutex);
        if (m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(DuplicateMutableStorageError{});
        }

        m_mutableStorage = std::make_shared<MutableStorage>(args...);
    }

    void dropMutable() { m_mutableStorage.reset(); }

    void pushMutableToImmutableFront()
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }
        std::unique_lock lock(m_immutablesMutex);
        m_immutableStorages.push_front(m_mutableStorage);
        m_mutableStorage.reset();
    }

    void popImmutableFront()
    {
        std::unique_lock lock(m_immutablesMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }
        m_immutableStorages.pop_front();
    }

    task::Task<void> mergeAndPopImmutableBack()
    {
        std::unique_lock mergeLock(m_mergeMutex);
        std::unique_lock immutablesLock(m_immutablesMutex);
        if (m_immutableStorages.empty())
        {
            BOOST_THROW_EXCEPTION(NotExistsImmutableStorageError{});
        }
        auto immutableStorage = std::move(m_immutableStorages.back());
        m_immutableStorages.pop_back();
        immutablesLock.unlock();

        // TODO: Transactional
        auto it = co_await immutableStorage->seek(transaction_executor::EMPTY_STATE_KEY);
        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                co_await storage2::writeOne(
                    m_backendStorage, co_await it.key(), co_await it.value());
            }
            else
            {
                co_await storage2::removeOne(m_backendStorage, co_await it.key());
            }
        }
    }

    MutableStorage& mutableStorage()
    {
        if (!m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(NotExistsMutableStorageError{});
        }
        return *m_mutableStorage;
    }
    BackendStorage& backendStorage() { return m_backendStorage; }
};
}  // namespace bcos::transaction_scheduler