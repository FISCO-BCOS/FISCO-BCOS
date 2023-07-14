#include "bcos-utilities/bcos-utilities/FixedBytes.h"
#include "bcos-utilities/bcos-utilities/BucketMap.h"
#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include "bcos-crypto/bcos-crypto/interfaces/crypto/CommonType.h"
#include "boost/multi_index/mem_fun.hpp"
#include "boost/multi_index_container.hpp"
#include "boost/multi_index/hashed_index.hpp"
#include "boost/multi_index/member.hpp"
#include "boost/multi_index/ordered_index.hpp"

using namespace bcos;

class MultiIndexTxContainer
{
public:
    struct MyStruct
    {
        bcos::crypto::HashType txHash;
        std::int64_t timeStamp;
        bcos::protocol::Transaction::Ptr transaction;

        MyStruct(bcos::crypto::HashType _txHash, bcos::protocol::Transaction::Ptr _transaction)
                : txHash(_txHash), timeStamp(_transaction->importTime()), transaction(_transaction)
        {
        }
    };

    MultiIndexTxContainer() = default;
    MultiIndexTxContainer(const MultiIndexTxContainer&) = default;
    MultiIndexTxContainer(MultiIndexTxContainer&&) = default;
    MultiIndexTxContainer& operator=(const MultiIndexTxContainer&) = default;
    MultiIndexTxContainer& operator=(MultiIndexTxContainer&&) noexcept = default;

    using Container = boost::multi_index::multi_index_container<
            MyStruct,
            boost::multi_index::indexed_by<
                    boost::multi_index::hashed_unique<
                            boost::multi_index::member<MyStruct, bcos::crypto::HashType, &MyStruct::txHash>>,
                    boost::multi_index::ordered_non_unique<
                            boost::multi_index::member<MyStruct, std::int64_t, &MyStruct::timeStamp>>>>;



    class IteratorImpl
    {
    public:
        IteratorImpl(typename Container::nth_index<0>::type::iterator& it)
                : first(it->txHash), second(it->transaction), m_iterator(it)
        {}

        IteratorImpl(const bcos::crypto::HashType& _first, const bcos::protocol::Transaction::Ptr& _transaction)
                : first(_first), second(_transaction), m_iterator()
        {}

        friend bool operator==(const std::shared_ptr<IteratorImpl>& lhs, const std::shared_ptr<IteratorImpl>& rhs) {
            if (lhs && rhs) {
                return *lhs == *rhs;
            }
            return lhs.get() == rhs.get();
        }

        bool operator==(const IteratorImpl& other) const {
            return m_iterator == other.m_iterator && first == other.first && second == other.second;
        }

        Container::nth_index<0>::type::iterator getIterator() const { return m_iterator; }

        const bcos::crypto::HashType& first;
        const bcos::protocol::Transaction::Ptr& second;
    private:
        typename Container::nth_index<0>::type::iterator m_iterator;
    };
    using iterator = std::shared_ptr<IteratorImpl>;




    iterator find(const bcos::crypto::HashType& key)
    {
        auto it =  multiIndexMap.get<0>().find(key);
        if(it != multiIndexMap.get<0>().end()){
            TXPOOL_LOG(DEBUG) << LOG_KV("txHash:",it->txHash) << LOG_KV("timestamp:",it->timeStamp);
        }
        else{
            TXPOOL_LOG(DEBUG) <<"bucket not found the transaction,txHash: " << key;
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

    std::pair<iterator, bool> try_emplace(const bcos::crypto::HashType& key,
                                          const bcos::protocol::Transaction::Ptr& value)
    {
        MyStruct newData(key, value);
        auto result = multiIndexMap.insert(newData);
        auto txhash = result.first->txHash;
        auto it = multiIndexMap.get<0>().find(txhash);
        return {std::make_shared<IteratorImpl>(it), result.second};
    }

    bool contains(const bcos::crypto::HashType& key){
        return multiIndexMap.get<0>().find(key) != multiIndexMap.get<0>().end();
    }
    void erase(std::shared_ptr<IteratorImpl> it_ptr){
        multiIndexMap.get<0>().erase(it_ptr->getIterator());
    }
    size_t size() const { return multiIndexMap.size(); }
    void clear(){ multiIndexMap.clear(); }

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


class TransactionBucket : public Bucket<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr, MultiIndexTxContainer>
{
public:

    using Base = Bucket<bcos::crypto::HashType, bcos::protocol::Transaction::Ptr, TransactionBucket>;
    using Ptr = std::shared_ptr<TransactionBucket>;
    TransactionBucket() = default;

    template <class AccessorType>  // handler return isContinue
    bool forEach(std::function<bool(typename AccessorType::Ptr)> handler,
                 typename AccessorType::Ptr accessor = nullptr){
        if (!accessor)
        {
            accessor = std::make_shared<AccessorType>(this->shared_from_this());  // acquire lock here
        }

        return m_values.forEach<AccessorType>(handler, accessor);
    }
};

