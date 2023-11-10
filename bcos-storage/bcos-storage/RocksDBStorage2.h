#pragma once
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Exception.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/storage2/StorageMethods.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Overloaded.h"
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <type_traits>
#include <variant>

namespace bcos::storage2::rocksdb
{

template <class ResolverType, class Item>
concept Resolver = requires(ResolverType&& resolver) {
                       // clang-format off
    resolver.encode(std::declval<Item>());
    { resolver.decode(std::string_view{})} -> std::convertible_to<Item>;
                       // clang-format on
                   };

template <class AnyResolver, class Key>
struct ResolverEncodeReturnTrait
{
    using type = decltype(std::declval<AnyResolver>().encode(std::declval<Key>()));
};
template <class AnyResolver, class Key>
using ResolverEncodeReturnType = typename ResolverEncodeReturnTrait<AnyResolver, Key>::type;

// clang-format off
struct RocksDBException : public bcos::Error {};
struct UnsupportedMethod : public bcos::Error {};
struct UnexpectedItemType : public bcos::Error {};
// clang-format on

constexpr static size_t ROCKSDB_SEP_HEADER_SIZE = 12;
constexpr inline size_t getRocksDBKeyPairSize(
    bool hashColumnFamily, size_t keySize, size_t valueSize)
{
    /*
    default cf:
    |KTypeValue|
    |key_size|key_bytes|
    |value_length|value_bytes|

    specify cf:
    |kTypeColumnFamilyValue|column_family_id|
    |key_size|key_bytes|
    |value_length|value_bytes|
    */
    return hashColumnFamily ? 1 + 4 + keySize + 4 + valueSize : 1 + keySize + 4 + valueSize;
}

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
    RocksDBStorage2(
        ::rocksdb::DB& rocksDB, KeyResolver&& keyResolver, ValueResolver&& valueResolver)
      : m_rocksDB(rocksDB),
        m_keyResolver(std::forward<KeyResolver>(keyResolver)),
        m_valueResolver(std::forward<ValueResolver>(valueResolver))
    {}
    using Key = KeyType;
    using Value = ValueType;

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, RocksDBStorage2& storage,
        auto const& key) -> task::AwaitableValue<std::optional<ValueType>>
    {
        auto rocksDBKey = storage.m_keyResolver.encode(key);
        ::rocksdb::PinnableSlice result;
        auto status =
            storage.m_rocksDB.Get(::rocksdb::ReadOptions(), storage.m_rocksDB.DefaultColumnFamily(),
                ::rocksdb::Slice(RANGES::data(rocksDBKey), RANGES::size(rocksDBKey)), &result);
        if (!status.ok())
        {
            if (!status.IsNotFound())
            {
                BOOST_THROW_EXCEPTION(
                    RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
            }
            return {};
        }

        return {std::make_optional(storage.m_valueResolver.decode(result.ToStringView()))};
    }

    static auto readSome(RocksDBStorage2& storage, RANGES::input_range auto&& keys)
    {
        std::vector<::rocksdb::PinnableSlice> results;
        std::vector<::rocksdb::Status> status;
        auto encodedKeys =
            keys | RANGES::views::transform([&](const auto& key) {
                return storage.m_keyResolver.encode(key);
            }) |
            RANGES::to<std::vector<
                ResolverEncodeReturnType<KeyResolver, RANGES::range_value_t<decltype(keys)>>>>();
        auto rocksDBKeys = encodedKeys | RANGES::views::transform([](const auto& encodedKey) {
            return ::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey));
        }) | RANGES::to<std::vector<::rocksdb::Slice>>();

        results.resize(RANGES::size(rocksDBKeys));
        status.resize(RANGES::size(rocksDBKeys));

        storage.m_rocksDB.MultiGet(::rocksdb::ReadOptions(),
            storage.m_rocksDB.DefaultColumnFamily(), rocksDBKeys.size(), rocksDBKeys.data(),
            results.data(), status.data());

        return RANGES::views::zip(results, status) |
               RANGES::views::transform([m_storage = &storage, m_results = std::move(results),
                                            m_status = std::move(status)](
                                            auto&& tuple) -> std::optional<ValueType> {
                   auto&& [result, status] = tuple;
                   if (!status.ok())
                   {
                       if (!status.IsNotFound())
                       {
                           BOOST_THROW_EXCEPTION(
                               RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
                       }
                       return {};
                   }

                   return std::make_optional(
                       m_storage.m_valueResolver.decode(result.ToStringView()));
               });
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, RocksDBStorage2& storage,
        RANGES::input_range auto&& keys)
        -> task::AwaitableValue<decltype(readSome(storage, std::forward<decltype(keys)>(keys)))>
    {
        return {readSome(storage, std::forward<decltype(keys)>(keys))};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        RocksDBStorage2& storage, RANGES::input_range auto&& keys,
        RANGES::input_range auto&& values)
    {
        constexpr static auto ROCKSDB_WRITE_CHUNK_SIZE = 32;
        using RocksDBKeyValueTuple =
            std::tuple<decltype(storage.m_keyResolver.encode(
                           std::declval<RANGES::range_value_t<decltype(keys)>>())),
                decltype(storage.m_valueResolver.encode(
                    std::declval<RANGES::range_value_t<decltype(values)>>()))>;

        std::atomic_size_t totalReservedLength = ROCKSDB_SEP_HEADER_SIZE;
        tbb::concurrent_vector<RocksDBKeyValueTuple> rocksDBKeyValues;
        auto chunkRange =
            RANGES::views::zip(keys, values) | RANGES::views::chunk(ROCKSDB_WRITE_CHUNK_SIZE);
        tbb::parallel_for_each(chunkRange, [&](auto&& subrange) {
            size_t localReservedLength = 0;
            for (auto&& [key, value] : subrange)
            {
                auto it = rocksDBKeyValues.emplace_back(
                    storage.m_keyResolver.encode(key), storage.m_valueResolver.encode(value));
                auto&& [keyBuffer, valueBuffer] = *it;
                localReservedLength += getRocksDBKeyPairSize(false, keyBuffer, valueBuffer);
            }
            totalReservedLength += localReservedLength;
        });

        ::rocksdb::WriteBatch writeBatch(totalReservedLength);
        for (auto&& [keyBuffer, valueBuffer] : rocksDBKeyValues)
        {
            writeBatch.Put(::rocksdb::Slice(RANGES::data(keyBuffer), RANGES::size(keyBuffer)),
                ::rocksdb::Slice(RANGES::data(valueBuffer), RANGES::size(valueBuffer)));
        }
        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, &writeBatch);

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        RocksDBStorage2& storage, RANGES::input_range auto const& keys)
    {
        ::rocksdb::WriteBatch writeBatch;

        for (auto const& key : keys)
        {
            auto encodedKey = storage.m_keyResolver.encode(key);
            writeBatch.Delete(::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey)));
        }

        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }

        return {};
    }


    task::Task<void> merge(storage2::SeekableStorage auto& from)
    {
        auto it = co_await from.seek(storage2::STORAGE_BEGIN);
        using IteratorKeyType = task::AwaitableReturnType<decltype(it.key())>;
        using IteratorValueType = task::AwaitableReturnType<decltype(it.value())>;

        using BatchKeyType = std::conditional_t<std::is_lvalue_reference_v<IteratorKeyType>,
            std::add_pointer_t<std::remove_reference_t<IteratorKeyType>>, IteratorKeyType>;
        using BatchValueType = std::conditional_t<std::is_lvalue_reference_v<IteratorValueType>,
            std::add_pointer_t<std::remove_reference_t<IteratorValueType>>, IteratorValueType>;

        using KeyValue = std::tuple<BatchKeyType, BatchValueType>;
        using DeleteKey = BatchKeyType;
        using KeyValueBuffer =
            std::tuple<decltype(m_keyResolver.encode(std::declval<IteratorKeyType>())),
                decltype(m_valueResolver.encode(std::declval<IteratorValueType>()))>;
        using DeleteKeyBuffer = decltype(m_keyResolver.encode(std::declval<IteratorKeyType>()));
        using Item = std::variant<KeyValue, DeleteKey>;
        using EncodedItem = std::variant<KeyValueBuffer, DeleteKeyBuffer>;

        ::rocksdb::WriteBatch writeBatch;
        tbb::parallel_pipeline(std::thread::hardware_concurrency(),
            tbb::make_filter<void, Item>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control) {
                    return task::syncWait([&]() -> task::Task<Item> {
                        if (!co_await it.next())
                        {
                            control.stop();
                            co_return Item{};
                        }

                        if (co_await it.hasValue())
                        {
                            co_return Item{KeyValue(refToPointer(co_await it.key()),
                                refToPointer(co_await it.value()))};
                        }
                        else
                        {
                            co_return Item{DeleteKey(refToPointer(co_await it.key()))};
                        }
                    }());
                }) &
                tbb::make_filter<Item, EncodedItem>(tbb::filter_mode::parallel,
                    [&](Item input) -> EncodedItem {
                        return std::visit(
                            bcos::overloaded{[&](KeyValue& keyValue) {
                                                 auto& [key, value] = keyValue;
                                                 return EncodedItem{KeyValueBuffer(
                                                     m_keyResolver.encode(pointerToRef(key)),
                                                     m_valueResolver.encode(pointerToRef(value)))};
                                             },
                                [&](DeleteKey& deleteKey) {
                                    return EncodedItem{DeleteKeyBuffer(
                                        m_keyResolver.encode(pointerToRef(deleteKey)))};
                                }},
                            input);
                    }) &
                tbb::make_filter<EncodedItem, void>(
                    tbb::filter_mode::serial_out_of_order, [&](EncodedItem input) {
                        std::visit(bcos::overloaded{
                                       [&](KeyValueBuffer& keyValueBuffer) {
                                           auto& [keyBuffer, valueBuffer] = keyValueBuffer;
                                           writeBatch.Put(::rocksdb::Slice(RANGES::data(keyBuffer),
                                                              RANGES::size(keyBuffer)),
                                               ::rocksdb::Slice(RANGES::data(valueBuffer),
                                                   RANGES::size(valueBuffer)));
                                       },
                                       [&](DeleteKeyBuffer& deleteKeyBuffer) {
                                           writeBatch.Delete(
                                               ::rocksdb::Slice(RANGES::data(deleteKeyBuffer),
                                                   RANGES::size(deleteKeyBuffer)));
                                       }},
                            input);
                    }));
        ::rocksdb::WriteOptions options;
        auto status = m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }
        co_return;
    }
};

}  // namespace bcos::storage2::rocksdb