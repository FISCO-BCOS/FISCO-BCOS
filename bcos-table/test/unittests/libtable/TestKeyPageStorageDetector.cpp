/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/KeyPageStorageDetector.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Serialize.h>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bcos;
using namespace bcos::storage;

namespace bcos::test
{
namespace
{
struct RawTableMeta
{
    std::vector<KeyPageStorage::PageInfo> pages;

    template <class Archive>
    void save(Archive& archive, const unsigned int) const
    {
        archive & pages;
    }
    template <class Archive>
    void load(Archive& archive, const unsigned int)
    {
        archive & pages;
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
};

struct RawPage
{
    uint32_t validCount = 0;
    std::vector<std::pair<std::string, std::string>> entries;

    template <class Archive>
    void save(Archive& archive, const unsigned int) const
    {
        archive & validCount;
        for (auto const& [key, value] : entries)
        {
            archive & key;
            uint32_t const size = value.size();
            archive & size;
            archive.save_binary(value.data(), value.size());
        }
    }
    template <class Archive>
    void load(Archive&, const unsigned int)
    {}
    BOOST_SERIALIZATION_SPLIT_MEMBER()
};

struct KeyPageStorageDetectorFixture
{
    KeyPageStorageDetectorFixture() { backend->setEnableTraverse(true); }

    void setRow(StorageInterface& storage, std::string const& table, std::string const& key,
        std::string const& value)
    {
        Entry entry;
        entry.set(value);
        std::promise<Error::UniquePtr> promise;
        storage.asyncSetRow(table, key, std::move(entry),
            [&promise](Error::UniquePtr error) { promise.set_value(std::move(error)); });
        auto error = promise.get_future().get();
        BOOST_REQUIRE(!error);
    }

    void writePageRow(std::string const& table, std::string const& key, std::string const& value)
    {
        auto pageStorage = std::make_shared<KeyPageStorage>(backend, false);
        setRow(*pageStorage, StorageInterface::SYS_TABLES, table, "value");
        setRow(*pageStorage, table, key, value);
        backend->merge(true, *pageStorage);
    }

    void writeMetadata(std::string const& table, std::string const& pageKey, uint16_t count,
        std::string const& endKey, bool writePage)
    {
        KeyPageStorage::Page page;
        Entry entry;
        entry.set("payload");
        page.setEntry(endKey, std::move(entry));
        RawTableMeta meta;
        meta.pages.emplace_back(pageKey, count, static_cast<uint16_t>(page.size()), nullptr);
        setRow(*backend, StorageInterface::SYS_TABLES, table, "value");
        setRow(*backend, table, "", serialize::encode(meta));
        if (writePage)
        {
            setRow(*backend, table, pageKey, serialize::encode(page));
        }
    }

    void writeDuplicateEntryPage(std::string const& table)
    {
        KeyPageStorage::Page page;
        Entry entry;
        entry.set("payload");
        page.setEntry("name", std::move(entry));
        RawTableMeta meta;
        meta.pages.emplace_back("name", 2, static_cast<uint16_t>(page.size()), nullptr);
        RawPage duplicate;
        duplicate.validCount = 2;
        duplicate.entries = {{"name", "payload"}, {"name", "payload"}};

        setRow(*backend, StorageInterface::SYS_TABLES, table, "value");
        setRow(*backend, table, "", serialize::encode(meta));
        setRow(*backend, table, "name", serialize::encode(duplicate));
    }

    std::shared_ptr<StateStorage> backend = std::make_shared<StateStorage>(nullptr, false);
};
}  // namespace

BOOST_AUTO_TEST_SUITE(KeyPageStorageDetectorTest)

BOOST_FIXTURE_TEST_CASE(emptyAndNormalStorageHaveNoMetadata, KeyPageStorageDetectorFixture)
{
    std::size_t examined = 1;
    BOOST_CHECK(!hasKeyPageMetadata(*backend, &examined));
    BOOST_CHECK_EQUAL(examined, 0);

    setRow(*backend, StorageInterface::SYS_TABLES, "t_plain", "value");
    setRow(*backend, "t_plain", "name", "payload");
    setRow(*backend, "t_plain", "", "ordinary empty-key entry");
    BOOST_CHECK(!hasKeyPageMetadata(*backend, &examined));
    BOOST_CHECK_EQUAL(examined, 1);
}

BOOST_FIXTURE_TEST_CASE(detectsCommittedPageMetadata, KeyPageStorageDetectorFixture)
{
    writePageRow("t_page", "name", "payload");
    std::size_t examined = 0;
    BOOST_CHECK(hasKeyPageMetadata(*backend, &examined));
    BOOST_CHECK_EQUAL(examined, 1);
}

BOOST_FIXTURE_TEST_CASE(systemTablePageMetadataIsAlsoDetected, KeyPageStorageDetectorFixture)
{
    writePageRow(std::string(ledger::SYS_CONFIG), "name", "payload");
    BOOST_CHECK(hasKeyPageMetadata(*backend));
}

BOOST_FIXTURE_TEST_CASE(inconsistentPageMetadataFails, KeyPageStorageDetectorFixture)
{
    writePageRow("t_page", "name", "payload");
    setRow(*backend, "t_page", "name", "not-a-page");
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_FIXTURE_TEST_CASE(emptyPageKeyFails, KeyPageStorageDetectorFixture)
{
    writeMetadata("t_page", "", 1, "name", false);
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_FIXTURE_TEST_CASE(zeroPageCountFails, KeyPageStorageDetectorFixture)
{
    writeMetadata("t_page", "name", 0, "name", false);
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_FIXTURE_TEST_CASE(missingPageFailsWithRemedy, KeyPageStorageDetectorFixture)
{
    writeMetadata("t_page", "name", 1, "name", false);
    try
    {
        static_cast<void>(hasKeyPageMetadata(*backend));
        BOOST_FAIL("missing page must fail detection");
    }
    catch (bcos::Error const& error)
    {
        BOOST_CHECK(
            std::string(error.errorMessage()).find("storage.key_page_size") != std::string::npos);
        BOOST_CHECK(std::string(error.errorMessage()).find("0x") != std::string::npos);
    }
}

BOOST_FIXTURE_TEST_CASE(pageKeyMismatchFails, KeyPageStorageDetectorFixture)
{
    writeMetadata("t_page", "name", 1, "other", true);
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_FIXTURE_TEST_CASE(pageInfoCountMismatchFails, KeyPageStorageDetectorFixture)
{
    writeMetadata("t_page", "name", 2, "name", true);
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_FIXTURE_TEST_CASE(duplicatePageEntriesFail, KeyPageStorageDetectorFixture)
{
    writeDuplicateEntryPage("t_page");
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
