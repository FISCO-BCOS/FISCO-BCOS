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
 * @brief test cases for secp256k1/sm2/ed25519
 * @file SignatureTest.h
 * @date 2021.03.06
 */
#include "bcos-crypto/zkp/discretezkp/DiscreteLogarithmZkp.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ZkpTest, TestPromptFixture)
// aggregateRistrettoPoint
BOOST_AUTO_TEST_CASE(testAggregateRistrettoPoint)
{
    std::string point1 = "5ede6177a14b66389ab5fe5785d7c14adf2a3973aa7c3070878954937e47746f";
    std::string point2 = "d21b3e0fde82da359493b96b8f3633f9ba39bbb8d8d72c951c173639c750c248";
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();

    auto result = zkpImpl->aggregateRistrettoPoint(*fromHexString(point1), *fromHexString(point2));
    BOOST_CHECK(
        *toHexString(result) == "327ea1e62fd6c6a3fc8fb203618fbf2f7924bbfbdec52a9324bd6964c4c0ca12");

    point1 = "786058ceffe85198ef127b6a9781b07adbe3b2d21de87a26804e6a1b43cd4135";
    point2 = "18df09397eed2a614062649be865da8e26cbdd8848fdbed7e8ae8b0e67745d52";
    result = zkpImpl->aggregateRistrettoPoint(*fromHexString(point1), *fromHexString(point2));
    BOOST_CHECK(
        *toHexString(result) == "36f238ce7215e9704a60c631ac0d576a5d816baf6fc1092798d3bd6ae260f540");

    point2 = "c25d4eed8b636edaa06e2ee0b3a16d1ea7058a3fd455702180cc9bdb14ce8016";
    result = zkpImpl->aggregateRistrettoPoint(*fromHexString(point1), *fromHexString(point2));
    BOOST_CHECK(
        *toHexString(result) != "36f238ce7215e9704a60c631ac0d576a5d816baf6fc1092798d3bd6ae260f540");
}
// verifyEitherEqualityProof
BOOST_AUTO_TEST_CASE(testVerifyEitherEqualityProof)
{
    std::string c1_point = "e0ad69e116f7aa07e7ce1d9e55eec9be69baa310ec4616fbf7e11756a3ff3627";
    std::string c2_point = "8020353cff25729feb2d057330434b423b5b01fc6325b9c5aae49df56dceff7b";
    std::string c3_point = "aac55c73d60521c0978394872db2a8a9ee21b67ea990e6cd1301f71e6f449216";
    std::string basepoint = "e2f2ae0a6abc4e71a884a961c500515f58e30b6aa582dd8db6a65945e08d2d76";
    std::string blinding_basepoint =
        "8c9240b456a9e6dc65c377a1048d745f94a08cdb7f44cbcd7b46f34048871134";
    std::string proof =
        "d13f92f8ff02ed667f318f90e63db8f7c6cc5504bca4d30ba5ba98ee27762500c97cabd65e0d0f383e2a86ded2"
        "95e8a6b5474c59455802a1d48e480de1b2be08fb48782b96d8090c9b4b619a8998f8cc8999d12edb7e0fbdb394"
        "6c7373b41509807b2a1b88febf3058f843d73e72f73841093be86e20cb0c16f28ee5ffd3750aa90efe06abee0e"
        "893f6af3dbd214a66d312ae4dd1253dd3bcb70dfc5f9821e03a92492bd5a18e05e64c98165b8a433f068c39796"
        "703d8069b6c3fae2b4406d06f54a46f05c5d321c4ed01f1c72c9a89c3d3ba7adbd807da14148dd4c77b41c07c4"
        "3d026dd14fa7c025ad3a06a9a891149d6a25268b49f79cc016c6c49bafd107";
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();
    auto result = zkpImpl->verifyEitherEqualityProof(*fromHexString(c1_point),
        *fromHexString(c2_point), *fromHexString(c3_point), *fromHexString(proof),
        *fromHexString(basepoint), *fromHexString(blinding_basepoint));
    BOOST_CHECK(result == true);

    // invalid case
    result = zkpImpl->verifyEitherEqualityProof(*fromHexString(c2_point), *fromHexString(c2_point),
        *fromHexString(c1_point), *fromHexString(proof), *fromHexString(basepoint),
        *fromHexString(blinding_basepoint));
    BOOST_CHECK(result == false);

    // case equal to zeror
    c1_point = "ced7f98f75ab04635c7acd0600a67067b5c3475a82911aa02cdd92864a273118";
    proof =
        "4bdc1283354b39dbb901b5bd96ee1de30601bf6321c2348f3c594b2a0fe8b306eba04833e1b817c25f50103b95"
        "7ae8657f8e3497b92d993ffd040ad4a4f28a0e831601c9efde53fd3252d6e70aa0f10ecd6852739ae03fc20b4a"
        "aeb922fc3c0895be82ed5be7012dbb25615e41b6184f76f39ecc01a3696c9d5dd8acb79d1b0fcf8db3cc437612"
        "a333d67298562f994857b73cc241cc550cb8ec19057c669e0b7535f5c0b3f73d6778a6d6f9d762cd35be7d6603"
        "a04767c24de80b6544577306bf42cc0f703fddfefffc50b88a1f275c31ceed85cbf0de458fc570ddf4ba570185"
        "d6c0a0b18c136fd2fc16e7bdde856e90ab7488b44ed829ca305d861df1e30e";
    result = zkpImpl->verifyEitherEqualityProof(*fromHexString(c1_point), *fromHexString(c2_point),
        *fromHexString(c3_point), *fromHexString(proof), *fromHexString(basepoint),
        *fromHexString(blinding_basepoint));
    BOOST_CHECK(result == true);
}

// verifyKnowledgeProof
BOOST_AUTO_TEST_CASE(testVerifyKnowledgeProof)
{
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();
    bytes point =
        *fromHexString("c2e63cef83875e81ea26e00546102cfbbca50ec21a92d077df9100d1bc3a461e");
    bytes proof = *fromHexString(
        "5466f3449a00af0d922670b9f80295c6a685c23713349a91a36e4c082a3e282f9aa835448523cfc62b527d7533"
        "0845f4e889c2e70e844c35177a9f0647e3d00b0d17ac6a70acbf14f9a7d838a0b4fe251ba59dc00404cb7d243d"
        "d4cba1832d0f");
    bytes base_point =
        *fromHexString("e2f2ae0a6abc4e71a884a961c500515f58e30b6aa582dd8db6a65945e08d2d76");
    bytes blinding_base_point =
        *fromHexString("625c50529218ebb9f80e296886f4ac5bb55e06416db27b901c552d3e06ec4871");
    auto ret = zkpImpl->verifyKnowledgeProof(point, proof, base_point, blinding_base_point);
    BOOST_CHECK(ret == true);

    // invalid case
    ret = zkpImpl->verifyKnowledgeProof(base_point, proof, point, blinding_base_point);
    BOOST_CHECK(ret == false);
}

// verifyFormatProof
BOOST_AUTO_TEST_CASE(testVerifyFormatProof)
{
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();
    bytes c1 = *fromHexString("febd4c0d856502a95986a7181c42c51768846755147f178c3646a12834b4e63d");
    bytes c2 = *fromHexString("ce8c95f13b202defc2bd0e3ddf62dddeb478b693b5f20ce9cd28b64c473aef0f");
    bytes proof = *fromHexString(
        "80b7cb7fe7f4aa86b86b7d375d15242cc33336e8b2b735807d9a786be353f0544e99b6d416ce1f1b8d97894f33"
        "d2c94dbe4987dea483ac68cf44b2abf7c6594dc12255a9babc8bd275fe3103ea44d0a33cc3cad8f2d0ef4ca5fc"
        "9257464ecd0405bde6b3bb5e612b14c738e6221940a3f07e82344693867bcd6c85360bbff40e");
    bytes c1_basepoint =
        *fromHexString("e2f2ae0a6abc4e71a884a961c500515f58e30b6aa582dd8db6a65945e08d2d76");
    bytes c2_basepoint =
        *fromHexString("8c9240b456a9e6dc65c377a1048d745f94a08cdb7f44cbcd7b46f34048871134");
    bytes blinding_base_point =
        *fromHexString("c2b614cc98c793bff0298b0c0881fa78261f0bc6d5f826484257b34d3480c22e");
    auto ret =
        zkpImpl->verifyFormatProof(c1, c2, proof, c1_basepoint, c2_basepoint, blinding_base_point);
    BOOST_CHECK(ret == true);

    // invalid case
    ret =
        zkpImpl->verifyFormatProof(c2, c1, proof, c1_basepoint, c2_basepoint, blinding_base_point);
    BOOST_CHECK(ret == false);
}

// verifySumProof
BOOST_AUTO_TEST_CASE(testVerifySumProof)
{
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();
    bytes c1 = *fromHexString("d4cc1326224453213b6a8d758a87bda4612cfabc7992127580e39cddba6ca234");
    bytes c2 = *fromHexString("6839477d311c001e605001c6f270b99429b0b2a3fc7e8e0ae70fe77c4c203a1e");
    bytes c3 = *fromHexString("b050a9a8baae17bdaa7d0cac3f3f2fbb2634a02ca0a86962f2ece308b6fffa3e");
    bytes proof = *fromHexString(
        "fca6e4c20eaa0c167aecd92579b726851369cd993b2204aef6d48064522b2f17849ad5218b1caf6857db0df2f9"
        "db348f981e928f61b26aa4b83d2f834332237bf6a293eb4e1161db741fe77c4c22e4109cb4d9fc2baada7c46d1"
        "2a567fe1114cc24be6612d716b0ff984a98d52a6b0215ffd3ae05afb1e3ce2ea326bb1f66f0ed805efec8c1b73"
        "b87541f05bb28fa8bc4a52c1a0505a420f4b55b088900e75000e49b79e6039f17d9f8f01cf6090a3e8b3946113"
        "897e090e46671b6272ecde05412f27c2cd4f06d738473a561bf23cb6bf96960db30a6b414660977417ae580d44"
        "8123c5f895f563adf7bc56fe7ca7a42149dc0ac4a4203ccc3b68daa5cc1504");
    bytes value_basepoint =
        *fromHexString("e2f2ae0a6abc4e71a884a961c500515f58e30b6aa582dd8db6a65945e08d2d76");
    bytes blinding_base_point =
        *fromHexString("8c9240b456a9e6dc65c377a1048d745f94a08cdb7f44cbcd7b46f34048871134");
    auto ret = zkpImpl->verifySumProof(c1, c2, c3, proof, value_basepoint, blinding_base_point);
    BOOST_CHECK(ret == true);

    // invalid case
    ret = zkpImpl->verifySumProof(c2, c1, c3, proof, value_basepoint, blinding_base_point);
    BOOST_CHECK(ret == false);
}

// verifyProductProof
BOOST_AUTO_TEST_CASE(testVerifyProductProof)
{
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();
    bytes c1 = *fromHexString("54a33c1e4a2f7c9201b888b2260aade604a546e5acf49c63bd016fc442270640");
    bytes c2 = *fromHexString("80c99be28493d198e4c2780446d54701a7773ef0d5c4f511d04a832047c62971");
    bytes c3 = *fromHexString("66b290292161da6e43fcd32ef103a674fa9535155443bcba462455a838bac71f");
    bytes proof = *fromHexString(
        "687f1bed4716b0e1b0ab3c855594bf82e119b48f71f0f2c42cdb43725bbe2d39b0ed1666b78c87b785e2c7bbd0"
        "69981be08634a8c9ee712bd94cf30153f63e2974a00e328f42b3992fce13f66a44adb0a5b8814a3bc34d3da406"
        "f3d90014bc679cd0e0456ae03f1cf34156aec93d1b0c81f2e14de374c4d90540562181073500512781dc90f142"
        "30e152e313c8571c9406a9497480d7b71c8b38564dd4c2dd051acc1d1db1acd4528a2ef9911ab487b192abf7d5"
        "3ea94cbd5ed83ca23d7c760d70231328c4a4008cc58de545712dae4f1d336b8562ec59468b737c30b76938002f"
        "79a5923855cf48c6399fa25cebeea13502768ed584dc04b2821b35fb32540d");
    bytes value_basepoint =
        *fromHexString("e2f2ae0a6abc4e71a884a961c500515f58e30b6aa582dd8db6a65945e08d2d76");
    bytes blinding_base_point =
        *fromHexString("8c9240b456a9e6dc65c377a1048d745f94a08cdb7f44cbcd7b46f34048871134");
    auto ret = zkpImpl->verifyProductProof(c1, c2, c3, proof, value_basepoint, blinding_base_point);
    BOOST_CHECK(ret == true);

    // invalid case
    ret = zkpImpl->verifyProductProof(c2, c1, c3, proof, value_basepoint, blinding_base_point);
    BOOST_CHECK(ret == false);
}

// verifyEqualityProof
BOOST_AUTO_TEST_CASE(testVerifyEqualityProof)
{
    auto zkpImpl = std::make_shared<DiscreteLogarithmZkp>();
    bytes c1 = *fromHexString("62bf13dcf116499f3970b8497120741e9dc66fe1d9c3a34274b5e7e851398f40");
    bytes c2 = *fromHexString("6c07f2bd045bd4058e2f8c15b618193229a749827784c6e0b62ee175a987c510");
    bytes proof = *fromHexString(
        "a782e114de54fd1460081bae2b05edfc157b6ff31cb55c85d81c130556fb5b03e2569d3ec8e78be9732e73a6c5"
        "e9c0e231aff1171ae87453747cabaaf2cfdc1de474d1c45b0ac1454e5b3ba860d73d5938b2536f06a13b321fe9"
        "664acfc0c013");
    bytes basepoint1 =
        *fromHexString("e2f2ae0a6abc4e71a884a961c500515f58e30b6aa582dd8db6a65945e08d2d76");
    bytes basepoint2 =
        *fromHexString("8c9240b456a9e6dc65c377a1048d745f94a08cdb7f44cbcd7b46f34048871134");
    auto ret = zkpImpl->verifyEqualityProof(c1, c2, proof, basepoint1, basepoint2);
    BOOST_CHECK(ret == true);

    // invalid case
    ret = zkpImpl->verifyEqualityProof(c2, c1, proof, basepoint1, basepoint2);
    BOOST_CHECK(ret == false);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos