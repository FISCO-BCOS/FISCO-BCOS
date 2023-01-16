/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file UtilitiesTest.cpp
 * @author: kyonGuo
 * @date 2023/1/3
 */

#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-executor/src/precompiled/common/Utilities.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-tool/VersionConverter.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class UtilitiesFixture : public PrecompiledFixture
{
public:
    UtilitiesFixture() = default;

    ~UtilitiesFixture() override = default;

    void init(bool _isWasm, protocol::BlockVersion version = BlockVersion::V3_1_VERSION)
    {
        setIsWasm(_isWasm, false, true, version);
    }

    static bool checkPathValid(
        std::string const& path, protocol::BlockVersion version = BlockVersion::V3_1_VERSION)
    {
        return precompiled::checkPathValid(path, version);
    }
};
BOOST_FIXTURE_TEST_SUITE(UtilitiesTest, UtilitiesFixture)

BOOST_AUTO_TEST_CASE(pathValidTest)
{
    BOOST_CHECK(!checkPathValid(""));
    BOOST_CHECK(!checkPathValid("/apps/check/path/level/is/too/deep"));
    BOOST_CHECK(!checkPathValid(
        "/apps/check/path/too/looooooooooooooooooooooooooooooooooooooooooooooooon"));
    BOOST_CHECK(checkPathValid("/"));
    BOOST_CHECK(checkPathValid("/apps/123456/"));
    BOOST_CHECK(checkPathValid("apps/123456/"));
    BOOST_CHECK(!checkPathValid("/apps/123456>"));
    BOOST_CHECK(!checkPathValid("/apps/123456="));
    BOOST_CHECK(!checkPathValid("/apps/123\"456"));
    /// >= 3.2
    BOOST_CHECK(checkPathValid("/apps/123456/", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(checkPathValid("apps/123456/", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(!checkPathValid("/apps/123456>", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(!checkPathValid("/apps/123456=", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(!checkPathValid("/apps/123\"456", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(!checkPathValid("/apps/123456\n", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(!checkPathValid("/apps/123456\t", BlockVersion::V3_2_VERSION));
    BOOST_CHECK(!checkPathValid("/apps/123456 ", BlockVersion::V3_2_VERSION));
    //    BOOST_CHECK(checkPathValid(std::string("/apps/123456中文"), BlockVersion::V3_2_VERSION));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test