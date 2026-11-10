/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/KeyPageStorageDetector.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::storage;

namespace bcos::test
{
namespace
{
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

    std::shared_ptr<StateStorage> backend = std::make_shared<StateStorage>(nullptr, false);
};
}  // namespace

BOOST_AUTO_TEST_SUITE(KeyPageStorageDetectorTest)

BOOST_FIXTURE_TEST_CASE(emptyAndNormalStorageHaveNoMetadata, KeyPageStorageDetectorFixture)
{
    BOOST_CHECK(!hasKeyPageMetadata(*backend));

    setRow(*backend, StorageInterface::SYS_TABLES, "t_plain", "value");
    setRow(*backend, "t_plain", "name", "payload");
    setRow(*backend, "t_plain", "", "ordinary empty-key entry");
    BOOST_CHECK(!hasKeyPageMetadata(*backend));
}

BOOST_FIXTURE_TEST_CASE(detectsCommittedPageMetadata, KeyPageStorageDetectorFixture)
{
    writePageRow("t_page", "name", "payload");
    BOOST_CHECK(hasKeyPageMetadata(*backend));
}

BOOST_FIXTURE_TEST_CASE(ignoredSystemTablesAreNotPageEvidence, KeyPageStorageDetectorFixture)
{
    writePageRow(std::string(ledger::SYS_CONFIG), "name", "payload");
    BOOST_CHECK(!hasKeyPageMetadata(*backend));
}

BOOST_FIXTURE_TEST_CASE(inconsistentPageMetadataFails, KeyPageStorageDetectorFixture)
{
    writePageRow("t_page", "name", "payload");
    setRow(*backend, "t_page", "name", "not-a-page");
    BOOST_CHECK_THROW(hasKeyPageMetadata(*backend), bcos::Error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
