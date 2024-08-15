#pragma once
#include <oneapi/tbb/concurrent_hash_map.h>
namespace bcos::string_cache
{
class StringCache
{
private:
    struct Empty
    {
    };
    tbb::concurrent_hash_map<std::string, Empty> m_cache;

    using ReadAccessor = decltype(m_cache)::const_accessor;

public:
    intptr_t getStringID(std::string_view str)
    {
        std::string ss(str);
        ReadAccessor readIterator;
        if (m_cache.find(readIterator, ss))
        {
            return reinterpret_cast<intptr_t>(std::addressof(readIterator->first));
        }

        readIterator->first;
        m_cache.insert(str);
        return reinterpret_cast<intptr_t>(&(*m_cache.find(str)));
    }
};
}  // namespace bcos::string_cache