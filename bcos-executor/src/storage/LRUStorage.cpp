#include "LRUStorage.h"
#include "../Common.h"
#include "libstorage/StateStorage.h"
#include <omp.h>
#include <oneapi/tbb/spin_mutex.h>
#include <boost/format.hpp>
#include <boost/iterator/zip_iterator.hpp>
#include <algorithm>
#include <thread>

using namespace bcos::executor;

LRUStorage::LRUStorage(std::shared_ptr<StorageInterface> prev)
  : storage::StateStorage(std::move(prev)), m_mruBuckets(std::thread::hardware_concurrency())
{}

void LRUStorage::asyncGetPrimaryKeys(std::string_view table,
    const std::optional<bcos::storage::Condition const>& _condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback)
{
    storage::StateStorage::asyncGetPrimaryKeys(table, _condition, std::move(_callback));
}

void LRUStorage::asyncGetRow(std::string_view table, std::string_view _key,
    std::function<void(Error::UniquePtr, std::optional<bcos::storage::Entry>)> _callback)
{
    storage::StateStorage::asyncGetRow(table, _key,
        [this, callback = std::move(_callback), table = std::string(table),
            key = std::string(_key)](
            Error::UniquePtr error, std::optional<bcos::storage::Entry> entry) {
            if (!error && entry)
            {
                updateMRU(EntryKeyWrapper(std::move(table), std::move(key)));
            }
            callback(std::move(error), std::move(entry));
        });
}

void LRUStorage::asyncGetRows(std::string_view table,
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<bcos::storage::Entry>>)>
        _callback)
{
    auto keys = std::make_shared<std::vector<std::string>>();

    std::visit(
        [&keys](auto&& input) {
            keys->reserve(input.size());
            for (auto& it : input)
            {
                keys->emplace_back(it);
            }
        },
        _keys);

    storage::StateStorage::asyncGetRows(table, *keys,
        [this, table = std::string(table), keys, callback = std::move(_callback)](
            Error::UniquePtr error, std::vector<std::optional<bcos::storage::Entry>> entries) {
            if (!error && keys->size() == entries.size())
            {
                for (size_t i = 0; i < keys->size(); ++i)
                {
                    auto& key = keys->at(i);
                    auto& entry = entries[i];

                    if (entry)
                    {
                        updateMRU(EntryKeyWrapper(std::move(table), std::move(key)));
                    }
                }
            }

            callback(std::move(error), std::move(entries));
        });
}

void LRUStorage::asyncSetRow(std::string_view table, std::string_view key,
    bcos::storage::Entry entry, std::function<void(Error::UniquePtr)> callback)
{
    storage::StateStorage::asyncSetRow(table, key, std::move(entry), std::move(callback));
    updateMRU(EntryKeyWrapper(std::string(table), std::string(key)));
}

void LRUStorage::merge(bool onlyDirty, const TraverseStorageInterface& source)
{
    if (&source == this)
    {
        EXECUTOR_LOG(ERROR) << "Can't merge from self!";
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Can't merge from self!"));
    }

    std::atomic_size_t count = 0;
    source.parallelTraverse(
        onlyDirty, [this, &count](const std::string_view& table, const std::string_view& key,
                       const storage::Entry& entry) {
            asyncSetRow(table, key, entry, [](Error::UniquePtr) {});
            ++count;
            return true;
        });

    EXECUTOR_LOG(INFO) << "Successfull merged " << count << " records";
}


void LRUStorage::updateMRU(EntryKeyWrapper entryKey)
{
    auto threadID = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto index = threadID % m_mruBuckets.size();

    auto& bucket = m_mruBuckets[index];
    std::unique_lock<std::mutex> lock(bucket.mutex);

    // Push item to mru
    auto result = bucket.mru.emplace_back(std::move(entryKey));
    if (!result.second)
    {
        bucket.mru.relocate(bucket.mru.end(), result.first);
    }

    if (storage::StateStorage::capacity() > m_maxCapacity)
    {
        STORAGE_LOG(INFO) << "Current capacity: " << storage::StateStorage::capacity()
                          << " greater than " << m_maxCapacity
                          << ", start clear size: " << storage::StateStorage::size();
        checkAndClear(bucket);
    }
}

void LRUStorage::checkAndClear(MRUBucket& bucket)
{
    while (storage::StateStorage::capacity() > m_maxCapacity && !bucket.mru.empty())
    {
        auto& item = bucket.mru.front();

        bcos::storage::Entry entry;
        entry.setStatus(bcos::storage::Entry::PURGED);

        auto [tableViw, keyView] = item.view();
        storage::StateStorage::asyncSetRow(
            tableViw, keyView, std::move(entry), [](Error::UniquePtr) {});

        bucket.mru.pop_front();
    }

    STORAGE_LOG(INFO) << "LRUStorage cleared, current size: " << storage::StateStorage::size();
}