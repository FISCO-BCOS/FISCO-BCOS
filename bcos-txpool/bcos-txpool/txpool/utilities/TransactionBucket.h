#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/txpool/TxPoolTypeDef.h"
#include "bcos-utilities/BucketMap.h"
#include "boost/multi_index/hashed_index.hpp"
#include "boost/multi_index/member.hpp"
#include "boost/multi_index/ordered_index.hpp"
#include "boost/multi_index_container.hpp"

namespace bcos
{
class MultiIndexTxContainer
{
public:
    struct TransactionData
    {
        bcos::crypto::HashType txHash;
        std::int64_t timeStamp;
        bcos::protocol::Transaction::Ptr transaction;

        TransactionData(bcos::protocol::Transaction::Ptr _transaction)
          : txHash(_transaction->hash()),
            timeStamp(_transaction->importTime() * 1000000 + utcTimeUs() % 1000000),
            transaction(_transaction)
        {}
    };

    MultiIndexTxContainer() = default;
    MultiIndexTxContainer(const MultiIndexTxContainer&) = default;
    MultiIndexTxContainer(MultiIndexTxContainer&&) = default;
    MultiIndexTxContainer& operator=(const MultiIndexTxContainer&) = default;
    MultiIndexTxContainer& operator=(MultiIndexTxContainer&&) noexcept = default;


    using Container = boost::multi_index::multi_index_container<TransactionData,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::member<TransactionData,
                bcos::crypto::HashType, &TransactionData::txHash>>,
            boost::multi_index::ordered_unique<boost::multi_index::member<TransactionData,
                std::int64_t, &TransactionData::timeStamp>>>>;


    class IteratorImpl
    {
    public:
        IteratorImpl(typename Container::nth_index<0>::type::iterator& it)
          : m_iterator(it),
            first(m_iterator->txHash),
            // we assume that _transaction is not indexed so we can cast it to non-const
            second(const_cast<bcos::protocol::Transaction::Ptr&>(m_iterator->transaction))
        {}

        IteratorImpl(const bcos::crypto::HashType& _first,
            const bcos::protocol::Transaction::Ptr& _transaction)
          : m_iterator(),
            first(_first),
            // we assume that _transaction is not indexed so we can cast it to non-const
            second(const_cast<bcos::protocol::Transaction::Ptr&>(_transaction))
        {}

        friend bool operator==(
            const std::shared_ptr<IteratorImpl>& lhs, const std::shared_ptr<IteratorImpl>& rhs)
        {
            if (lhs && rhs)
            {
                return *lhs == *rhs;
            }
            return lhs == rhs;
        }

        bool operator==(const IteratorImpl& other) const { return m_iterator == other.m_iterator; }


    private:
        typename Container::nth_index<0>::type::iterator m_iterator;

    public:
        const bcos::crypto::HashType& first;
        bcos::protocol::Transaction::Ptr& second;
    };
    using iterator = std::shared_ptr<IteratorImpl>;


    iterator find(const bcos::crypto::HashType& key)
    {
        auto it = multiIndexMap.get<0>().find(key);
        if (it != multiIndexMap.get<0>().end())
        {
            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << LOG_KV("txHash:", it->txHash)
                                  << LOG_KV("timestamp:", it->timeStamp);
            }
        }
        else
        {
            if (c_fileLogLevel == LogLevel::DEBUG) [[unlikely]]
            {
                TXPOOL_LOG(DEBUG) << "bucket not found the transaction,txHash: " << key;
            }
        }

        return std::make_shared<IteratorImpl>(it);
    }

    iterator end()
    {
        auto it = multiIndexMap.get<0>().end();
        return std::make_shared<IteratorImpl>(it);
    }

    iterator begin()
    {
        auto it = multiIndexMap.get<0>().begin();
        return std::make_shared<IteratorImpl>(it);
    }

    std::pair<iterator, bool> try_emplace(
        const bcos::crypto::HashType& key, const bcos::protocol::Transaction::Ptr& value)
    {
        TransactionData newData(value);
        auto result = multiIndexMap.emplace(std::move(newData));
        if (!result.second)
        {
            TXPOOL_LOG(DEBUG) << LOG_DESC("bucket insert failed") << LOG_KV("txHash", key)
                              << LOG_KV("timestamp", newData.timeStamp);
        }
        return {std::make_shared<IteratorImpl>(result.first), result.second};
    }

    bool contains(const bcos::crypto::HashType& key)
    {
        return multiIndexMap.get<0>().find(key) != multiIndexMap.get<0>().end();
    }
    void erase(std::shared_ptr<IteratorImpl>& it_ptr)
    {
        auto eraseCount = multiIndexMap.get<0>().erase(it_ptr->first);
        if (eraseCount == 0)
        {
            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << LOG_DESC("bucket erase failed")
                                  << LOG_KV("eraseCount", eraseCount);
            }
        }
        else
        {
            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << LOG_DESC("bucket erase success")
                                  << LOG_KV("eraseCount", eraseCount);
            }
        }
    }
    size_t size() const { return multiIndexMap.size(); }
    void clear() { multiIndexMap.clear(); }

    template <class AccessorType>
    bool forEach(std::function<bool(typename AccessorType::Ptr)> handler,
        typename AccessorType::Ptr accessor = nullptr)
    {
        for (const auto& item : multiIndexMap.get<1>())
        {
            auto it = IteratorImpl(item.txHash, item.transaction);
            accessor->setValue(std::make_shared<IteratorImpl>(it));
            if (!handler(accessor))
            {
                return false;
            }
        }
        return true;
    }

protected:
    Container multiIndexMap;
};


class TransactionBucket
  : public Bucket<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr, MultiIndexTxContainer>
{
public:
    using Base =
        Bucket<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr, TransactionBucket>;
    using Ptr = std::shared_ptr<TransactionBucket>;
    TransactionBucket() = default;

    template <class AccessorType>  // handler return isContinue
    bool forEach(std::function<bool(typename AccessorType::Ptr)> handler,
        typename AccessorType::Ptr accessor = nullptr)
    {
        if (!accessor)
        {
            accessor =
                std::make_shared<AccessorType>(this->shared_from_this());  // acquire lock here
        }

        return m_values.forEach<AccessorType>(handler, accessor);
    }
};
}  // namespace bcos