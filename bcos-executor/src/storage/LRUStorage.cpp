#include "bcos-executor/LRUStorage.h"
#include "../Common.h"
#include "libstorage/StateStorage.h"
#include <boost/format.hpp>
#include <boost/iterator/zip_iterator.hpp>
#include <algorithm>
#include <thread>

using namespace bcos::executor;

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
    updateMRU(EntryKeyWrapper(std::string(table), std::string(key)));
    storage::StateStorage::asyncSetRow(table, key, std::move(entry), std::move(callback));
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

void LRUStorage::start()
{
    EXECUTOR_LOG(TRACE) << "Starting lru cleaner thread";
    m_running = true;
    m_worker = std::make_unique<std::thread>([self = shared_from_this()]() { self->startLoop(); });
}

void LRUStorage::stop()
{
    if (m_running)
    {
        EXECUTOR_LOG(TRACE) << "Stoping thread";
        m_running = false;

        m_mruQueue.emplace(EntryKeyWrapper());
        m_worker->join();
    }
}

void LRUStorage::startLoop()
{
    while (true)
    {
        EntryKeyWrapper entryKey;
        if (m_mruQueue.try_pop(entryKey))
        {
            // Check if stopped
            if (entryKey.isStop())
            {
                break;
            }

            // Push item to mru
            auto result = m_mru.emplace_back(std::move(entryKey));
            if (!result.second)
            {
                m_mru.relocate(m_mru.end(), result.first);
            }

            if (storage::StateStorage::capacity() > m_maxCapacity)
            {
                size_t clearedCount = 0;
                size_t clearedCapacity = 0;
                // Clear the out date items
                while ((storage::StateStorage::capacity() > ((m_maxCapacity * 2) / 3)) &&
                       !m_mru.empty())
                {
                    auto currentCapacity = storage::StateStorage::capacity();
                    auto& item = m_mru.front();

                    bcos::storage::Entry entry;
                    entry.setStatus(bcos::storage::Entry::PURGED);

                    auto [tableViw, keyView] = item.tableKeyView();
                    storage::StateStorage::asyncSetRow(
                        tableViw, keyView, std::move(entry), [](Error::UniquePtr) {});

                    ++clearedCount;
                    clearedCapacity += (currentCapacity - storage::StateStorage::capacity());

                    m_mru.pop_front();
                }

                STORAGE_LOG(DEBUG) << boost::format("LRUStorage clear %lu keys, %lu bytes") %
                                          clearedCount % clearedCapacity;
            }
        }
        else
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(200ms);  // TODO: 200ms is enough?
        }
    }
}

void LRUStorage::updateMRU(EntryKeyWrapper entryKey)
{
    m_mruQueue.push(std::move(entryKey));
}