/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "libinitializer/KeyPageStorageResolver.h"
#include <bcos-framework/storage/Serialize.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <bcos-tool/Exceptions.h>
#include <rocksdb/db.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>

using namespace bcos;
using namespace bcos::initializer;

namespace bcos::test
{
namespace
{
std::atomic_uint64_t fixtureCounter{0};

struct ConfigProbe : tool::NodeConfig
{
    using tool::NodeConfig::loadStorageConfig;
};

void loadStorage(ConfigProbe& config, std::string const& ini)
{
    boost::property_tree::ptree tree;
    std::stringstream stream(ini);
    boost::property_tree::read_ini(stream, tree);
    config.loadStorageConfig(tree);
}

struct ResolverFixture
{
    ResolverFixture()
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        rocksdb::Options options;
        options.create_if_missing = true;
        rocksdb::DB* raw = nullptr;
        auto status = rocksdb::DB::Open(options, dir.string(), &raw);
        BOOST_REQUIRE(status.ok());
        db.reset(raw);
        std::unique_ptr<rocksdb::DB, std::function<void(rocksdb::DB*)>> handle(
            db.get(), [](rocksdb::DB*) {});
        storage = std::make_shared<storage::RocksDBStorage>(std::move(handle), nullptr);
    }

    ~ResolverFixture()
    {
        storage.reset();
        db.reset();
        std::filesystem::remove_all(dir);
    }

    void put(std::string const& key, std::string const& value)
    {
        auto status = db->Put(rocksdb::WriteOptions{}, key, value);
        BOOST_REQUIRE(status.ok());
    }

    void writePage(std::string const& table)
    {
        storage::KeyPageStorage::Page page;
        storage::Entry entry;
        entry.set("payload");
        page.setEntry("name", std::move(entry));
        storage::KeyPageStorage::TableMeta meta;
        meta.getAllPageInfoNoLock().emplace_back(
            "name", 1, static_cast<uint16_t>(page.size()), nullptr);
        put("s_tables:" + table, "value");
        put(table + ":", storage::serialize::encode(meta));
        put(table + ":name", storage::serialize::encode(page));
    }

    KeyPageSizeResolution resolve(ConfigProbe& config)
    {
        return resolveKeyPageSize(config, *storage, *db);
    }

    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("keypage_storage_resolver_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(fixtureCounter++));
    std::unique_ptr<rocksdb::DB> db;
    std::shared_ptr<storage::RocksDBStorage> storage;
};
}  // namespace

BOOST_AUTO_TEST_SUITE(KeyPageStorageResolverTest)

BOOST_FIXTURE_TEST_CASE(emptyDatabaseAlwaysUsesZero, ResolverFixture)
{
    ConfigProbe positive;
    loadStorage(positive, "[storage]\nkey_page_size=10240\n");
    BOOST_CHECK_THROW(resolve(positive), tool::InvalidConfig);

    ConfigProbe missing;
    loadStorage(missing, "");
    auto initial = resolve(missing);
    BOOST_CHECK_EQUAL(initial.value, 0);
    BOOST_CHECK_EQUAL(initial.source, "empty");
}

BOOST_FIXTURE_TEST_CASE(legacyPageUsesConfiguredOrHistoricalDefaultSize, ResolverFixture)
{
    writePage("t_page");
    ConfigProbe custom;
    loadStorage(custom, "[storage]\nkey_page_size=8192\n");
    auto configured = resolve(custom);
    BOOST_CHECK_EQUAL(configured.value, 8192);
    BOOST_CHECK_EQUAL(configured.source, "config-verified");
    BOOST_CHECK_EQUAL(configured.tablesExamined, 1);

    ConfigProbe restarted;
    loadStorage(restarted, "");
    auto detected = resolve(restarted);
    BOOST_CHECK_EQUAL(detected.value, 10240);
    BOOST_CHECK_EQUAL(detected.source, "detection");
    BOOST_CHECK_EQUAL(detected.tablesExamined, 1);

    ConfigProbe restartedAgain;
    loadStorage(restartedAgain, "");
    auto detectedAgain = resolve(restartedAgain);
    BOOST_CHECK_EQUAL(detectedAgain.value, 10240);
    BOOST_CHECK_EQUAL(detectedAgain.source, "detection");
    BOOST_CHECK_EQUAL(detectedAgain.tablesExamined, 1);
}

BOOST_FIXTURE_TEST_CASE(plainExistingDataCannotEnablePageLater, ResolverFixture)
{
    put("s_tables:t_plain", "value");
    put("t_plain:name", "payload");
    ConfigProbe positive;
    loadStorage(positive, "[storage]\nkey_page_size=10240\n");
    BOOST_CHECK_THROW(resolve(positive), tool::InvalidConfig);
}

BOOST_FIXTURE_TEST_CASE(missingConfigOnPlainDataUsesZero, ResolverFixture)
{
    put("s_tables:t_plain", "value");
    put("t_plain:name", "payload");
    ConfigProbe first;
    loadStorage(first, "");
    auto initial = resolve(first);
    BOOST_CHECK_EQUAL(initial.value, 0);
    BOOST_CHECK_EQUAL(initial.source, "plain");

    ConfigProbe restarted;
    loadStorage(restarted, "");
    auto repeated = resolve(restarted);
    BOOST_CHECK_EQUAL(repeated.value, 0);
    BOOST_CHECK_EQUAL(repeated.source, "plain");
    BOOST_CHECK_EQUAL(repeated.tablesExamined, 1);

    ConfigProbe explicitZero;
    loadStorage(explicitZero, "[storage]\nkey_page_size=0\n");
    auto skipped = resolve(explicitZero);
    BOOST_CHECK_EQUAL(skipped.source, "config-skipped");
    BOOST_CHECK_EQUAL(skipped.tablesExamined, 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
