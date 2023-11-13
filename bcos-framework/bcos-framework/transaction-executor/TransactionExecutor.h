#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../protocol/TransactionReceiptFactory.h"
#include "../storage/Entry.h"
#include "../storage2/Storage.h"
#include "../storage2/StorageMethods.h"
#include "bcos-utilities/ThreeWay4StringView.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Trait.h>
#include <boost/container/small_vector.hpp>
#include <compare>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bcos::transaction_executor
{
constexpr static size_t CONTRACT_KEY_LENGTH = 32;
constexpr static size_t CONTRACT_TABLE_LENGTH = 48;

template <size_t length>
class SmallString : public boost::container::small_vector<char, length>
{
public:
    using boost::container::small_vector<char, length>::small_vector;
    explicit SmallString(std::string_view view)
    {
        this->assign(RANGES::begin(view), RANGES::end(view));
    }
    explicit SmallString(bytesConstRef ref) { this->assign(RANGES::begin(ref), RANGES::end(ref)); }
    explicit SmallString(const std::string& str)
    {
        this->assign(RANGES::begin(str), RANGES::end(str));
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
    operator std::string_view() const& { return {this->data(), this->size()}; }
};
using ContractTable = SmallString<CONTRACT_TABLE_LENGTH>;
using ContractKey = SmallString<CONTRACT_KEY_LENGTH>;

using StateKeyView = std::tuple<std::string_view, std::string_view>;
using StateKey = std::tuple<ContractTable, ContractKey>;
using StateValue = storage::Entry;

struct ExecuteTransaction
{
    /**
     * @brief Executes a transaction and returns a task that resolves to a transaction receipt.
     *
     * @tparam Executor The type of the executor.
     * @tparam Storage The type of the storage.
     * @tparam Args The types of additional arguments.
     * @param executor The executor instance.
     * @param storage The storage instance.
     * @param blockHeader The block header.
     * @param transaction The transaction to execute.
     * @param args Additional arguments.
     * @return A task that resolves to a transaction receipt.
     */
    auto operator()(auto& executor, auto& storage, const protocol::BlockHeader& blockHeader,
        const protocol::Transaction& transaction, auto&&... args) const
        -> task::Task<protocol::TransactionReceipt::Ptr>
    {
        co_return co_await tag_invoke(*this, executor, storage, blockHeader, transaction,
            std::forward<decltype(args)>(args)...);
    }
};
inline constexpr ExecuteTransaction executeTransaction{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::transaction_executor

template <>
struct std::less<bcos::transaction_executor::StateKey>
{
    auto operator()(bcos::transaction_executor::StateKey const& left,
        bcos::transaction_executor::StateKeyView const& right) const -> bool
    {
        auto leftView = static_cast<bcos::transaction_executor::StateKeyView>(left);
        return leftView < right;
    }
    auto operator()(bcos::transaction_executor::StateKeyView const& left,
        bcos::transaction_executor::StateKey const& right) const -> bool
    {
        auto rightView = static_cast<bcos::transaction_executor::StateKeyView>(right);
        return left < rightView;
    }
    auto operator()(bcos::transaction_executor::StateKey const& left,
        bcos::transaction_executor::StateKey const& right) const -> bool
    {
        auto leftView = static_cast<bcos::transaction_executor::StateKeyView>(left);
        auto rightView = static_cast<bcos::transaction_executor::StateKeyView>(right);
        return leftView < rightView;
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKeyView>
{
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const
    {
        auto const& [table, key] = stateKeyView;
        auto hash = std::hash<std::string_view>{}(table);
        boost::hash_combine(hash, std::hash<std::string_view>{}(key));
        return hash;
    }
};
template <>
struct boost::hash<bcos::transaction_executor::StateKeyView>
{
    size_t operator()(const bcos::transaction_executor::StateKeyView& stateKeyView) const
    {
        return std::hash<bcos::transaction_executor::StateKeyView>{}(stateKeyView);
    }
};

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const
    {
        auto view = static_cast<bcos::transaction_executor::StateKeyView>(stateKey);
        return std::hash<bcos::transaction_executor::StateKeyView>{}(view);
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

template <size_t N>
inline std::ostream& operator<<(
    std::ostream& stream, const bcos::transaction_executor::SmallString<N>& smallString)
{
    stream << static_cast<std::string_view>(smallString);
    return stream;
}
