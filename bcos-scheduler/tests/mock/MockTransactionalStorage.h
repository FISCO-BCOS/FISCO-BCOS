#pragma once

#include "bcos-framework/interfaces/ledger/LedgerInterface.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/libstorage/StateStorage.h"

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockTransactionalStorage : public bcos::storage::TransactionalStorageInterface
{
public:
    ~MockTransactionalStorage() override = default;

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) noexcept override
    {
        m_storage->asyncGetPrimaryKeys(table, _condition, std::move(_callback));
    }

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> _callback) noexcept
        override
    {
        m_storage->asyncGetRow(table, _key, std::move(_callback));
    }

    void asyncGetRows(std::string_view table,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)>
            _callback) noexcept override
    {
        // if(table == ledge)
        if (table == ledger::SYS_CONFIG)
        {
            // Return 3 dataes
            std::vector<std::optional<storage::Entry>> result;
            storage::Entry entry1;
            entry1.importFields({"100"});
            result.emplace_back(std::move(entry1));

            storage::Entry entry2;
            entry2.importFields({"200"});
            result.emplace_back(std::move(entry2));

            storage::Entry entry3;
            entry3.importFields({"300"});
            result.emplace_back(std::move(entry3));

            _callback(nullptr, std::move(result));
            return;
        }

        m_storage->asyncGetRows(table, _keys, std::move(_callback));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) noexcept override
    {
        m_storage->asyncSetRow(table, key, std::move(entry), std::move(callback));
    }

    void asyncPrepare(const TwoPCParams& params, const storage::TraverseStorageInterface& storage,
        std::function<void(Error::Ptr, uint64_t)> callback) noexcept override
    {
        callback(nullptr, 0);
    }

    void asyncCommit(
        const TwoPCParams& params, std::function<void(Error::Ptr)> callback) noexcept override
    {
        callback(nullptr);
    }

    void asyncRollback(
        const TwoPCParams& params, std::function<void(Error::Ptr)> callback) noexcept override
    {
        callback(nullptr);
    }

    bcos::storage::StateStorage::Ptr m_storage;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test