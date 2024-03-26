#pragma once

#include "SyncStorageWrapper.h"
#include "bcos-executor/src/Common.h"
#include "bcos-table/src/StorageWrapper.h"

namespace bcos::executor
{
using KeyLockResponse = std::tuple<Error::UniquePtr>;
using AcquireKeyLockResponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;

class ShardingKeyLocks
{
public:
    using Ptr = std::shared_ptr<ShardingKeyLocks>;
    ShardingKeyLocks() = default;
    virtual ~ShardingKeyLocks() = default;

    static std::string keyName(const std::string_view& address, const std::string_view& key)
    {
        return std::string(address) + "-" + std::string(key);
    }


    // Notice: this function is just for testing, because it assume passing the right address format
    static std::pair<std::string_view, std::string_view> toAddressAndKey(
        const std::string_view& keyName)
    {
        std::string_view str = keyName;
        // if keyName start from 0x, remove it
        if (keyName.size() > 2 && keyName[0] == '0' && keyName[1] == 'x')
        {
            str = keyName.substr(2);
        }

        // address is 40 characters, separate it
        return std::make_pair(str.substr(0, 40), str.substr(41));
    }


    bool existsKeyLock(const std::string_view& address, const std::string_view& key)
    {
        return m_existsKeyLocks.find(keyName(address, key)) != m_existsKeyLocks.end();
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

    void importMyKeyLock(const std::string_view& address, const std::string_view& key)
    {
        auto keyName = ShardingKeyLocks::keyName(address, key);
        auto it = m_myKeyLocks.lower_bound(keyName);
        if (it == m_myKeyLocks.end() || *it != keyName)
        {
            m_myKeyLocks.emplace_hint(it, keyName);
        }
    }

private:
    std::set<std::string, std::less<>> m_existsKeyLocks;
    std::set<std::string, std::less<>> m_myKeyLocks;
};

class ShardingSyncStorageWrapper : public SyncStorageWrapper
{
public:
    using Ptr = std::shared_ptr<ShardingSyncStorageWrapper>;

    ShardingSyncStorageWrapper(ShardingKeyLocks::Ptr shardingKeyLocks,
        storage::StateStorageInterface::Ptr storage,
        std::function<void(std::string)> externalAcquireKeyLocks,
        bcos::storage::Recoder::Ptr recoder)
      : SyncStorageWrapper(storage, externalAcquireKeyLocks, recoder),
        m_externalAcquireKeyLocks(std::move(externalAcquireKeyLocks)),
        m_shardingKeyLocks(shardingKeyLocks)
    {}

    ShardingSyncStorageWrapper(const ShardingSyncStorageWrapper&) = delete;
    ShardingSyncStorageWrapper(ShardingSyncStorageWrapper&&) = delete;
    ShardingSyncStorageWrapper& operator=(const ShardingSyncStorageWrapper&) = delete;
    ShardingSyncStorageWrapper& operator=(ShardingSyncStorageWrapper&&) = delete;


    std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key) override
    {
        acquireKeyLock(_key);

        return StorageWrapper::getRow(table, _key);
    }

    std::vector<std::optional<storage::Entry>> getRows(const std::string_view& table,
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

    void setRow(
        const std::string_view& table, const std::string_view& key, storage::Entry entry) override
    {
        acquireKeyLock(key);

        StorageWrapper::setRow(table, key, std::move(entry));
    }

    void importExistsKeyLocks(gsl::span<std::string> keyLocks) override
    {
        m_shardingKeyLocks->importExistsKeyLocks(std::move(keyLocks));
    }

    std::vector<std::string> exportKeyLocks() override
    {
        return m_shardingKeyLocks->exportKeyLocks();
    }

    ShardingKeyLocks::Ptr getKeyLocks() { return m_shardingKeyLocks; }

private:
    void acquireKeyLock(const std::string_view& key)
    {
        // ignore code, shard, codeHash
        if (!key.compare(ACCOUNT_CODE) || !key.compare(ACCOUNT_SHARD) ||
            !key.compare(ACCOUNT_CODE_HASH))
        {
            // ignore static system key
            return;
        }

        if (m_shardingKeyLocks->existsKeyLock(m_contractAddress, key))
        {
            m_externalAcquireKeyLocks(ShardingKeyLocks::keyName(m_contractAddress, key));
        }

        m_shardingKeyLocks->importMyKeyLock(m_contractAddress, key);
    }

    std::function<void(std::string)> m_externalAcquireKeyLocks;

    ShardingKeyLocks::Ptr m_shardingKeyLocks;
    std::string m_contractAddress;
};
}  // namespace bcos::executor