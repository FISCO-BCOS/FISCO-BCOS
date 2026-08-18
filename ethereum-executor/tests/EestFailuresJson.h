/**
 * @file EestFailuresJson.h
 * @brief Structured failure serialization for the EEST runner.
 *
 * Extracted from EESTRunner.cpp so the --json-failures writer and the
 * first-mismatch category logic can be unit-tested in the standalone
 * eest-json-tests target (which must not depend on ethereum-executor or
 * jsoncpp-only linkage).
 */
#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace bcos::test
{
/// Structured failure record emitted by eest-runner's --json-failures output.
/// `category` is the first mismatched field ("nonce"/"balance"/"code"/"storage")
/// or a control-flow classification ("expected_exception"/"unexpected").
struct FailureDetail
{
    std::string testName;
    std::string forkName;
    std::string reason;
    std::string category;  // nonce|balance|code|storage|expected_exception|unexpected
    int dataIndex = 0;     // EF post index (data/gas/value combo) for precise diff
    int gasIndex = 0;
    int valueIndex = 0;
};

/// Serialize failure details as a JSON array; each element carries exactly the
/// seven fields of FailureDetail.
std::string writeFailuresJson(std::vector<FailureDetail> const& details);

/// Return the field category of the first mismatch marker in a verifyPostState
/// error string ("nonce"/"balance"/"code"/"storage"), or "" when no marker is
/// present. verifyPostState appends markers ("    NONCE ", "    BALANCE ",
/// "    CODE ", "    STORAGE ") in account/category iteration order, so the
/// earliest marker in the string is the first mismatch; this helper is the
/// single source of truth used by the runner and exercised by the unit test.
std::string firstMismatchCategory(std::string const& errorStr);

}  // namespace bcos::test
