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
 * @file AccountPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-11-15
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/extension/AccountManagerPrecompiled.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::crypto;
using namespace bcos::codec;

namespace bcos::test
{
class AccountPrecompiledFixture : public PrecompiledFixture
{
public:
    AccountPrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false, true);
    }

    ~AccountPrecompiledFixture() override {}
};

BOOST_FIXTURE_TEST_SUITE(AccountPrecompiledTest, AccountPrecompiledFixture)

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test