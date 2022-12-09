#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <string>
#include <variant>

namespace bcos::test
{
struct HashMapFixture
{
    HashMapFixture()
    {
        keys.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            keys[i] = boost::lexical_cast<std::string>(i);
        }
    }

    std::vector<std::string> keys;
    size_t count = 100 * 1000;
};

struct KeyType
{
    KeyType(std::string_view key) : m_key(std::move(key)) {}
    KeyType(std::string key) : m_key(std::move(key)) {}

    std::string_view key() const
    {
        std::string_view view;
        std::visit([&view](auto&& key) { view = std::string_view(key); }, m_key);

        return view;
    }

    bool operator==(const KeyType& rhs) const { return key() == rhs.key(); }

    std::variant<std::string_view, std::string> m_key;
};

struct KeyTypeHasher
{
    size_t hash(const KeyType& key) const { return hasher(key.key()); }
    bool equal(const KeyType& lhs, const KeyType& rhs) const { return lhs.key() == rhs.key(); }

    std::hash<std::string_view> hasher;
};

struct KeyTypeHasherForUnordered
{
    size_t operator()(const KeyType& key) const { return hasher(key.key()); }

    std::hash<std::string_view> hasher;
};

BOOST_FIXTURE_TEST_SUITE(TestHashMap, HashMapFixture)

BOOST_AUTO_TEST_CASE(hashmap)
{
    tbb::concurrent_hash_map<KeyType, uint64_t, KeyTypeHasher> map;

    auto now = std::chrono::system_clock::now();
    for (auto& it : keys)
    {
        map.emplace(std::move(it), 0);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - now);
    std::cout << "hashmap elapsed: " << elapsed.count() << std::endl;
}

BOOST_AUTO_TEST_CASE(unorderedmap)
{
    tbb::concurrent_unordered_map<KeyType, uint64_t, KeyTypeHasherForUnordered> map;

    auto now = std::chrono::system_clock::now();
    for (auto& it : keys)
    {
        map.emplace(std::move(it), 0);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - now);
    std::cout << "unorderedmap elapsed: " << elapsed.count() << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test