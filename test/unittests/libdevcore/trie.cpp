/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "MemTrie.h"
#include <json/json.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/MemoryDB.h>
#include <libdevcore/TrieDB.h>
#include <libdevcore/TrieHash.h>
#include <test/tools/libbcos/Options.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/filesystem/path.hpp>
#include <boost/test/unit_test.hpp>
#include <fstream>

using namespace std;
using namespace dev;
using namespace dev::test;

namespace fs = boost::filesystem;
static unsigned fac(unsigned _i)
{
    return _i > 2 ? _i * fac(_i - 1) : _i;
}

using dev::operator<<;

BOOST_AUTO_TEST_SUITE(Crypto)

BOOST_FIXTURE_TEST_SUITE(Trie, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(fat_trie)
{
    h256 r;
    MemoryDB fm;
    {
        FatGenericTrieDB<MemoryDB> ft(&fm);
        ft.init();
        ft.insert(h256("69", h256::FromHex, h256::AlignRight).ref(),
            h256("414243", h256::FromHex, h256::AlignRight).ref());
        for (auto i : ft)
            LOG(INFO) << i.first << i.second;
        r = ft.root();
    }
    {
        FatGenericTrieDB<MemoryDB> ft(&fm);
        ft.setRoot(r);
        for (auto i : ft)
            LOG(INFO) << i.first << i.second;
    }
}

BOOST_AUTO_TEST_CASE(hex_encoded_securetrie_test)
{
    // #ifdef FISCO_GM
    //     fs::path const testPath = test::getTestPath() / fs::path("GMTrieTests");
    // #else
    fs::path const testPath = test::getTestPath() / fs::path("TrieTests");
    // #endif
    LOG(INFO) << "Testing Secure Trie...";
    Json::Value v;
    Json::Reader reader;
#ifdef FISCO_GM
    string const s = contentsString(testPath / fs::path("GMhex_encoded_securetrie_test.json"));
#else
    string const s = contentsString(testPath / fs::path("hex_encoded_securetrie_test.json"));
#endif
    BOOST_REQUIRE_MESSAGE(s.length() > 0,
        "Contents of 'hex_encoded_securetrie_test.json' is empty. Have you cloned the 'tests' repo "
        "branch develop?");
    reader.parse(s, v);
    Json::Value::Members v_mem = v.getMemberNames();
    for (auto it = v_mem.begin(); it != v_mem.end(); it++)
    {
        LOG(INFO) << *it;
        Json::Value& o = v[*it]["in"];
        Json::Value& root_obj = v[*it];
        /// Json::Value& o = v[i];
        vector<pair<string, string>> ss;
        Json::Value::Members mem = o.getMemberNames();
        for (auto it = mem.begin(); it != mem.end(); it++)
        {
            ss.push_back(make_pair(*it, o[*it].asString()));
            if (!ss.back().first.find("0x"))
                ss.back().first = asString(fromHex(ss.back().first.substr(2)));
            if (!ss.back().second.find("0x"))
                ss.back().second = asString(fromHex(ss.back().second.substr(2)));
        }
        for (unsigned j = 0; j < min(1000000000u, fac((unsigned)ss.size())); ++j)
        {
            next_permutation(ss.begin(), ss.end());
            MemoryDB m;
            EnforceRefs r(m, true);
            GenericTrieDB<MemoryDB> t(&m);
            MemoryDB hm;
            EnforceRefs hr(hm, true);
            HashedGenericTrieDB<MemoryDB> ht(&hm);
            MemoryDB fm;
            EnforceRefs fr(fm, true);
            FatGenericTrieDB<MemoryDB> ft(&fm);
            t.init();
            ht.init();
            ft.init();
            BOOST_REQUIRE(t.check(true));
            BOOST_REQUIRE(ht.check(true));
            BOOST_REQUIRE(ft.check(true));
            for (auto const& k : ss)
            {
                t.insert(k.first, k.second);
                ht.insert(k.first, k.second);
                ft.insert(k.first, k.second);
                BOOST_REQUIRE(t.check(true));
                BOOST_REQUIRE(ht.check(true));
                BOOST_REQUIRE(ft.check(true));
                auto i = ft.begin();
                auto j = t.begin();
                for (; i != ft.end() && j != t.end(); ++i, ++j)
                {
                    BOOST_CHECK_EQUAL(i == ft.end(), j == t.end());
                    BOOST_REQUIRE((*i).first.toBytes() == (*j).first.toBytes());
                    BOOST_REQUIRE((*i).second.toBytes() == (*j).second.toBytes());
                }
                BOOST_CHECK_EQUAL(ht.root(), ft.root());
            }
            BOOST_REQUIRE(!root_obj["root"].isNull());
            // #ifdef FISCO_GM
            //             BOOST_CHECK_EQUAL("0xa7b4922e16941f1e35346f747d13d915e8ccf67d335f262fd33cb15236e3dc84",
            //                 toHexPrefixed(ht.root().asArray()));
            //             BOOST_CHECK_EQUAL("0xa7b4922e16941f1e35346f747d13d915e8ccf67d335f262fd33cb15236e3dc84",
            //                 toHexPrefixed(ft.root().asArray()));
            // #else
            BOOST_CHECK_EQUAL(root_obj["root"].asString(), toHexPrefixed(ht.root().asArray()));
            BOOST_CHECK_EQUAL(root_obj["root"].asString(), toHexPrefixed(ft.root().asArray()));
            // #endif
        }
    }
}

BOOST_AUTO_TEST_CASE(trie_test_anyorder)
{
    fs::path const testPath = test::getTestPath() / fs::path("TrieTests");

    LOG(INFO) << "Testing Trie...";
#ifdef FISCO_GM
    string const s = contentsString(testPath / fs::path("GMtrieanyorder.json"));
#else
    string const s = contentsString(testPath / fs::path("trieanyorder.json"));
#endif
    BOOST_REQUIRE_MESSAGE(s.length() > 0,
        "Contents of 'trieanyorder.json' is empty. Have you cloned the 'tests' repo branch "
        "develop?");

    Json::Value v;
    Json::Reader reader;
    reader.parse(s, v);
    Json::Value::Members mem = v.getMemberNames();
    for (auto it = mem.begin(); it != mem.end(); it++)
    {
        LOG(INFO) << *it;
        Json::Value& o = v[*it]["in"];
        Json::Value& root_obj = v[*it];
        Json::Value::Members o_mem = o.getMemberNames();
        vector<pair<string, string>> ss;
        for (auto it = o_mem.begin(); it != o_mem.end(); it++)
        {
            ss.push_back(make_pair(*it, o[*it].asString()));
            if (!ss.back().first.find("0x"))
                ss.back().first = asString(fromHex(ss.back().first.substr(2)));
            if (!ss.back().second.find("0x"))
                ss.back().second = asString(fromHex(ss.back().second.substr(2)));
        }
        for (unsigned j = 0; j < min(1000u, fac((unsigned)ss.size())); ++j)
        {
            next_permutation(ss.begin(), ss.end());
            MemoryDB m;
            EnforceRefs r(m, true);
            GenericTrieDB<MemoryDB> t(&m);
            MemoryDB hm;
            EnforceRefs hr(hm, true);
            HashedGenericTrieDB<MemoryDB> ht(&hm);
            MemoryDB fm;
            EnforceRefs fr(fm, true);
            FatGenericTrieDB<MemoryDB> ft(&fm);
            t.init();
            ht.init();
            ft.init();
            BOOST_REQUIRE(t.check(true));
            BOOST_REQUIRE(ht.check(true));
            BOOST_REQUIRE(ft.check(true));
            for (auto const& k : ss)
            {
                t.insert(k.first, k.second);
                ht.insert(k.first, k.second);
                ft.insert(k.first, k.second);
                BOOST_REQUIRE(t.check(true));
                BOOST_REQUIRE(ht.check(true));
                BOOST_REQUIRE(ft.check(true));
                auto i = ft.begin();
                auto j = t.begin();
                for (; i != ft.end() && j != t.end(); ++i, ++j)
                {
                    BOOST_CHECK_EQUAL(i == ft.end(), j == t.end());
                    BOOST_REQUIRE((*i).first.toBytes() == (*j).first.toBytes());
                    BOOST_REQUIRE((*i).second.toBytes() == (*j).second.toBytes());
                }
                BOOST_CHECK_EQUAL(ht.root(), ft.root());
            }
            BOOST_REQUIRE(!root_obj["root"].isNull());
            // #ifdef FISCO_GM
            //             BOOST_CHECK_EQUAL("0xac0c2b00e9f978a86713cc6dddea3972925f0d29243a2b51a3b597afaf1c7451",
            //                 toHexPrefixed(t.root().asArray()));
            //             BOOST_CHECK_EQUAL(ht.root(), ft.root());

            // #else
            BOOST_CHECK_EQUAL(root_obj["root"].asString(), toHexPrefixed(t.root().asArray()));
            BOOST_CHECK_EQUAL(ht.root(), ft.root());
            // #endif
        }
    }
}

BOOST_AUTO_TEST_CASE(trie_tests_ordered)
{
    fs::path const testPath = test::getTestPath() / fs::path("TrieTests");

    LOG(INFO) << "Testing Trie...";
#ifdef FISCO_GM
    string const s = contentsString(testPath / fs::path("GMtrietest.json"));
#else
    string const s = contentsString(testPath / fs::path("trietest.json"));
#endif
    BOOST_REQUIRE_MESSAGE(s.length() > 0,
        "Contents of 'trietest.json' is empty. Have you cloned the 'tests' repo branch develop?");

    Json::Value v;
    Json::Reader reader;
    reader.parse(s, v);
    Json::Value::Members mem = v.getMemberNames();
    for (auto it = mem.begin(); it != mem.end(); it++)
    {
        LOG(INFO) << *it;
        Json::Value& o = v[*it]["in"];
        Json::Value& root_obj = v[*it];
        vector<pair<string, string>> ss;
        vector<string> keysToBeDeleted;

        for (int i = 0; i < (int)o.size(); i++)
        {
            vector<string> values;
            Json::Value s = o[i];
            if (s.type() == Json::arrayValue)
            {
                for (int j = 0; j < (int)s.size(); j++)
                {
                    if (s[j].type() == Json::stringValue)
                    {
                        values.push_back(s[j].asString());
                    }
                    else if (s[j].type() == Json::nullValue)
                    {
                        // mark entry for deletion
                        values.push_back("");
                        if (!values[0].find("0x"))
                            values[0] = asString(fromHex(values[0].substr(2)));
                        keysToBeDeleted.push_back(values[0]);
                    }
                    else
                        BOOST_FAIL("Bad type (expected string)");
                }
            }
            BOOST_REQUIRE(values.size() == 2);
            ss.push_back(make_pair(values[0], values[1]));
            if (!ss.back().first.find("0x"))
                ss.back().first = asString(fromHex(ss.back().first.substr(2)));
            if (!ss.back().second.find("0x"))
                ss.back().second = asString(fromHex(ss.back().second.substr(2)));
        }
        MemoryDB m;
        EnforceRefs r(m, true);
        GenericTrieDB<MemoryDB> t(&m);
        MemoryDB hm;
        EnforceRefs hr(hm, true);
        HashedGenericTrieDB<MemoryDB> ht(&hm);
        MemoryDB fm;
        EnforceRefs fr(fm, true);
        FatGenericTrieDB<MemoryDB> ft(&fm);
        t.init();
        ht.init();
        ft.init();
        BOOST_REQUIRE(t.check(true));
        BOOST_REQUIRE(ht.check(true));
        BOOST_REQUIRE(ft.check(true));

        for (auto const& k : ss)
        {
            if (find(keysToBeDeleted.begin(), keysToBeDeleted.end(), k.first) !=
                    keysToBeDeleted.end() &&
                k.second.empty())
                t.remove(k.first), ht.remove(k.first), ft.remove(k.first);
            else
                t.insert(k.first, k.second), ht.insert(k.first, k.second),
                    ft.insert(k.first, k.second);
            BOOST_REQUIRE(t.check(true));
            BOOST_REQUIRE(ht.check(true));
            BOOST_REQUIRE(ft.check(true));
            auto i = ft.begin();
            auto j = t.begin();
            for (; i != ft.end() && j != t.end(); ++i, ++j)
            {
                BOOST_CHECK_EQUAL(i == ft.end(), j == t.end());
                BOOST_REQUIRE((*i).first.toBytes() == (*j).first.toBytes());
                BOOST_REQUIRE((*i).second.toBytes() == (*j).second.toBytes());
            }
            BOOST_CHECK_EQUAL(ht.root(), ft.root());
        }

        BOOST_REQUIRE(!root_obj["root"].isNull());
        // #ifdef FISCO_GM
        // BOOST_CHECK_EQUAL(o["root"].get_str(), toHexPrefixed(t.root().asArray()));
        // #else
        BOOST_CHECK_EQUAL(root_obj["root"].asString(), toHexPrefixed(t.root().asArray()));
        // #endif
    }
}

h256 stringMapHash256(StringMap const& _s)
{
    BytesMap bytesMap;
    for (auto const& _v : _s)
        bytesMap.insert(std::make_pair(
            bytes(_v.first.begin(), _v.first.end()), bytes(_v.second.begin(), _v.second.end())));
    return hash256(bytesMap);
}

bytes stringMapRlp256(StringMap const& _s)
{
    BytesMap bytesMap;
    for (auto const& _v : _s)
        bytesMap.insert(std::make_pair(
            bytes(_v.first.begin(), _v.first.end()), bytes(_v.second.begin(), _v.second.end())));
    return rlp256(bytesMap);
}

BOOST_AUTO_TEST_CASE(moreTrieTests)
{
    LOG(INFO) << "Testing Trie more...";
    // More tests...
    {
        MemoryDB m;
        GenericTrieDB<MemoryDB> t(&m);
        t.init();  // initialise as empty tree.
        LOG(INFO) << t;
        LOG(INFO) << m;
        LOG(INFO) << t.root();
        LOG(INFO) << stringMapHash256(StringMap());

        t.insert(string("tesz"), string("test"));
        LOG(INFO) << t;
        LOG(INFO) << m;
        LOG(INFO) << t.root();
        LOG(INFO) << stringMapHash256({{"test", "test"}});

        t.insert(string("tesa"), string("testy"));
        LOG(INFO) << t;
        LOG(INFO) << m;
        LOG(INFO) << t.root();
        LOG(INFO) << stringMapHash256({{"test", "test"}, {"te", "testy"}});
        LOG(INFO) << t.at(string("test"));
        LOG(INFO) << t.at(string("te"));
        LOG(INFO) << t.at(string("t"));

        t.remove(string("te"));
        LOG(INFO) << m;
        LOG(INFO) << t.root();
        LOG(INFO) << stringMapHash256({{"test", "test"}});

        t.remove(string("test"));
        LOG(INFO) << m;
        LOG(INFO) << t.root();
        LOG(INFO) << stringMapHash256(StringMap());
    }
    {
        MemoryDB m;
        GenericTrieDB<MemoryDB> t(&m);
        t.init();  // initialise as empty tree.
        t.insert(string("a"), string("A"));
        t.insert(string("b"), string("B"));
        LOG(INFO) << t;
        LOG(INFO) << m;
        LOG(INFO) << t.root();
        LOG(INFO) << stringMapHash256({{"b", "B"}, {"a", "A"}});
        bytes r(stringMapRlp256({{"b", "B"}, {"a", "A"}}));
        LOG(INFO) << RLP(r);
    }
    {
        MemTrie t;
        t.insert("dog", "puppy");
        LOG(INFO) << hex << t.hash256();
        bytes r(t.rlp());
        LOG(INFO) << RLP(r);
    }
    {
        MemTrie t;
        t.insert("bed", "d");
        t.insert("be", "e");
        LOG(INFO) << hex << t.hash256();
        bytes r(t.rlp());
        LOG(INFO) << RLP(r);
    }
    {
        LOG(INFO) << hex << stringMapHash256({{"dog", "puppy"}, {"doe", "reindeer"}});
        MemTrie t;
        t.insert("dog", "puppy");
        t.insert("doe", "reindeer");
        LOG(INFO) << hex << t.hash256();
        bytes r(t.rlp());
        LOG(INFO) << RLP(r);
        LOG(INFO) << toHex(t.rlp());
    }
    {
        MemoryDB m;
        EnforceRefs r(m, true);
        GenericTrieDB<MemoryDB> d(&m);
        d.init();  // initialise as empty tree.
        MemTrie t;
        StringMap s;

        auto add = [&](char const* a, char const* b) {
            d.insert(string(a), string(b));
            t.insert(a, b);
            s[a] = b;

            LOG(INFO) << "/n-------------------------------";
            LOG(INFO) << a << " -> " << b;
            LOG(INFO) << d;
            LOG(INFO) << m;
            LOG(INFO) << d.root();
            LOG(INFO) << stringMapHash256(s);

            BOOST_REQUIRE(d.check(true));
            BOOST_REQUIRE_EQUAL(t.hash256(), stringMapHash256(s));
            BOOST_REQUIRE_EQUAL(d.root(), stringMapHash256(s));
            for (auto const& i : s)
            {
                (void)i;
                BOOST_REQUIRE_EQUAL(t.at(i.first), i.second);
                BOOST_REQUIRE_EQUAL(d.at(i.first), i.second);
            }
        };

        auto remove = [&](char const* a) {
            s.erase(a);
            t.remove(a);
            d.remove(string(a));

            /*LOG(INFO) << endl << "-------------------------------";
            LOG(INFO) << "X " << a;
            LOG(INFO) << d;
            LOG(INFO) << m;
            LOG(INFO) << d.root();
            LOG(INFO) << hash256(s);*/

            BOOST_REQUIRE(d.check(true));
            BOOST_REQUIRE(t.at(a).empty());
            BOOST_REQUIRE(d.at(string(a)).empty());
            BOOST_REQUIRE_EQUAL(t.hash256(), stringMapHash256(s));
            BOOST_REQUIRE_EQUAL(d.root(), stringMapHash256(s));
            for (auto const& i : s)
            {
                (void)i;
                BOOST_REQUIRE_EQUAL(t.at(i.first), i.second);
                BOOST_REQUIRE_EQUAL(d.at(i.first), i.second);
            }
        };

        add("dogglesworth", "cat");
        add("doe", "reindeer");
        remove("dogglesworth");
        add("horse", "stallion");
        add("do", "verb");
        add("doge", "coin");
        remove("horse");
        remove("do");
        remove("doge");
        remove("doe");
    }
}

namespace
{
/// Creates a random, printable, word.
std::string randomWord()
{
    static std::mt19937_64 s_eng(0);
    std::string ret(std::uniform_int_distribution<size_t>(1, 5)(s_eng), ' ');
    char const n[] = "qwertyuiop";  // asdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM1234567890";
    std::uniform_int_distribution<int> d(0, sizeof(n) - 2);
    for (char& c : ret)
        c = n[d(s_eng)];
    return ret;
}
}  // namespace

BOOST_AUTO_TEST_CASE(trieLowerBound)
{
    LOG(INFO) << "Stress-testing Trie.lower_bound...";
    if (0)
    {
        MemoryDB dm;
        EnforceRefs e(dm, true);
        GenericTrieDB<MemoryDB> d(&dm);
        d.init();  // initialise as empty tree.
        for (int a = 0; a < 20; ++a)
        {
            StringMap m;
            for (int i = 0; i < 50; ++i)
            {
                auto k = randomWord();
                auto v = toString(i);
                m[k] = v;
                d.insert(k, v);
            }

            for (auto i : d)
            {
                auto it = d.lower_bound(i.first);
                for (auto iit = d.begin(); iit != d.end(); ++iit)
                    if ((*iit).first.toString() >= i.first.toString())
                    {
                        BOOST_REQUIRE(it == iit);
                        break;
                    }
            }
            for (unsigned i = 0; i < 100; ++i)
            {
                auto k = randomWord();
                auto it = d.lower_bound(k);
                for (auto iit = d.begin(); iit != d.end(); ++iit)
                    if ((*iit).first.toString() >= k)
                    {
                        BOOST_REQUIRE(it == iit);
                        break;
                    }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(hashedLowerBound)
{
    // get some random keys and their hashes
    std::map<h256, std::string> hashToKey;
    for (int i = 0; i < 10; ++i)
    {
        std::string key = toString(i);
        hashToKey[crypto::Hash(key)] = key;
    }

    // insert keys into trie
    MemoryDB memdb;
    FatGenericTrieDB<MemoryDB> trie(&memdb);
    trie.init();

    for (auto const& hashAndKey : hashToKey)
        trie.insert(hashAndKey.second, std::string("value doesn't matter"));


    // Trie should have the same order of hashed keys as the map.
    // Get some random hashed key to start iteration
    auto itHashToKey = hashToKey.begin();
    ++itHashToKey;
    ++itHashToKey;

    // check trie iteration against map iteration
    for (auto itTrie = trie.hashedLowerBound(itHashToKey->first); itTrie != trie.hashedEnd();
         ++itTrie, ++itHashToKey)
    {
        // check hashed key
        BOOST_CHECK((*itTrie).first.toBytes() == itHashToKey->first.asBytes());
        // check key
        BOOST_CHECK(itTrie.key() == bytes(itHashToKey->second.begin(), itHashToKey->second.end()));
    }

    BOOST_CHECK(itHashToKey == hashToKey.end());
}

BOOST_AUTO_TEST_CASE(trieStess)
{
    LOG(INFO) << "Stress-testing Trie...";
    {
        MemoryDB m;
        MemoryDB dm;
        EnforceRefs e(dm, true);
        GenericTrieDB<MemoryDB> d(&dm);
        d.init();  // initialise as empty tree.
        MemTrie t;
        BOOST_REQUIRE(d.check(true));
        for (int a = 0; a < 20; ++a)
        {
            StringMap m;
            for (int i = 0; i < 50; ++i)
            {
                auto k = randomWord();
                auto v = toString(i);
                m[k] = v;
                t.insert(k, v);
                d.insert(k, v);
                BOOST_REQUIRE_EQUAL(stringMapHash256(m), t.hash256());
                BOOST_REQUIRE_EQUAL(stringMapHash256(m), d.root());
                BOOST_REQUIRE(d.check(true));
            }
            while (!m.empty())
            {
                auto k = m.begin()->first;
                auto v = m.begin()->second;
                d.remove(k);
                t.remove(k);
                m.erase(k);
                if (!d.check(true))
                {
                    // LOG(WARNING) << m;
                    for (auto i : d)
                        LOG(WARNING) << i.first.toString() << i.second.toString();

                    MemoryDB dm2;
                    EnforceRefs e2(dm2, true);
                    GenericTrieDB<MemoryDB> d2(&dm2);
                    d2.init();  // initialise as empty tree.
                    for (auto i : d)
                        d2.insert(i.first, i.second);

                    LOG(WARNING) << "Good:" << d2.root();
                    //					for (auto i: dm2.get())
                    //						LOG(WARNING) << i.first << ": " << RLP(i.second);
                    d2.debugStructure(cerr);
                    LOG(WARNING) << "Broken:"
                                 << d.root();  // Leaves an extension -> extension
                                               // (3c1... -> 742...)
                                               //					for (auto i:
                                               // dm.get()) 						LOG(WARNING)
                                               // << i.first <<
                                               //": " <<
                                               // RLP(i.second);
                    d.debugStructure(cerr);

                    d2.insert(k, v);
                    LOG(WARNING) << "Pres:" << d2.root();
                    //					for (auto i: dm2.get())
                    //						LOG(WARNING) << i.first << ": " << RLP(i.second);
                    d2.debugStructure(cerr);
                    d2.remove(k);

                    LOG(WARNING) << "Good?" << d2.root();
                }
                BOOST_REQUIRE(d.check(true));
                BOOST_REQUIRE_EQUAL(stringMapHash256(m), t.hash256());
                BOOST_REQUIRE_EQUAL(stringMapHash256(m), d.root());
            }
        }
    }
}

template <typename Trie>
void perfTestTrie(char const* _name)
{
    for (size_t p = 1000; p != 1000000; p *= 10)
    {
        MemoryDB dm;
        Trie d(&dm);
        d.init();
        LOG(INFO) << "TriePerf " << _name << p;
        std::vector<h256> keys(1000);
        Timer t;
        size_t ki = 0;
        for (size_t i = 0; i < p; ++i)
        {
            auto k = h256::random();
            auto v = toString(i);
            d.insert(k, v);

            if (i % (p / 1000) == 0)
                keys[ki++] = k;
        }
        LOG(INFO) << "Insert " << p << "values: " << t.elapsed();
        t.restart();
        for (auto k : keys)
            d.at(k);
        LOG(INFO) << "Query 1000 values: " << t.elapsed();
        t.restart();
        size_t i = 0;
        for (auto it = d.begin(); i < 1000 && it != d.end(); ++it, ++i)
            *it;
        LOG(INFO) << "Iterate 1000 values: " << t.elapsed();
        t.restart();
        for (auto k : keys)
            d.remove(k);
        LOG(INFO) << "Remove 1000 values:" << t.elapsed() << "\n";
    }
}

BOOST_AUTO_TEST_CASE(triePerf)
{
    if (test::Options::get().all)
    {
        perfTestTrie<SpecificTrieDB<GenericTrieDB<MemoryDB>, h256>>("GenericTrieDB");
        perfTestTrie<SpecificTrieDB<HashedGenericTrieDB<MemoryDB>, h256>>("HashedGenericTrieDB");
        perfTestTrie<SpecificTrieDB<FatGenericTrieDB<MemoryDB>, h256>>("FatGenericTrieDB");
    }
    else
        clog << "Skipping hive test Crypto/Trie/triePerf. Use --all to run it.\n";
}

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE_END()
