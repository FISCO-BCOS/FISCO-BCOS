/**
 * @file EestFailuresJsonTest.cpp
 * @brief Unit tests for EestFailuresJson (writeFailuresJson + firstMismatchCategory).
 *
 * Standalone Boost.Test target (eest-json-tests): links only jsoncpp_static and
 * Boost::unit_test_framework, so it must not include EESTRunner.cpp.
 */
#define BOOST_TEST_MODULE EestFailuresJsonTest
#include "EestFailuresJson.h"

#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::test
{

BOOST_AUTO_TEST_CASE(writeFailuresJsonEmitsSevenFieldRecords)
{
    std::vector<FailureDetail> details;
    details.push_back({"t/balance_mismatch", "Cancun", "state mismatch", "balance", 0, 1, 2});
    details.push_back({"t/storage_mismatch", "Prague", "state mismatch", "storage", 3, 4, 5});
    details.push_back({"t/expected_failure", "Shanghai", "expected exception 'X'",
        "expected_exception", 6, 7, 8});
    details.push_back({"t/unexpected", "Paris", "unexpected failure", "unexpected", 9, 10, 11});

    auto jsonStr = writeFailuresJson(details);

    Json::Value root;
    Json::CharReaderBuilder reader;
    std::istringstream iss(jsonStr);
    std::string errs;
    BOOST_REQUIRE_MESSAGE(
        Json::parseFromStream(reader, iss, &root, &errs), "invalid JSON: " + errs);
    BOOST_REQUIRE(root.isArray());
    BOOST_REQUIRE_EQUAL(root.size(), 4u);

    // Every element must carry exactly the 7 fields.
    for (auto const& obj : root)
    {
        BOOST_REQUIRE(obj.isObject());
        BOOST_REQUIRE_EQUAL(obj.size(), 7u);
        BOOST_REQUIRE(obj.isMember("testName"));
        BOOST_REQUIRE(obj.isMember("forkName"));
        BOOST_REQUIRE(obj.isMember("reason"));
        BOOST_REQUIRE(obj.isMember("category"));
        BOOST_REQUIRE(obj.isMember("dataIndex"));
        BOOST_REQUIRE(obj.isMember("gasIndex"));
        BOOST_REQUIRE(obj.isMember("valueIndex"));
    }

    BOOST_CHECK_EQUAL(root[0]["testName"].asString(), "t/balance_mismatch");
    BOOST_CHECK_EQUAL(root[0]["forkName"].asString(), "Cancun");
    BOOST_CHECK_EQUAL(root[0]["reason"].asString(), "state mismatch");
    BOOST_CHECK_EQUAL(root[0]["category"].asString(), "balance");
    BOOST_CHECK_EQUAL(root[0]["dataIndex"].asInt(), 0);
    BOOST_CHECK_EQUAL(root[0]["gasIndex"].asInt(), 1);
    BOOST_CHECK_EQUAL(root[0]["valueIndex"].asInt(), 2);

    BOOST_CHECK_EQUAL(root[1]["category"].asString(), "storage");
    BOOST_CHECK_EQUAL(root[1]["dataIndex"].asInt(), 3);
    BOOST_CHECK_EQUAL(root[1]["gasIndex"].asInt(), 4);
    BOOST_CHECK_EQUAL(root[1]["valueIndex"].asInt(), 5);

    BOOST_CHECK_EQUAL(root[2]["category"].asString(), "expected_exception");
    BOOST_CHECK_EQUAL(root[2]["reason"].asString(), "expected exception 'X'");
    BOOST_CHECK_EQUAL(root[2]["dataIndex"].asInt(), 6);
    BOOST_CHECK_EQUAL(root[2]["gasIndex"].asInt(), 7);
    BOOST_CHECK_EQUAL(root[2]["valueIndex"].asInt(), 8);

    BOOST_CHECK_EQUAL(root[3]["category"].asString(), "unexpected");
    BOOST_CHECK_EQUAL(root[3]["dataIndex"].asInt(), 9);
    BOOST_CHECK_EQUAL(root[3]["gasIndex"].asInt(), 10);
    BOOST_CHECK_EQUAL(root[3]["valueIndex"].asInt(), 11);
}

BOOST_AUTO_TEST_CASE(writeFailuresJsonHandlesEmptyList)
{
    auto jsonStr = writeFailuresJson({});
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::istringstream iss(jsonStr);
    std::string errs;
    BOOST_REQUIRE_MESSAGE(
        Json::parseFromStream(reader, iss, &root, &errs), "invalid JSON: " + errs);
    BOOST_REQUIRE(root.isArray());
    BOOST_REQUIRE_EQUAL(root.size(), 0u);
}

BOOST_AUTO_TEST_CASE(firstMismatchCategoryDetectsFirstField)
{
    // A balance-first verifyPostState error string → "balance".
    BOOST_CHECK_EQUAL(
        firstMismatchCategory("    BALANCE 0xaa: expected 0x01, got 0x02\n"), "balance");

    // The earliest marker wins regardless of the marker type.
    BOOST_CHECK_EQUAL(firstMismatchCategory("    NONCE 0xaa: expected 0x1, got 0x2\n"
                                            "    STORAGE 0xaa key=0x00: expected 0x1, got 0x2\n"),
        "nonce");

    BOOST_CHECK_EQUAL(firstMismatchCategory("    CODE 0xaa: expected 0xbb\n"), "code");
    BOOST_CHECK_EQUAL(
        firstMismatchCategory("    STORAGE 0xaa key=0x00: expected 0x1, got 0x2\n"), "storage");
    BOOST_CHECK_EQUAL(firstMismatchCategory(""), "");

    // Storage marker earlier in the string than balance → storage wins.
    BOOST_CHECK_EQUAL(firstMismatchCategory("    STORAGE a\n    BALANCE b\n"), "storage");
}

}  // namespace bcos::test
