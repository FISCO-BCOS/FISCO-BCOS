/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage/Serialize.h>
#include <bcos-table/src/StateStorage.h>
#include <bcos-tool/BfsFileFactory.h>
#include <boost/serialization/vector.hpp>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::storage;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BfsFileFactoryTest)

namespace
{
Table makeTable(const StateStorage::Ptr& storage, const std::string& name)
{
    auto info = std::make_shared<storage::TableInfo>(
        name, std::vector<std::string>{std::string(FS_KEY_TYPE)});
    return Table(storage.get(), std::move(info));
}

// buildLink/buildAuth store raw single-value rows (entry.set(std::string)), so read them back
// with Entry::get() rather than decoding them as a serialized field vector. Entry's getField /
// getObject accessors went away with the proxy-based buffer facade (#5247).
std::string firstField(const std::optional<Entry>& entry)
{
    return std::string(entry->get());
}
}  // namespace

// buildDirEntry maps each FileType and each string-like input to the stored
// type token via the std::visit dispatch.
BOOST_AUTO_TEST_CASE(buildDirEntryTypeDispatch)
{
    auto typeOf = [](std::variant<FileType, std::string, std::string_view> ft) {
        Entry entry;
        BfsFileFactory::buildDirEntry(entry, std::move(ft));
        // buildDirEntry stores serialize::encode<std::vector<std::string>>; decode it back the
        // same way production does (see BFSPrecompiled.cpp).
        auto fields = bcos::storage::serialize::decode<std::vector<std::string>>(entry.get());
        return fields.at(0);
    };

    BOOST_CHECK_EQUAL(typeOf(FileType::DIRECTOR), std::string(FS_TYPE_DIR));
    BOOST_CHECK_EQUAL(typeOf(FileType::LINK), std::string(FS_TYPE_LINK));
    BOOST_CHECK_EQUAL(typeOf(FileType::CONTRACT), std::string(FS_TYPE_CONTRACT));
    // AUTH is not DIRECTOR/LINK, so it falls through to the contract token.
    BOOST_CHECK_EQUAL(typeOf(FileType::AUTH), std::string(FS_TYPE_CONTRACT));
    // A raw string / string_view is stored verbatim.
    BOOST_CHECK_EQUAL(typeOf(std::string_view("custom_view")), "custom_view");
    BOOST_CHECK_EQUAL(typeOf(std::string("custom_str")), "custom_str");
}

// buildLink at V3.0 writes the abi and name rows even when empty; buildDir and
// buildContract are stubs returning false.
BOOST_AUTO_TEST_CASE(buildLinkV30WritesAbiAndName)
{
    auto storage = std::make_shared<StateStorage>(nullptr, false);
    auto table = makeTable(storage, "/apps/link0");

    BOOST_CHECK(BfsFileFactory::buildLink(table, "0xabc", "", /*blockVersion*/ 0x03000000, ""));

    BOOST_CHECK_EQUAL(firstField(table.getRow(FS_KEY_TYPE)), std::string(FS_TYPE_LINK));
    BOOST_CHECK_EQUAL(firstField(table.getRow(FS_LINK_ADDRESS)), "0xabc");
    // empty abi/name still persisted at V3.0
    BOOST_CHECK(table.getRow(FS_LINK_ABI).has_value());
    BOOST_CHECK(table.getRow(FS_KEY_NAME).has_value());
}

// Above V3.0, empty abi/name are skipped; populated ones are written.
BOOST_AUTO_TEST_CASE(buildLinkNewerVersionSkipsEmpty)
{
    auto storage = std::make_shared<StateStorage>(nullptr, false);
    auto table = makeTable(storage, "/apps/link1");

    auto version = static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION);
    BOOST_CHECK(BfsFileFactory::buildLink(table, "0xdef", "", version, ""));
    BOOST_CHECK(!table.getRow(FS_LINK_ABI).has_value());  // empty abi skipped
    BOOST_CHECK(!table.getRow(FS_KEY_NAME).has_value());  // empty name skipped

    auto table2 = makeTable(storage, "/apps/link2");
    BOOST_CHECK(BfsFileFactory::buildLink(table2, "0x123", "myabi", version, "myname"));
    BOOST_CHECK_EQUAL(firstField(table2.getRow(FS_LINK_ABI)), "myabi");
    BOOST_CHECK_EQUAL(firstField(table2.getRow(FS_KEY_NAME)), "myname");
}

// buildAuth writes the admin row plus the four fixed auth rows and returns true.
BOOST_AUTO_TEST_CASE(buildAuthWritesAdminAndFixedRows)
{
    auto storage = std::make_shared<StateStorage>(nullptr, false);
    auto table = makeTable(storage, "/apps/auth0");

    BOOST_CHECK(BfsFileFactory::buildAuth(table, "0xadmin"));
}

// The two stubs are documented to return false.
BOOST_AUTO_TEST_CASE(buildDirAndContractStubs)
{
    auto storage = std::make_shared<StateStorage>(nullptr, false);
    auto table = makeTable(storage, "/apps/dir0");
    BOOST_CHECK(!BfsFileFactory::buildDir(table));
    BOOST_CHECK(!BfsFileFactory::buildContract(table));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
