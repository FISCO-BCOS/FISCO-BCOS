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

#include <bcos-executor/src/vm/Precompiled.h>
#include <bcos-executor/src/vm/kzgPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(PrecompiledTest)

BOOST_AUTO_TEST_CASE(PointEvaluatePrecompiledTest)
{
    static constexpr intx::uint256 kBlsModulus{intx::from_string<intx::uint256>(
        "52435875175126190479447740508185965837690552500527637822603658699938581184513")};

    bytes in = *bcos::fromHexString(
        "014edfed8547661f6cb416eba53061a2f6dce872c0497e6dd485a876fe2567f1564c0a11a0f704f4fc3e8acfe0"
        "f8245f0ad1347b378fbf96e206da11a5d363066d928e13fe443e957d82e3e71d48cb65d51028eb4483e719bf8e"
        "fcdf12f7c321a421e229565952cfff4ef3517100a97da1d4fe57956fa50a442f92af03b1bf37adacc8ad4ed209"
        "b31287ea5bb94d9d06a444d6bb5aadc3ceb615b50d6606bd54bfe529f59247987cd1ab848d19de599a9052f183"
        "5fb0d0d44cf70183e19a68c9");
    static std::string pointE("point_evaluation");
    PrecompiledExecutor const& executor = PrecompiledRegistrar::executor(pointE);
    bytesConstRef input_ref(in.data(), in.size());
    std::pair<bool, bytes> out = executor(input_ref);
    BOOST_CHECK(out.first);
    BOOST_CHECK((out.second.size() == 64));
    intx::uint256 fieldElementsPerBlob{intx::be::unsafe::load<intx::uint256>(out.second.data())};
    BOOST_CHECK(fieldElementsPerBlob == 4096);
    intx::uint256 blsModulus{intx::be::unsafe::load<intx::uint256>(out.second.data() + 32)};
    BOOST_CHECK(blsModulus == kBlsModulus);

    // change hash version
    in[0] = 0x2;
    bytesConstRef input_ref1(in.data(), in.size());
    auto out1 = executor(input_ref1);
    BOOST_CHECK(!out1.first);
    in[0] = 0x1;

    // truncate input
    in.pop_back();
    bytesConstRef input_ref2(in.data(), in.size());
    auto out2 = executor(input_ref2);
    BOOST_CHECK(!out2.first);
    in.push_back(0xba);

    // extra input
    in.push_back(0);
    bytesConstRef input_ref3(in.data(), in.size());
    auto out3 = executor(input_ref3);
    BOOST_CHECK(!out3.first);
    in.pop_back();

    // Try z > BLS_MODULUS
    intx::uint256 z{intx::le::unsafe::load<intx::uint256>(&in[32])};
    z += kBlsModulus;
    intx::le::unsafe::store(&in[32], z);
    bytesConstRef input_ref4(in.data(), in.size());
    auto out4 = executor(input_ref4);
    BOOST_CHECK(!out4.first);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test