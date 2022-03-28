#pragma once

#include "StorageInterface.h"

namespace bcos::storage
{
class KVStorageHelper
{
public:
    KVStorageHelper(StorageInterface::Ptr storage) : m_storage(std::move(storage)) {}
    ~KVStorageHelper() {}

    void asyncGet(std::string_view _columnFamily, std::string_view _key,
        std::function<void(Error::UniquePtr, std::string_view value)> _callback)
    {
        m_storage->asyncGetRow(_columnFamily, _key,
            [callback = std::move(_callback)](
                Error::UniquePtr&& error, std::optional<Entry>&& entry) {
                if (error)
                {
                    callback(std::move(error), std::string_view());
                    return;
                }

                if (entry)
                {
                    callback(nullptr, entry->getField(0));
                }
                else
                {
                    callback(nullptr, "");
                }
            });
    };

    void asyncGetBatch(std::string_view _columnFamily,
        const std::shared_ptr<std::vector<std::string>>& _keys,
        std::function<void(Error::UniquePtr, std::shared_ptr<std::vector<std::string>>)> callback)
    {
        m_storage->asyncGetRows(_columnFamily, *_keys,
            [callback = std::move(callback)](
                Error::UniquePtr&& error, std::vector<std::optional<Entry>>&& entries) {
                if (error)
                {
                    callback(std::move(error), nullptr);
                    return;
                }

                auto values = std::make_shared<std::vector<std::string>>();
                for (auto& it : entries)
                {
                    if (it)
                    {
                        values->emplace_back(std::string(it->getField(0)));
                    }
                    else
                    {
                        values->emplace_back("");
                    }
                }

                callback(std::move(error), std::move(values));
            });
    }

    template <typename T>
    void asyncPut(std::string_view _columnFamily, std::string_view _key, T _value,
        std::function<void(Error::UniquePtr&&)> _callback)
    {
        Entry value;
        value.importFields({std::move(_value)});

        m_storage->asyncSetRow(_columnFamily, _key, std::move(value), std::move(_callback));
    }

    void asyncRemove(std::string_view _columnFamily, std::string_view _key,
        std::function<void(const Error::Ptr&)> _callback)
    {
        Entry value;
        value.setStatus(Entry::DELETED);

        m_storage->asyncSetRow(_columnFamily, _key, std::move(value), std::move(_callback));
    }
    StorageInterface::Ptr storage() { return m_storage; }

private:
    StorageInterface::Ptr m_storage;
};

}  // namespace bcos::storage