#pragma once

#include "../Common.h"
#include "StorageWrapper.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <boost/iterator/iterator_categories.hpp>
#include <boost/throw_exception.hpp>
#include <optional>
#include <thread>
#include <vector>

namespace bcos::executor
{
using KeyLockResponse = SetRowResponse;
using AcquireKeyLockResponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;

class SyncStorageWrapper : public StorageWrapper
{
public:
    using Ptr = std::shared_ptr<SyncStorageWrapper>;

    SyncStorageWrapper(storage::StateStorageInterface::Ptr storage,
        std::function<void(std::string)> externalAcquireKeyLocks,
        bcos::storage::Recoder::Ptr recoder)
      : StorageWrapper(storage, recoder),
        m_externalAcquireKeyLocks(std::move(externalAcquireKeyLocks))
    {}

    SyncStorageWrapper(const SyncStorageWrapper&) = delete;
    SyncStorageWrapper(SyncStorageWrapper&&) = delete;
    SyncStorageWrapper& operator=(const SyncStorageWrapper&) = delete;
    SyncStorageWrapper& operator=(SyncStorageWrapper&&) = delete;


    std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key) override
    {
        acquireKeyLock(_key);

        return StorageWrapper::getRow(table, _key);
    }

    std::vector<std::optional<storage::Entry>> getRows(
        const std::string_view& table, const std::variant<const gsl::span<std::string_view const>,
                                           const gsl::span<std::string const>>& _keys) override
    {
        std::visit(
            [this](auto&& keys) {
                for (auto& it : keys)
                {
                    acquireKeyLock(it);
                }
            },
            _keys);

        return StorageWrapper::getRows(table, _keys);
    }

    void setRow(
        const std::string_view& table, const std::string_view& key, storage::Entry entry) override
    {
        acquireKeyLock(key);

        StorageWrapper::setRow(table, key, std::move(entry));
    }

    void importExistsKeyLocks(gsl::span<std::string> keyLocks)
    {
        m_existsKeyLocks.clear();

        for (auto& it : keyLocks)
        {
            m_existsKeyLocks.emplace(std::move(it));
        }
    }

    std::vector<std::string> exportKeyLocks()
    {
        std::vector<std::string> keyLocks;
        keyLocks.reserve(m_myKeyLocks.size());
        for (auto& it : m_myKeyLocks)
        {
            keyLocks.emplace_back(std::move(it));
        }

        m_myKeyLocks.clear();

        return keyLocks;
    }

private:
    void acquireKeyLock(const std::string_view& key)
    {
        /*
        if (!key.compare(ACCOUNT_CODE))
        {
            // ignore static system key
            return;
        }
*/
        if (m_existsKeyLocks.find(key) != m_existsKeyLocks.end())
        {
            m_externalAcquireKeyLocks(std::string(key));
        }

        auto it = m_myKeyLocks.lower_bound(key);
        if (it == m_myKeyLocks.end() || *it != key)
        {
            m_myKeyLocks.emplace_hint(it, key);
        }
    }

    std::function<void(std::string)> m_externalAcquireKeyLocks;

    std::set<std::string, std::less<>> m_existsKeyLocks;
    std::set<std::string, std::less<>> m_myKeyLocks;
};
}  // namespace bcos::executor