/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file BloomTest.cpp
 * @author: kyonGuo
 * @date 2024/4/29
 */


#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-rpc/web3jsonrpc/model/Bloom.h>
#include <bcos-rpc/web3jsonrpc/model/Log.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <string_view>
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::codec::rlp;
using namespace std::literals;
using namespace std::string_view_literals;
namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(testBloom, RPCFixture)
BOOST_AUTO_TEST_CASE(testLogsToBloom)
{
    // case silkworm
    {
        Logs logs{};
        logs.push_back({.address = 0x22341ae42d6dd7384bc8584e50419ea3ac75b83f_bytes,
            .topics = {0x04491edcd115127caedbd478e2e7895ed80c7847e903431f94f9cfa579cad47f_hash}});
        logs.push_back({
            .address = 0xe7fb22dfef11920312e4989a3a2b81e2ebf05986_bytes,
            .topics =
                {
                    0x7f1fef85c4b037150d3675218e0cdb7cf38fea354759471e309f3354918a442f_hash,
                    0xd85629c7eaae9ea4a10234fed31bc0aeda29b2683ebe0c1882499d272621f6b6_hash,
                },
            .data = 0x2d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7b_bytes,
        });
        auto const bloom = rpc::getLogsBloom(logs);
        auto const bloomHex = toHex(bloom);
        // clang-format off
        auto constexpr expected = "00000000000000000081000000000000000000000000000000000002000000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000028000000000040000080000000400000000000000000000000000000000000000000000000000000000000000000000010000010000000000000000000000000000000001400000000000000008000000000000000000000000000000000"sv;
        // clang-format on
        BOOST_CHECK_EQUAL(bloomHex, expected);
    }
    // case hardhat
    {
        Logs logs{};
        logs.push_back({.address = 0x5fbdb2315678afecb367f032d93f642f64180aa3_bytes,
            .topics = {
                0x8be0079c531659141344cd1fd0a4f28419497f9722a3daafe3b4186f6b6457e0_hash,
                0x0000000000000000000000000000000000000000000000000000000000000000_hash,
                0x000000000000000000000000f39fd6e51aad88f6f4ce6ab8827279cfffb92266_hash,
            }});
        // clang-format off
        auto constexpr expected = "00000000000000000000000000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000040020000000000000100000800000000000000000000000000000000400000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000040000000200000000000000000000000002000000000000000000020000000000000000000000000000000000000000000000000000000000000000000"sv;
        // clang-format on
        auto const bloom = rpc::getLogsBloom(logs);
        auto const bloomHex = toHex(bloom);
        BOOST_CHECK_EQUAL(bloomHex, expected);
    }
}
BOOST_AUTO_TEST_SUITE_END()
};  // namespace bcos::test