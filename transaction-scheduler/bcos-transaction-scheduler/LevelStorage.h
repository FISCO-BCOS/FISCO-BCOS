#pragma once
#include <bcos-framework/transaction-executor/TransactionExecutor.h>

namespace bcos::transaction_scheduler
{
template <transaction_executor::StateStorage BackendStorage,
    transaction_executor::StateStorage Storage>
class LevelStorage
{
private:
    Storage& activeStorage;
    std::list<std::shared_ptr<Storage>> m_levelStorage;
    BackendStorage& m_backendStorage;

public:
    LevelStorage(BackendStorage& backendStorage) : m_backendStorage(backendStorage) {}

    LevelStorage fork() {}

    void newLayer() { m_levelStorage.emplace_front(); }
};
}  // namespace bcos::transaction_scheduler