#pragma once

#include "Storage.h"
#include <memory>
#include <optional>
#include <range/v3/view/any_view.hpp>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::storage2
{

// AnyStorage：针对 Storage 的 tag_invoke 接口的类型擦除封装。
//
// 设计
// - 在保留 Key/Value 显式类型的同时，擦除具体的存储实现类型
// - 与现有 storage2 的 tag_invoke API 互操作（readOne/Some、writeOne/Some、
//   removeOne/Some、range、可选的 range-seek，以及 merge）
//
// 用法
//   MemoryStorage<int, std::string, ORDERED> ms;
//   AnyStorage<int, std::string> any(ms);
//   co_await writeOne(any, 1, std::string{"v"});
//   auto v = co_await readOne(any, 1);
//   auto it = co_await range(any);
//   while (auto kv = co_await it.next()) { /* 迭代 */ }
//   // 如果底层存储支持 RANGE_SEEK（例如有序的 MemoryStorage），下面会从指定键开始：
//   auto it2 = co_await range(any, RANGE_SEEK, 1);
//
// 说明
// - 批量 API 接受通用 range；AnyStorage 在内部使用 any_view 别名进行适配
// - merge(to, from) 会遍历来源的 range，并以通用方式对目标执行写入/删除
// - 使用 DIRECT 进行删除时，如果底层存储支持，将绕过“逻辑删除”
//
// 通过将 Key/Value 作为模板参数保留，在擦除具体存储实现的同时保持强类型。

// AnyStorage: type-erased wrapper for Storage tag_invoke interface.
//
// Intent
// - Erase the concrete storage type while keeping Key/Value types explicit
// - Interoperate with existing storage2 tag_invoke API (readOne/Some, writeOne/Some,
//   removeOne/Some, range, optional range-seek, and merge)
//
// Quick usage
//   MemoryStorage<int, std::string, ORDERED> ms;
//   AnyStorage<int, std::string> any(ms);
//   co_await writeOne(any, 1, std::string{"v"});
//   auto v = co_await readOne(any, 1);
//   auto it = co_await range(any);
//   while (auto kv = co_await it.next()) { /* iterate */ }
//   // If underlying storage supports RANGE_SEEK (e.g. ordered MemoryStorage), this will seek:
//   auto it2 = co_await range(any, RANGE_SEEK, 1);
//
// Notes
// - Bulk APIs accept generic ranges; AnyStorage adapts them internally using any_view aliases
// - merge(to, from) iterates source's range and applies writes/removes to destination generically
// - Removing with DIRECT will bypass logical-deletion if the underlying storage supports it
//
// Keep Key/Value as template parameters to preserve strong types while
// erasing the concrete storage implementation.
template <class Key, class ValueT>
class AnyStorage
{
public:
    using KeyType = Key;
    using ValueType = ValueT;
    using DataValue = StorageValueType<ValueType>;
    using AnyKeyView =
        ::ranges::any_view<Key, ::ranges::category::mask | ::ranges::category::sized>;
    using AnyKeyValueView = ::ranges::any_view<std::pair<Key, ValueType>,
        ::ranges::category::mask | ::ranges::category::sized>;

    AnyStorage() = default;
    AnyStorage(const AnyStorage&) = default;
    AnyStorage(AnyStorage&&) noexcept = default;
    AnyStorage& operator=(const AnyStorage&) = default;
    AnyStorage& operator=(AnyStorage&&) noexcept = default;
    ~AnyStorage() = default;

    // Construct from any storage S that supports the required tag_invoke ops
    template <class Storage>
        requires(!std::is_same_v<std::remove_cvref_t<Storage>, AnyStorage>)
    explicit AnyStorage(Storage& storage) : m_self(std::make_shared<StorageModel<Storage>>(storage))
    {}

private:
    struct IteratorConcept
    {
        IteratorConcept() = default;
        IteratorConcept(const IteratorConcept&) = default;
        IteratorConcept& operator=(const IteratorConcept&) = default;
        IteratorConcept(IteratorConcept&&) noexcept = default;
        IteratorConcept& operator=(IteratorConcept&&) noexcept = default;
        virtual ~IteratorConcept() = default;
        virtual task::Task<std::optional<std::pair<Key, DataValue>>> next() = 0;
    };

    struct StorageConcept
    {
        StorageConcept() = default;
        StorageConcept(const StorageConcept&) = default;
        StorageConcept& operator=(const StorageConcept&) = default;
        StorageConcept(StorageConcept&&) noexcept = default;
        StorageConcept& operator=(StorageConcept&&) noexcept = default;
        virtual ~StorageConcept() = default;
        virtual task::Task<std::vector<std::optional<ValueType>>> readSome(AnyKeyView keys) = 0;
        virtual task::Task<void> writeSome(AnyKeyValueView keyValues) = 0;
        virtual task::Task<void> removeSome(AnyKeyView keys, bool direct) = 0;

        virtual task::Task<std::optional<ValueType>> readOne(Key key) = 0;
        virtual task::Task<void> writeOne(Key key, ValueType value) = 0;
        virtual task::Task<void> removeOne(Key key, bool direct) = 0;

        virtual task::Task<std::unique_ptr<IteratorConcept>> rangeBegin() = 0;
        virtual task::Task<std::unique_ptr<IteratorConcept>> rangeSeekBegin(const Key& key) = 0;

        // Generic merge: iterate source and write to destination
        virtual task::Task<void> mergeFrom(StorageConcept& from) = 0;
    };

    template <class It>
    struct IteratorModel : IteratorConcept
    {
        explicit IteratorModel(It iter) : m_it(std::move(iter)) {}

        task::Task<std::optional<std::pair<Key, DataValue>>> next() override
        {
            auto item = co_await m_it.next();
            if (!item)
            {
                co_return {};
            }

            // Accept either pair-like or tuple-like (two elements)
            auto& [key, dataValue] = *item;
            // Convert StorageValueType<U> -> StorageValueType<Value>
            DataValue out;
            std::visit(
                [&](auto&& inner) {
                    using T = std::remove_cvref_t<decltype(inner)>;
                    if constexpr (std::is_same_v<T, NOT_EXISTS_TYPE>)
                    {
                        out.template emplace<NOT_EXISTS_TYPE>();
                    }
                    else if constexpr (std::is_same_v<T, DELETED_TYPE>)
                    {
                        out.template emplace<DELETED_TYPE>();
                    }
                    else
                    {
                        out.template emplace<ValueType>(std::forward<decltype(inner)>(inner));
                    }
                },
                dataValue);

            co_return std::make_optional(std::make_pair(std::move(key), std::move(out)));
        }

        It m_it;
    };

    template <class Storage>
    struct StorageModel : StorageConcept
    {
        explicit StorageModel(Storage& storage) : m_storage(std::addressof(storage)) {}

        task::Task<std::vector<std::optional<ValueType>>> readSome(AnyKeyView keys) override
        {
            co_return co_await storage2::readSome(*m_storage, ::ranges::views::all(keys));
        }

        task::Task<void> writeSome(AnyKeyValueView keyValues) override
        {
            co_await storage2::writeSome(*m_storage, ::ranges::views::all(keyValues));
        }

        task::Task<void> removeSome(AnyKeyView keys, bool direct) override
        {
            if (direct)
            {
                co_await storage2::removeSome(*m_storage, ::ranges::views::all(keys), DIRECT);
            }
            else
            {
                co_await storage2::removeSome(*m_storage, ::ranges::views::all(keys));
            }
        }

        task::Task<std::optional<ValueType>> readOne(Key key) override
        {
            co_return co_await storage2::readOne(*m_storage, std::move(key));
        }

        task::Task<void> writeOne(Key key, ValueType value) override
        {
            co_await storage2::writeOne(*m_storage, std::move(key), std::move(value));
        }

        task::Task<void> removeOne(Key key, bool direct) override
        {
            if (direct)
            {
                co_await storage2::removeOne(*m_storage, std::move(key), DIRECT);
            }
            else
            {
                co_await storage2::removeOne(*m_storage, std::move(key));
            }
        }

        task::Task<std::unique_ptr<IteratorConcept>> rangeBegin() override
        {
            auto it = co_await storage2::range(*m_storage);
            co_return std::unique_ptr<IteratorConcept>(
                std::make_unique<IteratorModel<decltype(it)>>(std::move(it)));
        }

        task::Task<std::unique_ptr<IteratorConcept>> rangeSeekBegin(const Key& key) override
        {
            if constexpr (requires { tag_invoke(range, *m_storage, RANGE_SEEK, key); })
            {
                auto it = co_await storage2::range(*m_storage, RANGE_SEEK, key);
                co_return std::unique_ptr<IteratorConcept>(
                    std::make_unique<IteratorModel<decltype(it)>>(std::move(it)));
            }
            else
            {
                // Fallback: no seek support, return begin()
                auto it = co_await storage2::range(*m_storage);
                co_return std::unique_ptr<IteratorConcept>(
                    std::make_unique<IteratorModel<decltype(it)>>(std::move(it)));
            }
        }

        task::Task<void> mergeFrom(StorageConcept& from) override
        {
            // Generic merge: iterate from.range() and apply to this storage
            // Try to get an iterator from 'from'
            auto fromIteratorPtr = co_await from.rangeBegin();
            while (auto item = co_await fromIteratorPtr->next())
            {
                auto&& [key, dataValue] = *item;
                if (std::holds_alternative<NOT_EXISTS_TYPE>(dataValue))
                {
                    continue;  // skip
                }
                if (std::holds_alternative<DELETED_TYPE>(dataValue))
                {
                    co_await storage2::removeOne(*m_storage, std::move(key));
                    continue;
                }
                auto& valueRef = std::get<ValueType>(dataValue);
                co_await storage2::writeOne(*m_storage, std::move(key), std::move(valueRef));
            }
        }

        Storage* m_storage;
    };

    std::shared_ptr<StorageConcept> m_self;

public:
    // Expose Value type to satisfy readOne operator() return type requirement
    using Value = ValueType;

    // Type-erased iterator facade that satisfies Iterator concept
    class Iterator
    {
    public:
        explicit Iterator(std::unique_ptr<IteratorConcept> ptr) : m_ptr(std::move(ptr)) {}

        task::Task<std::optional<std::pair<Key, DataValue>>> next()
        {
            co_return co_await m_ptr->next();
        }

    private:
        std::unique_ptr<IteratorConcept> m_ptr;
    };

    // tag_invoke set: forward to underlying Concept
    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keys) -> task::Task<std::vector<std::optional<Value>>>
    {
        co_return co_await storage.m_self->readSome(::ranges::views::all(keys));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keyValues) -> task::Task<void>
    {
        co_await storage.m_self->writeSome(::ranges::views::all(keyValues));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keys) -> task::Task<void>
    {
        co_await storage.m_self->removeSome(::ranges::views::all(keys), false);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keys, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        co_await storage.m_self->removeSome(::ranges::views::all(keys), true);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, AnyStorage& storage,
        auto key) -> task::Task<std::optional<Value>>
    {
        co_return co_await storage.m_self->readOne(Key(std::move(key)));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, AnyStorage& storage,
        auto key, auto value) -> task::Task<void>
    {
        co_await storage.m_self->writeOne(Key(std::move(key)), Value(std::move(value)));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeOne> /*unused*/, AnyStorage& storage,
        auto key) -> task::Task<void>
    {
        co_await storage.m_self->removeOne(std::move(key), false);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeOne> /*unused*/, AnyStorage& storage,
        auto key, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        co_await storage.m_self->removeOne(std::move(key), true);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::range> /*unused*/, AnyStorage& storage)
        -> task::Task<typename AnyStorage::Iterator>
    {
        auto ptr = co_await storage.m_self->rangeBegin();
        co_return Iterator(std::move(ptr));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::range> /*unused*/, AnyStorage& storage,
        RANGE_SEEK_TYPE /*unused*/, const Key& key) -> task::Task<typename AnyStorage::Iterator>
    {
        auto ptr = co_await storage.m_self->rangeSeekBegin(key);
        co_return Iterator(std::move(ptr));
    }

    // Generic merge for AnyStorage to AnyStorage
    friend task::Task<void> tag_invoke(
        storage2::tag_t<storage2::merge> /*unused*/, AnyStorage& toStorage, AnyStorage& fromStorage)
    {
        co_await toStorage.m_self->mergeFrom(*fromStorage.m_self);
    }
};

}  // namespace bcos::storage2
