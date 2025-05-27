#pragma once

#include "bcos-table/src/StorageWrapper.h"
#include <boost/iterator/iterator_categories.hpp>
#include <boost/throw_exception.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace bcos::executor
{
using KeyLockResponse = std::tuple<Error::UniquePtr>;
using AcquireKeyLockResponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;

class SyncStorageWrapper : public storage::StorageWrapper
{
public:
    using Ptr = std::shared_ptr<SyncStorageWrapper>;

    SyncStorageWrapper(storage::StateStorageInterface::Ptr storage,
        std::function<void(std::string)> externalAcquireKeyLocks,
        bcos::storage::Recoder::Ptr recoder)
      : StorageWrapper(std::move(storage), std::move(recoder)),
        m_externalAcquireKeyLocks(std::move(externalAcquireKeyLocks))
    {}

    SyncStorageWrapper(const SyncStorageWrapper&) = delete;
    SyncStorageWrapper(SyncStorageWrapper&&) = delete;
    SyncStorageWrapper& operator=(const SyncStorageWrapper&) = delete;
    SyncStorageWrapper& operator=(SyncStorageWrapper&&) = delete;


    virtual std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key) override
    {
        acquireKeyLock(_key);

        return StorageWrapper::getRow(table, _key);
    }

    virtual std::vector<std::optional<storage::Entry>> getRows(const std::string_view& table,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys) override
    {
        for (auto it : keys)
        {
            acquireKeyLock(it);
        }

        return StorageWrapper::getRows(table, keys);
    }

    virtual void setRow(
        const std::string_view& table, const std::string_view& key, storage::Entry entry) override
    {
        acquireKeyLock(key);

        StorageWrapper::setRow(table, key, std::move(entry));
    }

    virtual void importExistsKeyLocks(gsl::span<std::string> keyLocks)
    {
        m_existsKeyLocks.clear();

        for (auto& it : keyLocks)
        {
            m_existsKeyLocks.emplace(std::move(it));
        }
    }

    virtual std::vector<std::string> exportKeyLocks()
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

    std::function<void(std::string)> takeExternalAcquireKeyLocks()
    {
        return std::move(m_externalAcquireKeyLocks);
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