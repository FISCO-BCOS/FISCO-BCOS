#pragma once

#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../protocol/TransactionReceiptFactory.h"
#include "../storage/Entry.h"
#include "../storage2/StringPool.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Trait.h>
#include <boost/container/small_vector.hpp>
#include <compare>
#include <type_traits>

namespace bcos::transaction_executor
{
using TableNamePool = storage2::string_pool::FixedStringPool;
using TableNameID = storage2::string_pool::StringID;

constexpr static size_t MOSTLY_KEY_LENGTH = 32;
class SmallKey : public boost::container::small_vector<char, MOSTLY_KEY_LENGTH>
{
public:
    using boost::container::small_vector<char, MOSTLY_KEY_LENGTH>::small_vector;

    explicit SmallKey(concepts::bytebuffer::ByteBuffer auto const& buffer)
      : boost::container::small_vector<char, MOSTLY_KEY_LENGTH>::small_vector::small_vector(
            RANGES::begin(buffer), RANGES::end(buffer))
    {}

    std::string_view toStringView() const& { return {this->data(), this->size()}; }
};

using StateKey = std::tuple<TableNameID, SmallKey>;
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
            protocol::ReceiptFactoryReturnType<ReceiptFactory>>;
    };
}  // namespace bcos::transaction_executor

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& stateKey) const
    {
        auto const& [table, key] = stateKey;
        size_t hash = 0;
        boost::hash_combine(hash, std::hash<bcos::transaction_executor::TableNameID>{}(table));
        boost::hash_combine(hash, std::hash<std::string_view>{}(key.toStringView()));
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
    std::ostream& stream, bcos::transaction_executor::SmallKey const& smallKey)
{
    stream << smallKey.toStringView();
    return stream;
}