#pragma once

#include "../../src/Common.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-table/src/StateStorage.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos::protocol;

namespace bcos::test
{
class MockTransactionalStorage : public bcos::storage::TransactionalStorageInterface
{
public:
    MockTransactionalStorage(bcos::crypto::Hash::Ptr hashImpl) : m_hashImpl(std::move(hashImpl))
    {
        m_inner = std::make_shared<bcos::storage::StateStorage>(nullptr, false);
        m_inner->setEnableTraverse(true);
    }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) noexcept override
    {
        m_inner->asyncGetPrimaryKeys(table, _condition, std::move(_callback));
    }

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> _callback) noexcept
        override
    {
        // Test-only fault injection (review F2 regression): when armed for an exact
        // (table, key) pair, answer with a storage error — StorageWrapper::
        // getRowInternal converts an Error response into a thrown bcos::Error, the
        // real storage-fault shape the host callbacks see.
        if (m_faultTable == table && m_faultKey == _key)
        {
            std::cerr << "F2TEST: fault FIRED table=" << table << " key=" << _key << std::endl;
            _callback(BCOS_ERROR_UNIQUE_PTR(-1, "injected storage fault (F2 test)"),
                std::nullopt);
            return;
        }
        m_inner->asyncGetRow(table, _key, std::move(_callback));
    }

    void armRowFault(std::string table, std::string key)
    {
        m_faultTable = std::move(table);
        m_faultKey = std::move(key);
    }

    void disarmRowFault()
    {
        m_faultTable.clear();
        m_faultKey.clear();
    }

    void asyncGetRows(std::string_view table,
        ::ranges::any_view<std::string_view,
            ::ranges::category::input | ::ranges::category::random_access | ::ranges::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)>
            _callback) noexcept override
    {
        m_inner->asyncGetRows(table, keys, std::move(_callback));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) noexcept override
    {
        m_inner->asyncSetRow(table, key, std::move(entry), std::move(callback));
    }

    void setRow(std::string_view table, std::string_view key, storage::Entry entry) noexcept
    {
        std::promise<Error::UniquePtr> promise;
        m_inner->asyncSetRow(table, key, std::move(entry),
            [&](auto&& error) { promise.set_value(std::move(error)); });
        promise.get_future().get();
    }

    void asyncOpenTable(std::string_view tableName,
        std::function<void(Error::UniquePtr, std::optional<storage::Table>)> callback) noexcept
        override
    {
        m_inner->asyncOpenTable(tableName, std::move(callback));
    }

    void asyncPrepare(const TwoPCParams& params,
        const bcos::storage::TraverseStorageInterface& storage,
        std::function<void(Error::Ptr, uint64_t, const std::string&)> callback) noexcept override
    {
        BOOST_CHECK_GT(params.number, 0);

        std::mutex mutex;
        storage.parallelTraverse(
            true, [&](const std::string_view& table, const std::string_view& key,
                      const storage::Entry& entry) {
                std::unique_lock<std::mutex> lock(mutex);

                auto keyHex = boost::algorithm::hex_lower(std::string(key));
                // EXECUTOR_LOG(TRACE) << "Merge data" << LOG_KV("table", table)
                //                 << LOG_KV("key", keyHex) << LOG_KV("fields", fields);

                auto myTable = m_inner->openTable(table);
                if (!myTable)
                {
                    m_inner->createTable(std::string(table), std::string(executor::STORAGE_VALUE));
                    myTable = m_inner->openTable(std::string(table));
                }
                myTable->setRow(key, entry);

                return true;
            });

        callback(nullptr, 0, "");
    }

    void asyncCommit(const TwoPCParams& params,
        std::function<void(Error::Ptr, uint64_t)> callback) noexcept override
    {
        BOOST_CHECK_GT(params.number, 0);
        callback(nullptr, 0);
    }

    void asyncRollback(
        const TwoPCParams& params, std::function<void(Error::Ptr)> callback) noexcept override
    {
        BOOST_CHECK_GT(params.number, 0);
        callback(nullptr);
    }

    bcos::storage::StateStorage::Ptr m_inner;
    std::string m_faultTable;
    std::string m_faultKey;
    bcos::crypto::Hash::Ptr m_hashImpl;
};
}  // namespace bcos::test