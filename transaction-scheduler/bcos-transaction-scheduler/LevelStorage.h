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

public:
    LevelStorage(BackendStorage& backendStorage) : m_backendStorage(backendStorage) {}

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

    void pushFront()
    {
        std::unique_lock lock(m_storageMutex);
        m_immutableStorages.push_front(m_mutableStorage);
        m_mutableStorage.reset();
    }

    void popFront()
    {
        std::unique_lock lock(m_storageMutex);
        m_immutableStorages.pop_front();
    }

    void popAndMergeBack() {}
};
}  // namespace bcos::transaction_scheduler