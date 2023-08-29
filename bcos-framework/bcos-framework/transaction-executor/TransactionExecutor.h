#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../protocol/TransactionReceiptFactory.h"
#include "../storage/Entry.h"
#include "../storage2/Storage.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Trait.h>
#include <boost/container/small_vector.hpp>
#include <compare>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bcos::transaction_executor
{
constexpr static size_t MOSTLY_LENGTH = 32;

class SmallString : public boost::container::small_vector<char, MOSTLY_LENGTH>
{
public:
    using boost::container::small_vector<char, MOSTLY_LENGTH>::small_vector;
    SmallString(concepts::bytebuffer::ByteBuffer auto const& bytes)
    {
        assign(RANGES::begin(bytes), RANGES::end(bytes));
    }
    auto operator<=>(std::string_view view) const
    {
        return static_cast<std::string_view>(*this) <=> view;
    }
    auto operator<=>(std::string const& str) const
    {
        return static_cast<std::string_view>(*this) <=> std::string_view(str);
    }
    auto operator<=>(SmallString const& rhs) const
    {
        return static_cast<std::string_view>(*this) <=> static_cast<std::string_view>(rhs);
    }
    operator std::string_view() const& { return {data(), size()}; }
};
using StateKeyView = std::tuple<std::string_view, std::string_view>;
using StateKey = std::tuple<SmallString, SmallString>;
using StateValue = storage::Entry;

template <class StorageType>
concept StateStorage = requires() {
                           requires storage2::ReadableStorage<StorageType>;
                           requires storage2::WriteableStorage<StorageType>;
                       };

template <class TransactionExecutorType, class Storage, class ReceiptFactory>
concept TransactionExecutor =
    requires(TransactionExecutorType executor, const protocol::BlockHeader& blockHeader,
        Storage& storage, ReceiptFactory& receiptFactory) {
        requires StateStorage<Storage>;
        requires protocol::IsTransactionReceiptFactory<ReceiptFactory>;

        requires std::same_as<task::AwaitableReturnType<decltype(executor.execute(
                                  blockHeader, std::declval<protocol::Transaction>(), 0))>,
            protocol::TransactionReceipt::Ptr>;
    };
}  // namespace bcos::transaction_executor

template <>
struct std::less<bcos::transaction_executor::StateKey>
{
    auto operator()(bcos::transaction_executor::StateKey const& left,
        bcos::transaction_executor::StateKeyView const& right) const -> bool
    {
        auto const& [leftTable, leftKey] = left;
        auto const& [rightTable, rightKey] = right;

        auto tableEqual = leftTable <=> rightTable;
        if (tableEqual != std::weak_ordering::equivalent)
        {
            return leftKey < rightKey;
        }

        return tableEqual == std::weak_ordering::less;
    }
    auto operator()(bcos::transaction_executor::StateKeyView const& left,
        bcos::transaction_executor::StateKey const& right) const -> bool
    {
        return !(*this)(right, left);
    }
    auto operator()(bcos::transaction_executor::StateKey const& left,
        bcos::transaction_executor::StateKey const& right) const -> bool
    {
        auto const& [leftTable, leftKey] = left;
        auto const& [rightTable, rightKey] = right;

        auto tableEqual = leftTable <=> rightTable;
        if (tableEqual != std::weak_ordering::equivalent)
        {
            return leftKey < rightKey;
        }

        return tableEqual == std::weak_ordering::less;
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const
    {
        auto const& [table, key] = stateKey;
        auto hash = std::hash<std::string_view>{}(table);
        boost::hash_combine(hash, std::hash<std::string_view>{}(key));
        return hash;
    }
};

template <>
struct boost::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const
    {
        return std::hash<bcos::transaction_executor::StateKey>{}(stateKey);
    }
};

inline std::ostream& operator<<(
    std::ostream& stream, const bcos::transaction_executor::SmallString& smallString)
{
    stream << static_cast<std::string_view>(smallString);
    return stream;
}