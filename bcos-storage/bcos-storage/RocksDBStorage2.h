#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Error.h"
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/snapshot.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <range/v3/view/chunk.hpp>
#include <type_traits>
#include <variant>

namespace bcos::storage2::rocksdb
{

template <class ResolverType, class Item>
concept Resolver = requires(ResolverType&& resolver) {
    { resolver.encode(std::declval<Item>()) };
    { resolver.decode(std::string_view{}) } -> std::convertible_to<Item>;
};

// clang-format off
struct RocksDBException : public bcos::Error {};
struct UnsupportedMethod : public bcos::Error {};
struct UnexpectedItemType : public bcos::Error {};
// clang-format on

// Copy from rocksdb util/coding.h
inline constexpr int varintLength(uint64_t v)
{
    int len = 1;
    while (v >= 128)
    {
        v >>= 7;
        len++;
    }
    return len;
}

constexpr static size_t ROCKSDB_SEP_HEADER_SIZE = 12;
constexpr inline size_t getRocksDBKeyPairSize(
    bool hashColumnFamily, size_t keySize, size_t valueSize)
{
    /*
    default cf:
    |KTypeValue|
    |key_size|key_bytes|
    |value_length|value_bytes|
    */
    return hashColumnFamily ?
               1 + 4 + varintLength(keySize) + keySize + varintLength(valueSize) + valueSize :
               1 + varintLength(keySize) + keySize + varintLength(valueSize) + valueSize;
}

constexpr static auto ROCKSDB_WRITE_CHUNK_SIZE = 64;

template <class KeyType, class ValueType, Resolver<KeyType> KeyResolver,
    Resolver<ValueType> ValueResolver>
class RocksDBStorage2
{
private:
    ::rocksdb::DB& m_rocksDB;
    [[no_unique_address]] KeyResolver m_keyResolver;
    [[no_unique_address]] ValueResolver m_valueResolver;

public:
    RocksDBStorage2(::rocksdb::DB& rocksDB) : m_rocksDB(rocksDB) {}
    RocksDBStorage2(::rocksdb::DB& rocksDB, KeyResolver keyResolver, ValueResolver valueResolver)
      : m_rocksDB(rocksDB),
        m_keyResolver(std::move(keyResolver)),
        m_valueResolver(std::move(valueResolver))
    {}
    using Key = KeyType;
    using Value = ValueType;

    auto readSome(::ranges::input_range auto keys)
    {
        auto encodedKeys = keys | ::ranges::views::transform([&](auto&& key) {
            return m_keyResolver.encode(std::forward<decltype(key)>(key));
        }) | ::ranges::to<std::vector>();

        std::vector<::rocksdb::PinnableSlice> results(::ranges::size(encodedKeys));
        std::vector<::rocksdb::Status> status(::ranges::size(encodedKeys));

        auto rocksDBKeys = encodedKeys | ::ranges::views::transform([](const auto& encodedKey) {
            return ::rocksdb::Slice(::ranges::data(encodedKey), ::ranges::size(encodedKey));
        }) | ::ranges::to<std::vector>();
        m_rocksDB.MultiGet(::rocksdb::ReadOptions(), m_rocksDB.DefaultColumnFamily(),
            rocksDBKeys.size(), rocksDBKeys.data(), results.data(), status.data());

        auto values =
            ::ranges::views::zip(results, status) |
            ::ranges::views::transform([&](auto tuple) -> StorageValueType<ValueType> {
                auto& [result, status] = tuple;
                if (!status.ok())
                {
                    if (!status.IsNotFound())
                    {
                        BOOST_THROW_EXCEPTION(
                            RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
                    }
                    return {};
                }
                return {m_valueResolver.decode(result.ToStringView())};
            }) |
            ::ranges::to<std::vector>();
        return values;
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, RocksDBStorage2& storage,
        ::ranges::input_range auto keys)
        -> task::AwaitableValue<std::vector<std::optional<ValueType>>>
    {
        auto values = storage.readSome(std::move(keys));
        return {::ranges::views::transform(values, [](auto&& value) -> std::optional<ValueType> {
            if (auto entry = std::get_if<ValueType>(std::addressof(value)))
            {
                return std::make_optional(std::move(*entry));
            }
            return {};
        }) | ::ranges::to<std::vector>()};
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, RocksDBStorage2& storage,
        auto key) -> task::AwaitableValue<std::optional<ValueType>>
    {
        task::AwaitableValue<std::optional<ValueType>> result;

        auto rocksDBKey = storage.m_keyResolver.encode(key);
        std::string value;
        auto status =
            storage.m_rocksDB.Get(::rocksdb::ReadOptions(), storage.m_rocksDB.DefaultColumnFamily(),
                ::rocksdb::Slice(::ranges::data(rocksDBKey), ::ranges::size(rocksDBKey)),
                std::addressof(value));
        if (!status.ok())
        {
            if (!status.IsNotFound())
            {
                BOOST_THROW_EXCEPTION(
                    RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
            }
            return result;
        }
        result.value().emplace(storage.m_valueResolver.decode(std::move(value)));

        return result;
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        RocksDBStorage2& storage, ::ranges::input_range auto keyValues)
    {
        using RangeKeyType = std::tuple_element_t<0, ::ranges::range_value_t<decltype(keyValues)>>;
        using RangeValueType =
            std::tuple_element_t<1, ::ranges::range_value_t<decltype(keyValues)>>;
        using RocksDBKeyValueTuple =
            std::tuple<decltype(storage.m_keyResolver.encode(std::declval<RangeKeyType>())),
                decltype(storage.m_valueResolver.encode(std::declval<RangeValueType>()))>;

        std::atomic_size_t totalReservedLength = ROCKSDB_SEP_HEADER_SIZE;
        tbb::concurrent_vector<RocksDBKeyValueTuple> rocksDBKeyValues;
        if constexpr (::ranges::sized_range<decltype(keyValues)>)
        {
            rocksDBKeyValues.reserve(::ranges::size(keyValues));
        }
        auto chunkRange = ::ranges::views::chunk(keyValues, ROCKSDB_WRITE_CHUNK_SIZE);
        tbb::task_group writeGroup;
        for (auto&& subrange : chunkRange)
        {
            writeGroup.run([subrange = std::forward<decltype(subrange)>(subrange),
                               &rocksDBKeyValues, &storage, &totalReservedLength]() {
                size_t localReservedLength = 0;
                for (auto&& [key, value] : subrange)
                {
                    auto it = rocksDBKeyValues.emplace_back(
                        storage.m_keyResolver.encode(std::forward<decltype(key)>(key)),
                        storage.m_valueResolver.encode(std::forward<decltype(value)>(value)));
                    auto const& [keyBuffer, valueBuffer] = *it;
                    localReservedLength += getRocksDBKeyPairSize(
                        false, ::ranges::size(keyBuffer), ::ranges::size(valueBuffer));
                }
                totalReservedLength += localReservedLength;
            });
        }
        writeGroup.wait();

        ::rocksdb::WriteBatch writeBatch(totalReservedLength);
        for (auto&& [keyBuffer, valueBuffer] : rocksDBKeyValues)
        {
            writeBatch.Put(::rocksdb::Slice(::ranges::data(keyBuffer), ::ranges::size(keyBuffer)),
                ::rocksdb::Slice(::ranges::data(valueBuffer), ::ranges::size(valueBuffer)));
        }
        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, std::addressof(writeBatch));

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << errinfo_comment(status.ToString()));
        }
        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
        RocksDBStorage2& storage, auto key, auto value)
    {
        auto rocksDBKey = storage.m_keyResolver.encode(key);
        auto rocksDBValue = storage.m_valueResolver.encode(value);

        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Put(options,
            ::rocksdb::Slice(::ranges::data(rocksDBKey), ::ranges::size(rocksDBKey)),
            ::rocksdb::Slice(::ranges::data(rocksDBValue), ::ranges::size(rocksDBValue)));

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << errinfo_comment(status.ToString()));
        }
        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        RocksDBStorage2& storage, ::ranges::input_range auto keys)
    {
        ::rocksdb::WriteBatch writeBatch;

        for (auto const& key : keys)
        {
            auto encodedKey = storage.m_keyResolver.encode(key);
            writeBatch.Delete(
                ::rocksdb::Slice(::ranges::data(encodedKey), ::ranges::size(encodedKey)));
        }

        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << errinfo_comment(status.ToString()));
        }

        return {};
    }

    friend task::Task<void> tag_invoke(
        storage2::tag_t<merge> /*unused*/, RocksDBStorage2& storage, auto& fromStorage)
    {
        auto range = co_await storage2::range(fromStorage);

        using MergeRangeValueType = typename task::AwaitableReturnType<
            std::invoke_result_t<decltype(&decltype(range)::next), decltype(range)>>::value_type;
        using RocksDBKeyValueTuple =
            std::tuple<decltype(storage.m_keyResolver.encode(std::declval<
                           std::remove_pointer_t<std::tuple_element_t<0, MergeRangeValueType>>>())),
                std::optional<decltype(storage.m_valueResolver.encode(std::get<Value>(
                    std::declval<std::tuple_element_t<1, MergeRangeValueType>>())))>>;

        std::atomic_size_t totalReservedLength = ROCKSDB_SEP_HEADER_SIZE;
        tbb::concurrent_vector<RocksDBKeyValueTuple> rocksDBKeyValues;
        tbb::task_group mergeGroup;
        while (auto keyValue = co_await range.next())
        {
            mergeGroup.run([&rocksDBKeyValues, &storage, &totalReservedLength,
                               keyValue = std::move(keyValue)]() {
                auto&& [key, value] = *keyValue;
                size_t localReservedLength = 0;
                if (!std::holds_alternative<DELETED_TYPE>(value))
                {
                    auto it = rocksDBKeyValues.emplace_back(storage.m_keyResolver.encode(key),
                        std::make_optional(storage.m_valueResolver.encode(std::get<Value>(value))));
                    auto&& [keyBuffer, valueBuffer] = *it;
                    localReservedLength += getRocksDBKeyPairSize(
                        false, ::ranges::size(keyBuffer), ::ranges::size(*valueBuffer));
                }
                else
                {
                    auto it = rocksDBKeyValues.emplace_back(storage.m_keyResolver.encode(key),
                        std::tuple_element_t<1, RocksDBKeyValueTuple>{});
                    auto&& [keyBuffer, valueBuffer] = *it;
                    localReservedLength +=
                        getRocksDBKeyPairSize(false, ::ranges::size(keyBuffer), 0);
                }
                totalReservedLength += localReservedLength;
            });
        }
        mergeGroup.wait();

        ::rocksdb::WriteBatch writeBatch(totalReservedLength);
        for (auto&& [keyBuffer, valueBuffer] : rocksDBKeyValues)
        {
            if (valueBuffer)
            {
                writeBatch.Put(
                    ::rocksdb::Slice(::ranges::data(keyBuffer), ::ranges::size(keyBuffer)),
                    ::rocksdb::Slice(::ranges::data(*valueBuffer), ::ranges::size(*valueBuffer)));
            }
            else
            {
                writeBatch.Delete(
                    ::rocksdb::Slice(::ranges::data(keyBuffer), ::ranges::size(keyBuffer)));
            }
        }
        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, std::addressof(writeBatch));

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << errinfo_comment(status.ToString()));
        }
    }

    class Iterator
    {
    private:
        const ::rocksdb::Snapshot* m_snapshot;
        std::unique_ptr<::rocksdb::Iterator> m_iterator;
        const RocksDBStorage2* m_storage;

    public:
        Iterator(const Iterator&) = delete;
        Iterator(Iterator&& iterator) noexcept
          : m_snapshot(iterator.m_snapshot),
            m_iterator(std::move(iterator.m_iterator)),
            m_storage(iterator.m_storage)
        {
            iterator.m_snapshot = nullptr;
            iterator.m_storage = nullptr;
        }
        Iterator& operator=(const Iterator&) = delete;
        Iterator& operator=(Iterator&& iterator) noexcept
        {
            m_snapshot = iterator.m_snapshot;
            m_iterator = std::move(iterator.m_iterator);
            m_storage = iterator.m_storage;
            iterator.m_snapshot = nullptr;
            iterator.m_storage = nullptr;
            return *this;
        }
        Iterator(const ::rocksdb::Snapshot* snapshot, ::rocksdb::Iterator* iterator,
            const RocksDBStorage2* storage)
          : m_snapshot(snapshot), m_iterator(iterator), m_storage(storage)
        {}
        ~Iterator() noexcept
        {
            if (m_snapshot != nullptr && m_storage != nullptr)
            {
                m_storage->m_rocksDB.ReleaseSnapshot(m_snapshot);
            }
        }

        auto next()
        {
            task::AwaitableValue<std::optional<std::tuple<KeyType, StorageValueType<ValueType>>>>
                result;
            if (m_iterator->Valid())
            {
                auto key = m_storage->m_keyResolver.decode(m_iterator->key().ToStringView());
                auto value = m_storage->m_valueResolver.decode(m_iterator->value().ToStringView());
                m_iterator->Next();
                result.value().emplace(std::move(key), StorageValueType<Value>{std::move(value)});
            }
            return result;
        }
    };

    static task::AwaitableValue<Iterator> range(
        RocksDBStorage2& storage, const ::rocksdb::Slice* startSlice = nullptr)
    {
        const auto* snapshot = storage.m_rocksDB.GetSnapshot();
        ::rocksdb::ReadOptions readOptions;
        readOptions.snapshot = snapshot;
        auto* iterator = storage.m_rocksDB.NewIterator(readOptions);
        if (startSlice != nullptr)
        {
            iterator->Seek(*startSlice);
        }
        else
        {
            iterator->SeekToFirst();
        }
        return Iterator{snapshot, iterator, std::addressof(storage)};
    }

    friend task::AwaitableValue<Iterator> tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, RocksDBStorage2& storage)
    {
        return range(storage);
    }

    friend task::AwaitableValue<Iterator> tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, RocksDBStorage2& storage,
        storage2::RANGE_SEEK_TYPE /*unused*/, auto&& startKey)
    {
        auto encodedKey = storage.m_keyResolver.encode(startKey);
        ::rocksdb::Slice slice(::ranges::data(encodedKey), ::ranges::size(encodedKey));
        return range(storage, std::addressof(slice));
    }
};

}  // namespace bcos::storage2::rocksdb