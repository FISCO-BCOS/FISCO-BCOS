#pragma once

#include <bcos-framework/interfaces/storage/StorageInterface.h>
#include <bcos-framework/libstorage/StateStorage.h>
#include <tbb/concurrent_queue.h>
#include <tbb/task_group.h>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace bcos::executor
{
class LRUStorage : public virtual bcos::storage::StateStorage,
                   public virtual bcos::storage::MergeableStorageInterface,
                   public std::enable_shared_from_this<LRUStorage>
{
public:
    using StateStorage::StateStorage;
    ~LRUStorage() noexcept override { stop(); }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<bcos::storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<bcos::storage::Entry>)> _callback)
        override;

    void asyncGetRows(std::string_view table,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<bcos::storage::Entry>>)>
            _callback) override;

    void asyncSetRow(std::string_view table, std::string_view key, bcos::storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) override;

    void merge(bool onlyDirty, const TraverseStorageInterface& source) override;

    void start();
    void stop();

    void setMaxCapacity(size_t capacity) { m_maxCapacity = capacity; }

private:
    void startLoop();

    struct EntryKeyWrapper : public EntryKey
    {
        using EntryKey::tuple;

        std::tuple<std::string_view, std::string_view> tableKeyView() const
        {
            return std::make_tuple(
                std::string_view(std::get<0>(*this)), std::string_view(std::get<1>(*this)));
        }

        bool isStop() const { return std::get<0>(*this).empty() && std::get<1>(*this).empty(); }
    };

    void updateMRU(EntryKeyWrapper entryKey);

    boost::multi_index_container<EntryKeyWrapper,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<EntryKeyWrapper,
                std::tuple<std::string_view, std::string_view>, &EntryKeyWrapper::tableKeyView>>>>
        m_mru;
    tbb::concurrent_queue<EntryKeyWrapper> m_mruQueue;

    size_t m_maxCapacity = 32 * 1024 * 1024;  // default 32 for cache

    std::unique_ptr<std::thread> m_worker;
    std::atomic_bool m_running = false;

    std::atomic<uint64_t> m_hitTimes;
    std::atomic<uint64_t> m_queryTimes;
};
}  // namespace bcos::executor