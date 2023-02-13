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
#include <type_traits>

namespace bcos::transaction_executor
{
using TableNamePool = storage2::string_pool::FixedStringPool;
using TableNameID = storage2::string_pool::StringID;

class SmallKey : public boost::container::small_vector<char, 32>
{
public:
    using boost::container::small_vector<char, 32>::small_vector;
    SmallKey(concepts::bytebuffer::ByteBuffer auto const& buffer)
      : boost::container::small_vector<char, 32>::small_vector::small_vector(
            RANGES::begin(buffer), RANGES::end(buffer))
    {}
    SmallKey& operator=(concepts::bytebuffer::ByteBuffer auto const& buffer)
    {
        this->assign(RANGES::begin(buffer), RANGES::end(buffer));
        return *this;
    }

    std::string_view toStringView() const& { return {this->data(), this->size()}; }
    std::string_view operator()() const& { return toStringView(); }
};

using StateKey = std::tuple<TableNameID, SmallKey>;  // TODO: 计算最多支持多少表名
using StateValue = storage::Entry;

const static auto EMPTY_STATE_KEY = std::tuple{TableNameID(), std::string_view()};

template <class StorageType>
concept StateStorage = requires()
{
    requires storage2::ReadableStorage<StorageType>;
    requires storage2::WriteableStorage<StorageType>;
};

template <class TransactionExecutorType, class Storage, class ReceiptFactory>
concept TransactionExecutor = requires(TransactionExecutorType executor,
    const protocol::BlockHeader& blockHeader, const protocol::Transaction& transaction)
{
    requires StateStorage<Storage>;
    requires protocol::IsTransactionReceiptFactory<ReceiptFactory>;

    requires std::same_as<
        task::AwaitableReturnType<decltype(executor.execute(blockHeader, transaction, 0))>,
        protocol::ReceiptFactoryReturnType<ReceiptFactory>>;
};

// All auto interfaces is awaitable
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

inline std::ostream& operator<<(
    std::ostream& stream, bcos::transaction_executor::SmallKey const& smallKey)
{
    stream << smallKey.toStringView();
    return stream;
}