#pragma once

#include "../interfaces/storage/StorageInterface.h"

namespace bcos::storage
{
inline void asyncBatchSetRows(const StorageInterface::Ptr& storage, const std::string_view& table,
    gsl::span<std::tuple<std::string_view, Entry>> entries,
    std::function<void(Error::UniquePtr&&, std::vector<bool>&&)> callback)
{
    struct ResultCollection
    {
        std::atomic_size_t success = 0;
        std::atomic_size_t failed = 0;

        std::vector<bool> results;
    };

    auto collection = std::make_shared<ResultCollection>();
    collection->results.resize(entries.size());

    auto callbackPtr =
        std::make_shared<std::function<void(Error::UniquePtr&&, std::vector<bool> &&)>>(
            std::move(callback));

    size_t index = 0;
    for (auto& it : entries)
    {
        storage->asyncSetRow(table, std::get<0>(it), std::get<1>(it),
            [collection, index, callbackPtr](Error::UniquePtr&& error) {
                if (error)
                {
                    collection->results[index] = false;
                    ++collection->failed;
                }
                else
                {
                    collection->results[index] = true;
                    ++collection->success;
                }

                if (collection->success + collection->failed == collection->results.size())
                {
                    (*callbackPtr)(nullptr, std::move(collection->results));
                }
            });

        ++index;
    }
}
}  // namespace bcos::storage