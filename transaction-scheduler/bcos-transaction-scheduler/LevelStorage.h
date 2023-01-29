#pragma once
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <boost/throw_exception.hpp>

namespace bcos::transaction_scheduler
{
struct MutableStorageExists : public bcos::Error
{
};

template <transaction_executor::StateStorage BackendStorage,
    transaction_executor::StateStorage Storage>
class LevelStorage
{
private:
    std::shared_ptr<Storage> m_mutableStorage;
    std::list<std::shared_ptr<Storage>> m_immutableStorages;  // Read only storages
    BackendStorage& m_backendStorage;                         // Backend storage

    std::mutex m_storageMutex;

    template <transaction_executor::StateStorage ReadableStorage>
    auto readFromStorage(ReadableStorage& storage, RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_mutableStorage.read(keys))>>
    {
        auto it = storage.read(keys);
        co_return readFromStorage();
    }

public:
    LevelStorage(BackendStorage& backendStorage) : m_backendStorage(backendStorage) {}

    class ReadIterator
    {
    public:
        friend class LevelStorage;
        using Key = const transaction_executor::StateKey&;
        using Value = const transaction_executor::StateValue&;

        task::Task<bool> hasValue() const { co_return true; }
        task::Task<bool> next() & {}
        task::Task<Key> key() const {}
        task::Task<Value> value() const {}

        void release() {}
    };

    LevelStorage fork()
    {
        std::unique_lock lock(m_storageMutex);
        LevelStorage levelStorage(m_backendStorage);
        levelStorage.m_immutableStorages = m_immutableStorages;

        return levelStorage;
    }

    template <class... Args>
    void newMutable(Args... args)
    {
        std::unique_lock lock(m_storageMutex);
        if (m_mutableStorage)
        {
            BOOST_THROW_EXCEPTION(MutableStorageExists{});
        }

        m_mutableStorage = std::make_shared<Storage>(args...);
    }

    void dropMutable() { m_mutableStorage.reset(); }

    void pushMutableToImmutableFront()
    {
        std::unique_lock lock(m_storageMutex);
        m_immutableStorages.push_front(m_mutableStorage);
        m_mutableStorage.reset();
    }

    void popImmutableFront()
    {
        std::unique_lock lock(m_storageMutex);
        m_immutableStorages.pop_front();
    }

    void mergeAndPopImmutableBack() {}

    auto read(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_mutableStorage.read(keys))>>
    {
        if (m_mutableStorage)
        {
            auto it = m_mutableStorage->read(keys);
        }
    }
};
}  // namespace bcos::transaction_scheduler