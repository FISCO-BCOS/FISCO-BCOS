/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Unit tests for the Entry
 * @file Entry.cpp
 */

// Allow unit tests to inspect private members for buffer-model verification

#include "bcos-framework/protocol/Protocol.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-crypto/hash/SM3.h>
#include <bcos-framework/storage/Serialize.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <span>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::storage;

namespace bcos
{
namespace test
{
using namespace std;

struct EntryFixture
{
    EntryFixture()
    {
        tableInfo = std::make_shared<TableInfo>("testTable", std::vector<std::string>{"key2"});
    }

    ~EntryFixture() {}

    std::shared_ptr<TableInfo> tableInfo;
};
BOOST_FIXTURE_TEST_SUITE(EntryTest, EntryFixture)

BOOST_AUTO_TEST_CASE(viewEqual)
{
    std::string a = "value";

    BOOST_CHECK_EQUAL(a, "value");
    BOOST_CHECK_EQUAL(std::string_view(a), "value");
}

BOOST_AUTO_TEST_CASE(copyFrom)
{
    auto entry1 = std::make_shared<Entry>();
    auto entry2 = std::make_shared<Entry>();
    BOOST_CHECK_EQUAL(entry1->dirty(), false);
    entry1->set("value");
    BOOST_TEST(entry1->dirty() == true);
    BOOST_TEST(entry1->size() == 5);

    *entry2 = *entry1;

    {
        auto entry3 = Entry(*entry1);

        entry3.set("i am key2");

        auto entry4(std::move(entry3));

        auto entry5(*entry2);

        auto entry6(std::move(entry5));
    }

    BOOST_CHECK_EQUAL(entry2->get(), "value"sv);

    entry2->set("value2");

    BOOST_CHECK_EQUAL(entry2->get(), "value2");
    BOOST_CHECK_EQUAL(entry1->get(), "value");

    entry2->set("value3");
    BOOST_TEST(entry2->size() == 6);
    BOOST_TEST(entry2->get() == "value3");
    *entry2 = *entry2;
    BOOST_TEST(entry2->dirty() == true);
    // entry2->setDirty(false);
    entry2->setStatus(Entry::Status::NORMAL);
    BOOST_TEST(entry2->dirty() == false);
    // test setField lValue and rValue
    entry2->set(string("value2"));
    BOOST_TEST(entry2->dirty() == true);
    BOOST_TEST(entry2->size() == 6);
    auto value2 = "value2";
    entry2->set(value2);
}

BOOST_AUTO_TEST_CASE(functions)
{
    auto entry = std::make_shared<Entry>();
    BOOST_TEST(entry->dirty() == false);
    BOOST_TEST(entry->status() == Entry::Status::EMPTY);
    entry->setStatus(Entry::Status::DELETED);
    BOOST_TEST(entry->status() == Entry::Status::DELETED);
    BOOST_TEST(entry->dirty() == true);
}

BOOST_AUTO_TEST_CASE(BytesField)
{
    Entry entry;

    std::string value = "abcdefghijklmn";
    std::vector<char> data;
    data.assign(value.begin(), value.end());

    entry.set(std::string(value));

    BOOST_CHECK_EQUAL(entry.get(), value);

    Entry entry2;
    entry2.set(data);

    BOOST_CHECK_EQUAL(entry2.get(), value);
}

BOOST_AUTO_TEST_CASE(capacity)
{
    Entry entry;

    entry.set(std::string("abc"));

    entry.set(std::string("abdflsakdjflkasjdfoiqwueroi!!!!sdlkfjsldfbclsadflaksjdfpqweioruaaa"));

    BOOST_CHECK_LT(entry.size(), 100);
    BOOST_CHECK_GT(entry.size(), 0);
}

BOOST_AUTO_TEST_CASE(object)
{
    std::tuple<int, std::string, std::string> value = std::make_tuple(100, "hello", "world");

    Entry entry;
    entry.set(bcos::storage::serialize::encode(value));

    auto out =
        bcos::storage::serialize::decode<std::tuple<int, std::string, std::string>>(entry.get());

    BOOST_CHECK(out == value);
}

BOOST_AUTO_TEST_CASE(largeObject)
{
    Entry entry;
    entry.set(std::string(1024, 'a'));

    BOOST_CHECK_EQUAL(entry.get(), std::string(1024, 'a'));
}

BOOST_AUTO_TEST_CASE(stringView)
{
    Entry entry;
    std::string_view a(
        "Hello world! fisco bcos! fisco bcos! fisco bcos! fisco bcos! larger than 32");
    entry.set(a);

    Entry entry2 = entry;
    BOOST_CHECK_EQUAL(entry2.get(), a);
}

BOOST_AUTO_TEST_CASE(entryHash)
{
    auto data = "Hello world!"s;
    auto table = "table!"s;
    auto key = "key!"s;

    Entry entry;
    entry.setStatus(Entry::MODIFIED);
    entry.set(data);

    auto sm3 = std::make_shared<bcos::crypto::SM3>();
    auto oldHash = entry.hash(table, key, *sm3, 0);
    auto oldExpect = sm3->hash(bytesConstRef((bcos::byte*)data.data(), data.size()));
    BOOST_CHECK_EQUAL(oldHash, oldExpect);

    entry.setStatus(Entry::DELETED);
    auto deletedHash =
        entry.hash(table, key, *sm3, (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);

    auto hasher = sm3->hasher();
    hasher.update(table);
    hasher.update(key);

    bcos::crypto::HashType deletedExpect;
    hasher.final(deletedExpect);
    BOOST_CHECK_EQUAL(deletedHash, deletedExpect);

    entry.setStatus(Entry::MODIFIED);
    entry.set(data);
    auto modifyHash =
        entry.hash(table, key, *sm3, (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    hasher = sm3->hasher();
    hasher.update(table);
    hasher.update(key);
    hasher.update(data);

    bcos::crypto::HashType modifyExpect;
    hasher.final(modifyExpect);
    BOOST_CHECK_EQUAL(modifyHash, modifyExpect);

    entry.setStatus(Entry::NORMAL);
    auto normalHash =
        entry.hash(table, key, *sm3, (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    BOOST_CHECK_EQUAL(normalHash, bcos::crypto::HashType{});
}

// ── Buffer model coverage tests ───────────────────────────────────
// Each test verifies data correctness AND the buffer model via behavioral checks
// (entryTestHolder provides access to the proxy for has_value() verification).

// SmallBuffer: data ≤ 31 bytes, stored inline with 1-byte size overhead
BOOST_AUTO_TEST_CASE(smallBuffer_stdString)
{
    std::string value = "hello";  // 5 bytes → SmallBuffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), value);
    BOOST_CHECK_EQUAL(entry.size(), 5);
    BOOST_CHECK_EQUAL(entry.get(), value);
    BOOST_CHECK_EQUAL(entry.status(), Entry::MODIFIED);
    // Verify holder has a value and data is inline (modifying source doesn't affect entry)
    BOOST_TEST(entryTestHolder(entry).has_value());
    value[0] = 'H';
    BOOST_CHECK_EQUAL(entry.get(), std::string_view("hello"));  // unchanged = SmallBuffer copy
}

BOOST_AUTO_TEST_CASE(smallBuffer_boundary31)
{
    std::string value(31, 'x');  // exactly 31 bytes → SmallBuffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), value);
    BOOST_CHECK_EQUAL(entry.size(), 31);
    BOOST_TEST(entryTestHolder(entry).has_value());
    value[0] = 'X';
    BOOST_CHECK_EQUAL(entry.get()[0], 'x');  // unchanged
}

BOOST_AUTO_TEST_CASE(smallBuffer_vectorChar)
{
    std::vector<char> value = {'v', 'e', 'c'};  // 3 bytes → SmallBuffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), std::string_view("vec"));
    BOOST_CHECK_EQUAL(entry.size(), 3);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(smallBuffer_vectorUnsignedChar)
{
    std::vector<unsigned char> value = {0x00, 0xFF, 0x7F};  // 3 bytes → SmallBuffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.size(), 3);
    auto view = entry.get();
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[0]), 0x00);
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[1]), 0xFF);
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[2]), 0x7F);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(smallBuffer_arrayChar)
{
    std::array<char, 5> value = {'h', 'e', 'l', 'l', 'o'};  // 5 bytes → SmallBuffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), std::string_view("hello"));
    BOOST_CHECK_EQUAL(entry.size(), 5);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(smallBuffer_stringLiteral)
{
    // string literal → set(T&&) convertible to string_view → setImplCopy → SmallBuffer
    Entry entry;
    entry.set("hello literal");
    BOOST_CHECK_EQUAL(entry.get(), "hello literal");
    BOOST_CHECK_EQUAL(entry.size(), 13);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(smallBuffer_constCharPtr)
{
    const char* value = "const char ptr";
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), value);
    BOOST_CHECK_EQUAL(entry.data(), entry.get().data());
    BOOST_TEST(entryTestHolder(entry).has_value());
}

// Fixed32Buffer: data exactly 32 bytes, no size field needed
BOOST_AUTO_TEST_CASE(fixed32_stdString)
{
    std::string value(32, 'y');  // exactly 32 bytes → Fixed32Buffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), value);
    BOOST_CHECK_EQUAL(entry.size(), 32);
    BOOST_TEST(entryTestHolder(entry).has_value());
    value[0] = 'Y';
    BOOST_CHECK_EQUAL(entry.get()[0], 'y');  // Fixed32Buffer also copies inline
}

BOOST_AUTO_TEST_CASE(fixed32_vectorChar)
{
    std::vector<char> value(32, 'z');  // exactly 32 bytes → Fixed32Buffer
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.size(), 32);
    auto view = entry.get();
    BOOST_CHECK_EQUAL(view, std::string_view(value.data(), value.size()));
    BOOST_TEST(entryTestHolder(entry).has_value());
}

// BufferModel<T>: data > 32 bytes, container stored inline in proxy.
// Unique behavior: the container is moved into the proxy, so the original
// becomes empty / unspecified after set().
BOOST_AUTO_TEST_CASE(bufferModel_largeStdString)
{
    std::string value(100, 'L');  // >32 bytes → BufferModel<std::string>
    Entry entry;
    entry.set(std::move(value));
    BOOST_CHECK_EQUAL(entry.get(), std::string(100, 'L'));
    BOOST_CHECK_EQUAL(entry.size(), 100);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(bufferModel_largeVectorChar)
{
    std::vector<char> value(64, 'V');  // >32 bytes → BufferModel<std::vector<char>>
    Entry entry;
    entry.set(std::move(value));
    BOOST_CHECK_EQUAL(entry.size(), 64);
    auto view = entry.get();
    BOOST_CHECK_EQUAL(view, std::string_view(std::string(64, 'V')));
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(bufferModel_largeVectorUnsignedChar)
{
    std::vector<unsigned char> value(64, 0xAB);  // >32 bytes → BufferModel<std::vector<unsigned
                                                 // char>>
    Entry entry;
    entry.set(std::move(value));
    BOOST_CHECK_EQUAL(entry.size(), 64);
    auto view = entry.get();
    BOOST_CHECK_EQUAL(view.size(), 64);
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[0]), 0xAB);
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[63]), 0xAB);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

// SharedBufferModel: shared_ptr-wrapped, shares ownership.
// Unique behavior: copy of Entry shares the same underlying buffer;
// modifying the shared string affects both copies.
BOOST_AUTO_TEST_CASE(sharedBuffer_stdString_small)
{
    auto value = std::make_shared<std::string>("shared small");
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), *value);
    BOOST_CHECK_EQUAL(entry.size(), value->size());
    BOOST_TEST(entryTestHolder(entry).has_value());

    // Copy shares ownership — modifying the shared string affects the copy too
    Entry copy(entry);
    *value = "modified!";
    BOOST_CHECK_EQUAL(copy.get(), "modified!");
    BOOST_CHECK_EQUAL(entry.get(), "modified!");
}

BOOST_AUTO_TEST_CASE(sharedBuffer_stdString_large)
{
    auto value = std::make_shared<std::string>(std::string(100, 'S'));
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.get(), *value);
    BOOST_CHECK_EQUAL(entry.size(), 100);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(sharedBuffer_vectorChar)
{
    auto value =
        std::make_shared<std::vector<char>>(std::vector<char>{'s', 'h', 'a', 'r', 'e', 'd'});
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.size(), 6);
    BOOST_CHECK_EQUAL(entry.get(), std::string_view("shared"));
    BOOST_TEST(entryTestHolder(entry).has_value());

    // Shared ownership verification
    Entry copy(entry);
    (*value)[0] = 'S';
    BOOST_CHECK_EQUAL(copy.get()[0], 'S');
}

BOOST_AUTO_TEST_CASE(sharedBuffer_vectorUnsignedChar)
{
    auto value = std::make_shared<std::vector<unsigned char>>(
        std::vector<unsigned char>{0xAA, 0xBB, 0xCC, 0xDD});
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.size(), 4);
    auto view = entry.get();
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[0]), 0xAA);
    BOOST_CHECK_EQUAL(static_cast<unsigned char>(view[3]), 0xDD);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(sharedBuffer_arrayChar)
{
    using Arr6 = std::array<char, 6>;
    auto value = std::make_shared<Arr6>(Arr6{'a', 'r', 'r', 'a', 'y', '!'});
    Entry entry;
    entry.set(value);
    BOOST_CHECK_EQUAL(entry.size(), 6);
    BOOST_CHECK_EQUAL(entry.get(), std::string_view("array!"));
    BOOST_TEST(entryTestHolder(entry).has_value());
}

// set() with various types
BOOST_AUTO_TEST_CASE(set_coverage)
{
    Entry e1;
    e1.set(std::string("via set"));
    BOOST_CHECK_EQUAL(e1.get(), "via set");
    BOOST_TEST(entryTestHolder(e1).has_value());

    Entry e2;
    std::vector<char> vec = {'i', 'm', 'p', 'o', 'r', 't'};
    e2.set(std::move(vec));
    BOOST_CHECK_EQUAL(e2.get(), std::string_view("import"));
    BOOST_TEST(entryTestHolder(e2).has_value());
}

// Copy/move across different buffer models
BOOST_AUTO_TEST_CASE(bufferModel_copyAndMove)
{
    // SmallBuffer copy/move
    {
        Entry e1;
        e1.set(std::string("small"));
        Entry e2(e1);
        Entry e3(std::move(e1));
        BOOST_CHECK_EQUAL(e2.get(), "small");
        BOOST_CHECK_EQUAL(e3.get(), "small");
        BOOST_TEST(entryTestHolder(e2).has_value());
        BOOST_TEST(entryTestHolder(e3).has_value());
    }
    // BufferModel copy/move
    {
        Entry e1;
        e1.set(std::string(100, 'B'));
        Entry e2(e1);
        Entry e3(std::move(e1));
        BOOST_CHECK_EQUAL(e2.get(), std::string(100, 'B'));
        BOOST_CHECK_EQUAL(e3.get(), std::string(100, 'B'));
        BOOST_TEST(entryTestHolder(e2).has_value());
        BOOST_TEST(entryTestHolder(e3).has_value());
    }
    // SharedBufferModel copy/move — shares ownership
    {
        auto sp = std::make_shared<std::string>("shared copy");
        Entry e1;
        e1.set(sp);
        Entry e2(e1);
        Entry e3(std::move(e1));
        BOOST_CHECK_EQUAL(e2.get(), "shared copy");
        BOOST_CHECK_EQUAL(e3.get(), "shared copy");
        *sp = "changed via shared_ptr";
        BOOST_CHECK_EQUAL(e2.get(), "changed via shared_ptr");
        BOOST_CHECK_EQUAL(e3.get(), "changed via shared_ptr");
        BOOST_TEST(entryTestHolder(e2).has_value());
        BOOST_TEST(entryTestHolder(e3).has_value());
    }
}

// ── View deep-copy verification ───────────────────────────────────
// Non-owning views (string_view, span, bytesConstRef) must be
// deep-copied into the Entry: after the original data container is
// destroyed, the Entry must still hold the correct value.

BOOST_AUTO_TEST_CASE(viewDeepCopy_stringView)
{
    std::string original = "string_view deep copy - entry owns its data after source destroyed";
    std::string_view sv(original);

    Entry entry;
    entry.set(sv);

    BOOST_CHECK_EQUAL(entry.get(), sv);
    BOOST_CHECK_EQUAL(entry.size(), static_cast<int32_t>(sv.size()));

    // Destroy the original — Entry must still hold the value
    original.clear();
    original.shrink_to_fit();

    BOOST_CHECK_EQUAL(
        entry.get(), "string_view deep copy - entry owns its data after source destroyed");
    BOOST_CHECK_EQUAL(entry.size(), 66);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(viewDeepCopy_spanConstChar)
{
    std::vector<char> original = {'s', 'p', 'a', 'n', '_', 'd', 'e', 'e', 'p', '_', 'c', 'o', 'p',
        'y', '_', 't', 'e', 's', 't'};
    std::span<const char> sp(original.data(), original.size());

    Entry entry;
    entry.set(sp);

    BOOST_CHECK_EQUAL(entry.get(), std::string_view("span_deep_copy_test"));
    BOOST_CHECK_EQUAL(entry.size(), static_cast<int32_t>(sp.size()));

    original.clear();
    original.shrink_to_fit();

    BOOST_CHECK_EQUAL(entry.get(), "span_deep_copy_test");
    BOOST_CHECK_EQUAL(entry.size(), 19);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(viewDeepCopy_spanChar)
{
    std::string original = "span<char> deep copy test";
    std::span sp(original.data(), original.size());  // span<char>, non-const

    Entry entry;
    entry.set(sp);

    BOOST_CHECK_EQUAL(entry.get(), std::string_view("span<char> deep copy test"));

    original.clear();
    original.shrink_to_fit();

    BOOST_CHECK_EQUAL(entry.get(), "span<char> deep copy test");
    BOOST_CHECK_EQUAL(entry.size(), 25);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(viewDeepCopy_bytesConstRef)
{
    std::string original = "bytesConstRef deep copy test";
    auto ref = bytesConstRef(reinterpret_cast<const bcos::byte*>(original.data()), original.size());

    Entry entry;
    entry.set(ref);

    BOOST_CHECK_EQUAL(entry.get(), std::string_view(original));

    original.clear();
    original.shrink_to_fit();

    BOOST_CHECK_EQUAL(entry.get(), "bytesConstRef deep copy test");
    BOOST_CHECK_EQUAL(entry.size(), 28);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

BOOST_AUTO_TEST_CASE(viewDeepCopy_constructFromView)
{
    // Entry(auto) constructor must also deep-copy views
    std::string original = "constructed from string_view";
    std::string_view sv(original);

    Entry entry(sv);

    original.clear();
    original.shrink_to_fit();

    BOOST_CHECK_EQUAL(entry.get(), "constructed from string_view");
    BOOST_CHECK_EQUAL(entry.size(), 28);
    BOOST_TEST(entryTestHolder(entry).has_value());
}

// ─── Encodable test types for typed Entry tests ────────────────────
// Must be ≤ 32 bytes to satisfy proxy's restrict_layout<SBO=32, align=8>.

struct TestValueA
{
    int32_t id = 0;
    int32_t nameLen = 0;
    std::array<char, 24> nameBuf{};

    TestValueA() = default;
    TestValueA(int32_t i, std::string_view n) : id(i)
    {
        nameLen = static_cast<int32_t>(std::min(n.size(), nameBuf.size()));
        std::memcpy(nameBuf.data(), n.data(), nameLen);
    }
    TestValueA(bytesConstRef data)
    {
        if (data.size() < 8)
            return;
        std::memcpy(&id, data.data(), 4);
        std::memcpy(&nameLen, data.data() + 4, 4);
        auto actualLen = std::min(static_cast<size_t>(nameLen), nameBuf.size());
        if (data.size() >= 8 + actualLen)
            std::memcpy(nameBuf.data(), data.data() + 8, actualLen);
    }
    void encode(auto&& sink) const
    {
        uint8_t buf[32];
        std::memcpy(buf, &id, 4);
        std::memcpy(buf + 4, &nameLen, 4);
        std::memcpy(buf + 8, nameBuf.data(), nameLen);
        sink(bytesConstRef(buf, 8 + static_cast<size_t>(nameLen)));
    }
    std::string nameStr() const { return std::string(nameBuf.data(), nameLen); }
    bool operator==(const TestValueA& o) const
    {
        return id == o.id && nameLen == o.nameLen &&
               std::memcmp(nameBuf.data(), o.nameBuf.data(), nameLen) == 0;
    }
};
static_assert(sizeof(TestValueA) <= 32);

struct TestValueB
{
    int64_t value = 0;

    TestValueB() = default;
    explicit TestValueB(int64_t v) : value(v) {}
    TestValueB(bytesConstRef data)
    {
        if (data.size() >= 8)
            std::memcpy(&value, data.data(), 8);
    }
    void encode(auto&& sink) const
    {
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(&value), 8));
    }
    bool operator==(const TestValueB& o) const { return value == o.value; }
};
static_assert(sizeof(TestValueB) <= 32);

// ─── tag_invoke overloads for Encodable test types ────────────────

template <typename Sink>
void tag_invoke(bcos::storage::encode_t, const TestValueA& v, Sink&& sink)
{
    v.encode(std::forward<Sink>(sink));
}
TestValueA tag_invoke(
    bcos::storage::decode_t, std::type_identity<TestValueA>, bytesConstRef data)
{
    return TestValueA{data};
}

template <typename Sink>
void tag_invoke(bcos::storage::encode_t, const TestValueB& v, Sink&& sink)
{
    v.encode(std::forward<Sink>(sink));
}
TestValueB tag_invoke(
    bcos::storage::decode_t, std::type_identity<TestValueB>, bytesConstRef data)
{
    return TestValueB{data};
}

// ─── Typed Entry tests ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(setTypedGetTypedSameType)
{
    Entry entry;
    TestValueA original{42, "hello"};
    entry.setTyped(original);

    // getTyped should return a valid pointer with matching data
    auto* ptr = entry.getTyped<TestValueA>();
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_CHECK_EQUAL(ptr->id, 42);
    BOOST_CHECK_EQUAL(ptr->nameStr(), "hello");

    // holdsType should be correct
    BOOST_TEST(entry.holdsType<TestValueA>());
    BOOST_TEST(!entry.holdsType<TestValueB>());
}

BOOST_AUTO_TEST_CASE(setTypedGetTypedDifferentType)
{
    Entry entry;
    entry.setTyped(TestValueA{7, "test"});

    // getTyped<TestValueB> on an Entry holding TestValueA should fail
    auto* ptr = entry.getTyped<TestValueB>();
    BOOST_TEST(ptr == nullptr);

    // But getTyped<TestValueA> still works
    auto* ptrA = entry.getTyped<TestValueA>();
    BOOST_REQUIRE(ptrA != nullptr);
    BOOST_CHECK_EQUAL(ptrA->id, 7);

    // holdsType should distinguish
    BOOST_TEST(entry.holdsType<TestValueA>());
    BOOST_TEST(!entry.holdsType<TestValueB>());
}

BOOST_AUTO_TEST_CASE(lazyDecodeFromByteMode)
{
    // Start with a byte-mode Entry (simulating data from RocksDB)
    Entry entry;
    TestValueA original{99, "lazy"};
    std::string encoded;
    encode(original, [&encoded](bytesConstRef d) {
        encoded.append(reinterpret_cast<const char*>(d.data()), d.size());
    });
    entry.set(encoded);

    // Entry is in byte-mode; holdsType should be false for any type
    BOOST_TEST(!entry.holdsType<TestValueA>());
    BOOST_TEST(!entry.holdsType<TestValueB>());

    // First getTyped triggers lazy decode
    auto* ptr = entry.getTyped<TestValueA>();
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_CHECK_EQUAL(ptr->id, 99);
    BOOST_CHECK_EQUAL(ptr->nameStr(), "lazy");

    // After lazy decode, entry holds the typed model
    BOOST_TEST(entry.holdsType<TestValueA>());
    BOOST_TEST(!entry.holdsType<TestValueB>());

    // Second getTyped is O(1) — no re-decode
    auto* ptr2 = entry.getTyped<TestValueA>();
    BOOST_REQUIRE(ptr2 != nullptr);
    BOOST_CHECK(ptr == ptr2);  // Same pointer, same TypedHolderModel instance
}

BOOST_AUTO_TEST_CASE(encodeToBytesAfterSetTyped)
{
    Entry entry;
    entry.setTyped(TestValueA{55, "world"});

    // Encode to bytes — should match what TestValueA::encode produces
    std::string bytes;
    entry.encode([&bytes](bytesConstRef data) {
        bytes.append(reinterpret_cast<const char*>(data.data()), data.size());
    });
    TestValueA expected{55, "world"};
    std::string expectedBytes;
    encode(expected, [&expectedBytes](bytesConstRef d) {
        expectedBytes.append(reinterpret_cast<const char*>(d.data()), d.size());
    });
    BOOST_CHECK_EQUAL(bytes, expectedBytes);
}

BOOST_AUTO_TEST_CASE(setTypedMoveSemantics)
{
    Entry entry;
    TestValueA original{1, "move-test-name"};
    auto originalName = original.nameStr();

    entry.setTyped(std::move(original));

    auto* ptr = entry.getTyped<TestValueA>();
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_CHECK_EQUAL(ptr->id, 1);
    BOOST_CHECK_EQUAL(ptr->nameStr(), originalName);
}

BOOST_AUTO_TEST_CASE(emptyEntryGetTyped)
{
    Entry entry;
    auto* ptr = entry.getTyped<TestValueA>();
    BOOST_TEST(ptr == nullptr);
    BOOST_TEST(!entry.holdsType<TestValueA>());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
