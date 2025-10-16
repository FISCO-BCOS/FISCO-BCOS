#pragma once

#include "Storage.h"
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::storage2
{

// AnyStorage: type-erased wrapper for Storage tag_invoke interface.
// Keep Key/Value as template parameters to preserve strong types while
// erasing the concrete storage implementation.
template <class Key, class ValueT>
class AnyStorage
{
public:
    using KeyType = Key;
    using ValueType = ValueT;
    using DataValue = StorageValueType<ValueType>;

    AnyStorage() = default;
    AnyStorage(const AnyStorage&) = default;
    AnyStorage(AnyStorage&&) noexcept = default;
    AnyStorage& operator=(const AnyStorage&) = default;
    AnyStorage& operator=(AnyStorage&&) noexcept = default;
    ~AnyStorage() = default;

    // Construct from any storage S that supports the required tag_invoke ops
    template <class S>
        requires(!std::is_same_v<std::remove_cvref_t<S>, AnyStorage>)
    explicit AnyStorage(S& storageRef) : m_self(std::make_shared<Model<S>>(storageRef))
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

    struct Concept
    {
        Concept() = default;
        Concept(const Concept&) = default;
        Concept& operator=(const Concept&) = default;
        Concept(Concept&&) noexcept = default;
        Concept& operator=(Concept&&) noexcept = default;
        virtual ~Concept() = default;
        virtual task::Task<std::vector<std::optional<ValueType>>> readSomeVec(
            std::vector<Key> keys) = 0;
        virtual task::Task<void> writeSomeVec(
            std::vector<std::pair<Key, ValueType>> keyValues) = 0;
        virtual task::Task<void> removeSomeVec(std::vector<Key> keys, bool direct) = 0;

        virtual task::Task<std::optional<ValueType>> readOne(Key key) = 0;
        virtual task::Task<void> writeOne(Key key, ValueType value) = 0;
        virtual task::Task<void> removeOne(Key key, bool direct) = 0;

        virtual task::Task<std::unique_ptr<IteratorConcept>> rangeBegin() = 0;
        virtual task::Task<std::unique_ptr<IteratorConcept>> rangeSeekBegin(
            const Key& key) = 0;

        // Generic merge: iterate source and write to destination
        virtual task::Task<void> mergeFrom(Concept& from) = 0;
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
                co_return std::optional<std::pair<Key, DataValue>>{};
            }

            // Accept either pair-like or tuple-like (two elements)
            auto toPair = [](auto&& refTuple) {
                auto&& k = std::get<0>(refTuple);
                auto&& dataValue = std::get<1>(refTuple);
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
                            out.template emplace<ValueType>(
                                ValueType(std::forward<decltype(inner)>(inner)));
                        }
                    },
                    dataValue);
                return std::make_pair(Key(k), std::move(out));
            };

            co_return std::optional<std::pair<Key, DataValue>>{toPair(*item)};
        }

        It m_it;
    };

    template <class S>
    struct Model : Concept
    {
    explicit Model(S& storageRef) : storage(std::addressof(storageRef)) {}

        task::Task<std::vector<std::optional<ValueType>>> readSomeVec(
            std::vector<Key> keys) override
        {
            auto src = co_await readSome(*storage, std::move(keys));
            std::vector<std::optional<ValueType>> dst;
            dst.reserve(src.size());
            for (auto& opt : src)
            {
                if (opt)
                {
                    dst.emplace_back(ValueType(std::move(*opt)));
                }
                else
                {
                    dst.emplace_back(std::nullopt);
                }
            }
            co_return dst;
        }

        task::Task<void> writeSomeVec(
            std::vector<std::pair<Key, ValueType>> keyValues) override
        {
            using SValue = typename std::decay_t<S>::Value;
            std::vector<std::pair<Key, SValue>> converted;
            converted.reserve(keyValues.size());
            for (auto& kv : keyValues)
            {
                converted.emplace_back(std::move(kv.first), SValue(std::move(kv.second)));
            }
            co_await writeSome(*storage, std::move(converted));
            co_return;
        }

        task::Task<void> removeSomeVec(std::vector<Key> keys, bool direct) override
        {
            if (direct)
            {
                co_await removeSome(*storage, std::move(keys), DIRECT);
            }
            else
            {
                co_await removeSome(*storage, std::move(keys));
            }
            co_return;
        }

        task::Task<std::optional<ValueType>> readOne(Key key) override
        {
            auto src = co_await storage2::readOne(*storage, std::move(key));
            if (src)
            {
                co_return std::optional<ValueType>(ValueType(std::move(*src)));
            }
            co_return std::optional<ValueType>{};
        }

        task::Task<void> writeOne(Key key, ValueType value) override
        {
            using SValue = typename std::decay_t<S>::Value;
            co_await storage2::writeOne(
                *storage, std::move(key), SValue(std::move(value)));
            co_return;
        }

        task::Task<void> removeOne(Key key, bool direct) override
        {
            if (direct)
            {
                co_await storage2::removeOne(*storage, std::move(key), DIRECT);
            }
            else
            {
                co_await storage2::removeOne(*storage, std::move(key));
            }
            co_return;
        }

        task::Task<std::unique_ptr<IteratorConcept>> rangeBegin() override
        {
            auto it = co_await storage2::range(*storage);
            co_return std::unique_ptr<IteratorConcept>(
                new IteratorModel<decltype(it)>(std::move(it)));
        }

        task::Task<std::unique_ptr<IteratorConcept>> rangeSeekBegin(const Key& key) override
        {
            if constexpr (requires { tag_invoke(range, *storage, RANGE_SEEK, key); })
            {
                auto it = co_await storage2::range(*storage, RANGE_SEEK, key);
                co_return std::unique_ptr<IteratorConcept>(
                    new IteratorModel<decltype(it)>(std::move(it)));
            }
            else
            {
                // Fallback: no seek support, return begin()
                auto it = co_await storage2::range(*storage);
                co_return std::unique_ptr<IteratorConcept>(
                    new IteratorModel<decltype(it)>(std::move(it)));
            }
        }

        task::Task<void> mergeFrom(Concept& from) override
        {
            // Generic merge: iterate from.range() and apply to this storage
            // Try to get an iterator from 'from'
            auto fromIteratorPtr = co_await from.rangeBegin();
            while (true)
            {
                auto item = co_await fromIteratorPtr->next();
                if (!item)
                {
                    break;
                }
                auto&& [k, dataValue] = *item;
                if (std::holds_alternative<NOT_EXISTS_TYPE>(dataValue))
                {
                    continue;  // skip
                }
                if (std::holds_alternative<DELETED_TYPE>(dataValue))
                {
                    co_await storage2::removeOne(*storage, Key(k));
                    continue;
                }
                auto& valueRef = std::get<ValueType>(dataValue);
                co_await storage2::writeOne(*storage, Key(k), ValueType(valueRef));
            }
            co_return;
        }

        S* storage;
    };

    std::shared_ptr<Concept> m_self;

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
        ::ranges::input_range auto keys)
        -> task::Task<std::vector<std::optional<Value>>>
    {
        std::vector<Key> vec;
        for (auto&& k : keys)
        {
            vec.emplace_back(Key(std::forward<decltype(k)>(k)));
        }
        co_return co_await storage.m_self->readSomeVec(std::move(vec));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keyValues) -> task::Task<void>
        requires(std::tuple_size_v<::ranges::range_value_t<decltype(keyValues)>> >= 2)
    {
        std::vector<std::pair<Key, Value>> vec;
        for (auto&& keyValue : keyValues)
        {
            auto&& k = std::get<0>(keyValue);
            auto&& value = std::get<1>(keyValue);
            vec.emplace_back(Key(std::forward<decltype(k)>(k)),
                ValueType(std::forward<decltype(value)>(value)));
        }
        co_await storage.m_self->writeSomeVec(std::move(vec));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keys) -> task::Task<void>
    {
        std::vector<Key> vec;
        for (auto&& k : keys)
        {
            vec.emplace_back(Key(std::forward<decltype(k)>(k)));
        }
        co_await storage.m_self->removeSomeVec(std::move(vec), false);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, AnyStorage& storage,
        ::ranges::input_range auto keys, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        std::vector<Key> vec;
        for (auto&& k : keys)
        {
            vec.emplace_back(Key(std::forward<decltype(k)>(k)));
        }
        co_await storage.m_self->removeSomeVec(std::move(vec), true);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, AnyStorage& storage,
        auto key) -> task::Task<std::optional<Value>>
    {
        co_return co_await storage.m_self->readOne(Key(std::forward<decltype(key)>(key)));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, AnyStorage& storage,
        auto key, auto value) -> task::Task<void>
    {
        co_await storage.m_self->writeOne(
            Key(std::forward<decltype(key)>(key)), Value(std::forward<decltype(value)>(value)));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeOne> /*unused*/, AnyStorage& storage,
        auto key) -> task::Task<void>
    {
        co_await storage.m_self->removeOne(Key(std::forward<decltype(key)>(key)), false);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeOne> /*unused*/, AnyStorage& storage,
        auto key, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        co_await storage.m_self->removeOne(Key(std::forward<decltype(key)>(key)), true);
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
        storage2::tag_t<storage2::merge> /*unused*/, AnyStorage& toStorage,
        AnyStorage& fromStorage)
    {
        co_await toStorage.m_self->mergeFrom(*fromStorage.m_self);
        co_return;
    }
};

}  // namespace bcos::storage2
