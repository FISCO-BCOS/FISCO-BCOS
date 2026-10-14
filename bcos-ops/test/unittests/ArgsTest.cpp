/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief Args and the dispatch around it: a missing value, an unknown option and a stray
 *        positional are usage errors, never a silent fallback to the local node
 * @file ArgsTest.cpp
 */
#include <bcos-ops/Args.h>
#include <bcos-ops/Cli.h>
#include <bcos-ops/OpsError.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
struct Run
{
    int code;
    std::string out;
    std::string err;
};
Run run(std::vector<std::string> _args)
{
    std::ostringstream out;
    std::ostringstream err;
    auto code = runOps(_args, out, err);
    return {code, out.str(), err.str()};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ArgsTest)

BOOST_AUTO_TEST_CASE(valueOptionWithoutValueIsUsageError)
{
    // used to become a flag: --rpc unset, status fell back to --node-dir . and could exit 0
    auto bare = run({"fisco-bcos", "status", "--rpc"});
    BOOST_CHECK_EQUAL(bare.code, c_exitUsage);
    BOOST_CHECK(bare.err.find("--rpc needs a value") != std::string::npos);
    auto followed = run({"fisco-bcos", "status", "--rpc", "--json"});
    BOOST_CHECK_EQUAL(followed.code, c_exitUsage);
    BOOST_CHECK(followed.err.find("--rpc needs a value") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(unknownOptionIsUsageError)
{
    auto typo = run({"fisco-bcos", "status", "--rp", "1.2.3.4:20200"});
    BOOST_CHECK_EQUAL(typo.code, c_exitUsage);
    BOOST_CHECK(typo.err.find("unknown option --rp") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(strayPositionalIsUsageError)
{
    auto stray = run({"fisco-bcos", "status", "node0"});
    BOOST_CHECK_EQUAL(stray.code, c_exitUsage);
    BOOST_CHECK(stray.err.find("unexpected argument: node0") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(errorTextIsSanitized)
{
    auto escape = run({"fisco-bcos", "status", "--\x1b[2Jx", "1"});
    BOOST_CHECK_EQUAL(escape.code, c_exitUsage);
    BOOST_CHECK(escape.err.find("unknown option") != std::string::npos);
    BOOST_CHECK(escape.err.find('\x1b') == std::string::npos);
}

BOOST_AUTO_TEST_CASE(booleanFlagForms)
{
    BOOST_CHECK(Args({"--json"}, {"json"}).flag("json"));
    BOOST_CHECK(Args({"--json=1"}, {"json"}).flag("json"));
    BOOST_CHECK(Args({"--json=true"}, {"json"}).flag("json"));
    BOOST_CHECK(!Args({"--json=0"}, {"json"}).flag("json"));
    BOOST_CHECK(!Args({"--json=false"}, {"json"}).flag("json"));
    BOOST_CHECK_THROW(Args({"--json=yes"}, {"json"}), OpsError);
    // a boolean never swallows the next token
    Args args({"--json", "0x12"}, {"json"});
    BOOST_CHECK(args.flag("json"));
    BOOST_REQUIRE_EQUAL(args.positionals().size(), 1U);
    BOOST_CHECK_EQUAL(args.positionals()[0], "0x12");
}

BOOST_AUTO_TEST_CASE(valueForms)
{
    std::set<std::string> const options{"max-lag", "rpc"};
    Args spaced({"--max-lag", "5"}, {}, options);
    BOOST_CHECK_EQUAL(*spaced.option("max-lag"), "5");
    Args inlined({"--rpc=127.0.0.1:20200"}, {}, options);
    BOOST_CHECK_EQUAL(*inlined.option("rpc"), "127.0.0.1:20200");
    Args negative({"--max-lag", "-5"}, {}, options);  // -5 is a value
    BOOST_CHECK_EQUAL(*negative.option("max-lag"), "-5");
    // without a declared set (tests, nested dispatch) unknown names are accepted, values are not
    // optional
    BOOST_CHECK_THROW(Args({"--count"}, {}), OpsError);
    BOOST_CHECK_EQUAL(*Args({"--count", "2"}, {}).option("count"), "2");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
