/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-2565 / EIP-7883 modexp gas vectors (geth modexp_eip2565.json, modexp_eip7883.json).
 *  @file ModexpGasTest.cpp
 */

#include "vm/ModexpGas.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

struct ModexpGasVector
{
    const char* name;
    const char* inputHex;
    int64_t expectedGas;
};

// Vectors from ethereum/go-ethereum core/vm/testdata/precompiles/modexp_eip2565.json
static ModexpGasVector const kNagydaniEip2565[] = {
    {"nagydani-1-square",
        "0000000000000000000000000000000000000000000000000000000000000040"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000040"
        "e09ad9675465c53a109fac66a445c91b292d2bb2c5268addb30cd82f80fcb0"
        "033ff97c80a5fc6f39193ae969c6ede6710a6b7ac27078a06d90ef1c72e5c85fb50"
        "2fc9e1f6beb81516545975218075ec2af118cd8798df6e08a147c60fd6095ac2bb02c2908cf4dd7c81f11c289e"
        "4"
        "bce98f3553768f392a80ce22bf5c4f4a248c6b",
        200},
    {"nagydani-1-qube",
        "0000000000000000000000000000000000000000000000000000000000000040"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000040"
        "e09ad9675465c53a109fac66a445c91b292d2bb2c5268addb30cd82f80fcb0"
        "033ff97c80a5fc6f39193ae969c6ede6710a6b7ac27078a06d90ef1c72e5c85fb50"
        "3fc9e1f6beb81516545975218075ec2af118cd8798df6e08a147c60fd6095ac2bb02c2908cf4dd7c81f11c289e"
        "4"
        "bce98f3553768f392a80ce22bf5c4f4a248c6b",
        200},
    {"nagydani-1-pow0x10001",
        "0000000000000000000000000000000000000000000000000000000000000040"
        "0000000000000000000000000000000000000000000000000000000000000003"
        "0000000000000000000000000000000000000000000000000000000000000040"
        "e09ad9675465c53a109fac66a445c91b292d2bb2c5268addb30cd82f80fcb0"
        "033ff97c80a5fc6f39193ae969c6ede6710a6b7ac27078a06d90ef1c72e5c85fb50"
        "10001fc9e1f6beb81516545975218075ec2af118cd8798df6e08a147c60fd6095ac2bb02c2908cf4dd7c81f11c"
        "289"
        "e4bce98f3553768f392a80ce22bf5c4f4a248c6b",
        341},
    {"nagydani-2-pow0x10001",
        "0000000000000000000000000000000000000000000000000000000000000080"
        "0000000000000000000000000000000000000000000000000000000000000003"
        "0000000000000000000000000000000000000000000000000000000000000080"
        "cad7d991a00047dd54d3399b6b0b937c718abddef7917c75b6681f40cc15e2be0"
        "003657d8d4c34167b2f0bbbca0ccaa407c2a6a07d50f1517a8f22979ce12a81dcaf707cc0cebfc0ce2ee84ee7f"
        "77"
        "c38b9281b9822a8d3de62784c089c9b18dcb9a2a5eecbede90ea788a862a9ddd9d609c2c52972d63e289e28f6a"
        "5"
        "90ffbf51010001e6d893b80aeed5e6e9ce9afa8a5d5675c93a32ac05554cb20e9951b2c140e3ef4e433068cf0f"
        "b7"
        "3bc9f33af1853f64aa27a0028cbf570d7ac9048eae5dc7b28c87c31e5810f1e7fa2cda6adf9f1076dbc1ec1238"
        "5"
        "60071e7efc4e9565c49be9e7656951985860a558a754594115830bcdb421f741408346dd5997bb01c287087",
        1365},
    {"nagydani-4-pow0x10001",
        "0000000000000000000000000000000000000000000000000000000000000200"
        "0000000000000000000000000000000000000000000000000000000000000003"
        "0000000000000000000000000000000000000000000000000000000000000200"
        "db34d0e438249c0ed685c949cc28776a05094e1c48691dc3f2dca5fc3356d2a0663bd376e4712839917eb9a19c"
        "6"
        "70407e2c377a2de385a3ff3b52104f7f1f4e0c7bf7717fb913896693dc5edbb65b760ef1b00e42e9d8f9af1735"
        "238"
        "5e1cd742c9b006c0f669995cb0bb21d28c0aced2892267637b6470d8cee0ab27fc5d42658f6e88240c31d6774a"
        "a"
        "60a7ebd25cd48b56d0da11209f1928e61005c6eb709f3e8e0aaf8d9b10f7d7e296d772264dc76897ccdddadc91"
        "e"
        "fa91c1903b7232a9e4c3b941917b99a3bc0c26497dedc897c25750af60237aa67934a26a2bc491db3dcc677491"
        "9"
        "44bc1f51d3e5d76b8d846a62db03dedd61ff508f91a56d71028125035c3a44cbb041497c83bf3e4ae2a9613a40"
        "1"
        "cc721c547a2afa3b16a2969933d3626ed6d8a7428648f74122fd3f2a02a20758f7f693892c8fd798b39abac01d"
        "1"
        "8506c45e71432639e9f9505719ee822f62ccbf47f6850f096ff77b5afaf4be7d772025791717dbe5abf9b3f40c"
        "ff"
        "7d7aab6f67e38f62faf510747276e20a42127e7500c444f9ed92baf65ade9e836845e39c4316d9dce5f8e2c808"
        "3"
        "e2c0acbb95296e05e51aab13b6b8f53f06c9c4276e12b0671133218cc3ea907da3bd9a367096d9202128d14846"
        "cc"
        "2e20d56fc8473ecb07cecbfb8086919f3971926e7045b853d85a69d026195c70f9f7a823536e2a8f4b3e12e94d"
        "9"
        "b53a934353451094b81010001df3143a0057457d75e8c708b6337a6f5a4fd1a06727acf9fb93e2993c62f3378b"
        "3"
        "7d56c85e7b1e00f0145ebf8e4095bd723166293c60b6ac1252291ef65823c9e040ddad14969b3b340a4ef714db"
        "0"
        "93a587c37766d68b8d6b5016e741587e7e6bf7e763b44f0247e64bae30f994d248bfd20541a333e5b225ef6a61"
        "19"
        "9e301738b1e688f70ec1d7fb892c183c95dc543c3e12adf8a5e8b9ca9d04f9445cced3ab256f29e998e69efaa6"
        "33"
        "a7b60e1db5a867924ccab0a171d9d6e1098dfa15acde9553de599eaa56490c8f411e4985111f3d40bddfc5e301"
        "e"
        "db01547b01a886550a61158f7e2033c59707789bf7c854181d0c2e2a42a93cf09209747d7082e147eb8544de25"
        "c"
        "3eb14f2e35559ea0c0f5877f2f3fc92132c0ae9da4e45b2f6c866a224ea6d1f28c05320e287750fbc647368d41"
        "11"
        "6e528014cc1852e5531d53e4af938374daba6cee4baa821ed07117253bb3601ddd00d59a3d7fb2ef1f5a2fbba7"
        "c4"
        "29f0cf9a5b3462410fd833a69118f8be9c559b1000cc608fd877fb43f8e65c2d1302622b944462579056874b38"
        "72"
        "08d90623fcdaf93920ca7a9e4ba64ea208758222ad868501cc2c345e2d3a5ea2a17e5069248138c8a79c025118"
        "5"
        "d29ee73e5afab5354769142d2bf0cb6712727aa6bf84a6245fcdae66e4938d84d1b9dd09a884818622080ff5f9"
        "89"
        "42fb20acd7e0c916c2d5ea7ce6f7e173315384518f",
        21845},
};

// Expected Osaka (EIP-7883) gas for kNagydaniEip2565 inputs — geth modexp_eip7883.json
static int64_t const kNagydaniEip7883ExpectedGas[] = {
    500,     // nagydani-1-square
    500,     // nagydani-1-qube
    2048,    // nagydani-1-pow0x10001
    8192,    // nagydani-2-pow0x10001
    131072,  // nagydani-4-pow0x10001
};

BOOST_AUTO_TEST_SUITE(ModexpGasTest)

BOOST_AUTO_TEST_CASE(nagydani_eip2565_gas_vectors)
{
    for (auto const& vec : kNagydaniEip2565)
    {
        auto const input = bcos::fromHex(vec.inputHex);
        auto const gas = executor::calcModexpGas(ref(input), EVMC_BERLIN).convert_to<int64_t>();
        BOOST_CHECK_MESSAGE(gas == vec.expectedGas,
            vec.name << " expected gas " << vec.expectedGas << " got " << gas);
    }
}

BOOST_AUTO_TEST_CASE(revision_switches_eip198_vs_eip2565)
{
    bytes input(96, 0);
    input[31] = 32;
    input[63] = 1;
    input[95] = 32;
    input.resize(96 + 32 + 1 + 32, 0);
    input[96] = 0x01;
    input[96 + 32] = 0x01;
    input[96 + 33] = 0x01;

    auto const gas198 = executor::calcModexpGas(ref(input), EVMC_ISTANBUL).convert_to<int64_t>();
    auto const gas2565 = executor::calcModexpGas(ref(input), EVMC_BERLIN).convert_to<int64_t>();
    BOOST_CHECK_EQUAL(gas198, 51);
    BOOST_CHECK_EQUAL(gas2565, 200);
    BOOST_CHECK_NE(gas198, gas2565);
}

struct Modexp7883Vector
{
    const char* name;
    const char* inputHex;
    int64_t expectedGas;
};

static Modexp7883Vector const kEip7883[] = {
    {"minimal-1byte-all",
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0300000000000000000000000000000000000000000000000000000000000000"
        "0100000000000000000000000000000000000000000000000000000000000000"
        "0100000000000000000000000000000000000000000000000000000000000000",
        500},
    {"zero-exponent-32bytes",
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0100000000000000000000000000000000000000000000000000000000000000"
        "0100000000000000000000000000000000000000000000000000000000000000",
        500},
    {"marius-1-even",
        "0000000000000000000000000000000000000000000000000000000000000003"
        "00000000000000000000000000000000000000000000000000000000000000c1"
        "000000000000000000000000000000000000000000000000000000000000000c"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe0"
        "00007d7d7d83828282348286877d7d827d407d797d7d7d7d7d7d7d7d7d7d7d5b"
        "0000000000000000000000000000000000000000000000000000000000000003"
        "0000000000000000000000000000000000000000000000000000000000000021"
        "000000000000000000000000000000000000000000000000000000000000000c"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff40"
        "00007d7d7d83828282348286877d7d82",
        45296},
    {"guido-1-even",
        "0000000000000000000000000000000000000000000000000000000000000010"
        "00000000000000000000000000000000000000000000000000000000000000d8"
        "0000000000000000000000000000000000000000000000000000000000000010"
        "ffffffffffffffff76ffffffffffffff1cffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffc7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c76ec7"
        "c7c7c7ffffffffffffffc7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7"
        "ffffffffffffc7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c76ec7c7c7"
        "c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7c7"
        "c7c7c7c7c7c7ffffffffff3f000000000000000000000000",
        51136},
};

BOOST_AUTO_TEST_CASE(nagydani_eip7883_gas_vectors)
{
    static_assert(sizeof(kNagydaniEip2565) / sizeof(kNagydaniEip2565[0]) ==
                      sizeof(kNagydaniEip7883ExpectedGas) / sizeof(kNagydaniEip7883ExpectedGas[0]),
        "7883 gas table must match nagydani input table");
    for (size_t i = 0; i < sizeof(kNagydaniEip2565) / sizeof(kNagydaniEip2565[0]); ++i)
    {
        auto const& vec = kNagydaniEip2565[i];
        auto const input = bcos::fromHex(vec.inputHex);
        auto const gas = executor::calcModexpGas(ref(input), EVMC_OSAKA).convert_to<int64_t>();
        auto const expected = kNagydaniEip7883ExpectedGas[i];
        BOOST_CHECK_MESSAGE(
            gas == expected, vec.name << " expected gas " << expected << " got " << gas);
    }
}

BOOST_AUTO_TEST_CASE(eip7883_gas_vectors)
{
    for (auto const& vec : kEip7883)
    {
        auto const input = bcos::fromHex(vec.inputHex);
        auto const gas = executor::calcModexpGas(ref(input), EVMC_OSAKA).convert_to<int64_t>();
        BOOST_CHECK_MESSAGE(gas == vec.expectedGas,
            vec.name << " expected gas " << vec.expectedGas << " got " << gas);
    }
}

BOOST_AUTO_TEST_CASE(revision_switches_2565_vs_7883)
{
    bytes input(96, 0);
    input[31] = 32;
    input[63] = 1;
    input[95] = 32;
    input.resize(96 + 32 + 1 + 32, 0);
    input[96] = 0x01;
    input[96 + 32] = 0x01;
    input[96 + 33] = 0x01;

    auto const g2565 = executor::calcModexpGas(ref(input), EVMC_BERLIN).convert_to<int64_t>();
    auto const g7883 = executor::calcModexpGas(ref(input), EVMC_OSAKA).convert_to<int64_t>();
    BOOST_CHECK_EQUAL(g2565, 200);
    BOOST_CHECK_GE(g7883, 500);
    BOOST_CHECK_NE(g2565, g7883);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
