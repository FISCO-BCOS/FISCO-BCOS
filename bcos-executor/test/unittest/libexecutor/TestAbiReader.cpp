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
 */
/**
 * @brief : unitest for abi reader
 * @author: catli
 * @date: 2021-09-26
 */

#include "../src/dag/Abi.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
struct AbiReaderFixture
{
    AbiReaderFixture() { hashImpl = std::make_shared<Keccak256>(); }

    Hash::Ptr hashImpl;
};

BOOST_FIXTURE_TEST_SUITE(TestAbiReader, AbiReaderFixture)

BOOST_AUTO_TEST_CASE(NormalCase)
{
    auto abiStr = R"(
    [
        {
            "inputs":[
                {
                    "components":[
                        {
                            "internalType":"string",
                            "name":"name",
                            "type":"string"
                        },
                        {
                            "internalType":"uint32",
                            "name":"price",
                            "type":"uint32"
                        },
                        {
                            "internalType":"uint128",
                            "name":"count",
                            "type":"uint128"
                        }
                    ],
                    "internalType":"struct.Product[]",
                    "name":"init_products",
                    "type":"tuple[]"
                }
            ],
            "type":"constructor"
        },
        {
            "conflictFields":[
                {
                    "kind":0,
                    "value":[

                    ],
                    "read_only":true,
                    "slot":0
                },
                {
                    "kind":4,
                    "value":[
                        0, 1, 2
                    ],
                    "read_only":false,
                    "slot":1
                }
            ],
            "constant":false,
            "inputs":[
                {
                    "components":[
                        {
                            "internalType":"string",
                            "name":"name",
                            "type":"string"
                        },
                        {
                            "internalType":"uint32",
                            "name":"price",
                            "type":"uint32"
                        },
                        {
                            "internalType":"uint128",
                            "name":"count",
                            "type":"uint128"
                        }
                    ],
                    "internalType":"struct.Product[]",
                    "name":"prods",
                    "type":"tuple[]"
                }
            ],
            "selector": [352741043,0],
            "name":"add_prod_batch",
            "outputs":[

            ],
            "type":"function"
        },
        {
            "constant":true,
            "inputs":[

            ],
            "name":"admin",
            "outputs":[
                {
                    "internalType":"string",
                    "type":"string"
                }
            ],
            "selector": [352741043,0],
            "type":"function"
        }
    ]
    )"sv;

    auto result = FunctionAbi::deserialize(abiStr, *fromHexString("150666b3"), false);
    BOOST_CHECK(result.get() != nullptr);
    BOOST_CHECK_EQUAL(result->inputs.size(), 1);

    auto& input = result->inputs[0];
    BOOST_CHECK_EQUAL(input.type, "tuple[]");
    BOOST_CHECK_EQUAL(input.components.size(), 3);
    BOOST_CHECK_EQUAL(input.components[0].type, "string");
    BOOST_CHECK_EQUAL(input.components[1].type, "uint32");
    BOOST_CHECK_EQUAL(input.components[2].type, "uint128");

    auto& conflictFields = result->conflictFields;
    BOOST_CHECK_EQUAL(conflictFields.size(), 2);
    BOOST_CHECK_EQUAL(conflictFields[0].kind, 0);

    auto accessPath = vector<uint8_t>{};
    BOOST_CHECK(
        std::equal(accessPath.begin(), accessPath.end(), conflictFields[0].value.begin()));
    BOOST_CHECK_EQUAL(conflictFields[0].slot.value(), 0);

    accessPath = vector<uint8_t>{0, 1, 2};
    cout << conflictFields[1].value.size() << endl;
    BOOST_CHECK(
        std::equal(accessPath.begin(), accessPath.end(), conflictFields[1].value.begin()));
    BOOST_CHECK_EQUAL(conflictFields[1].slot.value(), 1);
}

BOOST_AUTO_TEST_CASE(InvalidAbi)
{
    auto abiStr = "vita"sv;
    auto result = FunctionAbi::deserialize(abiStr, *fromHexString("150666b3"), false);
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(InvalidSelector)
{
    auto abiStr = R"(
    [
        {
            "conflictFields":[
                {
                    "kind":0,
                    "value":[

                    ],
                    "read_only":false,
                    "slot":0
                }
            ],
            "constant":false,
            "inputs":[
                {
                    "internalType":"string",
                    "name":"name",
                    "type":"string"
                }
            ],
            "name":"set",
            "outputs":[

            ],
            "selector": [1322485854,0],
            "type":"function"
        }
    ]
    )"sv;

    auto result = FunctionAbi::deserialize(abiStr, *fromHexString("150666b3"), false);
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(EmptyConflictFields)
{
    auto abiStr = R"(
    [
        {
            "constant":false,
            "inputs":[
                {
                    "internalType":"string",
                    "name":"name",
                    "type":"string"
                }
            ],
            "name":"set",
            "outputs":[

            ],
            "selector": [1322485854,0],
            "type":"function"
        }
    ]
    )"sv;

    auto result = FunctionAbi::deserialize(abiStr, *fromHexString("4ed3885e"), false);
    BOOST_CHECK(result.get() != nullptr);
    BOOST_CHECK(result->conflictFields.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos