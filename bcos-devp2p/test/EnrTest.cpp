/**
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
 * @file EnrTest.cpp
 * @brief ENR codec / v4 identity scheme tests against the enr.md spec vector.
 * @date 2026/11/3
 */
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-devp2p/disc/Enr.h>
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::disc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EnrTest)

// enr.md "Test Vectors": 127.0.0.1, udp 30303, seq 1, signed with the private key below.
constexpr std::string_view c_specText =
    "enr:-IS4QHCYrYZbAKWCBRlAy5zzaDZXJBGkcnh4MHcBFZntXNFrdvJjX04jRzjzCBOonrkTfj499SZuOh8R33Ls8RRcy5"
    "wBgmlkgnY0gmlwhH8AAAGJc2VjcDI1NmsxoQPKY0yuDUmstAHYpMa2_oxVtw0RW_QAdpzBQA8yWM0xOIN1ZHCCdl8";
constexpr std::string_view c_specPrivateKey =
    "b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291";
constexpr std::string_view c_specNodeId =
    "a448f24c6d18e575453db13171562b71999873db5b286df957af199ec94617f7";
constexpr std::string_view c_specCompressedKey =
    "03ca634cae0d49acb401d8a4c6b6fe8c55b70d115bf400769cc1400f3258cd3138";
constexpr std::string_view c_specSignature =
    "7098ad865b00a582051940cb9cf36836572411a47278783077011599ed5cd16b76f2635f4e234738f30813a89eb913"
    "7e3e3df5266e3a1f11df72ecf1145ccb9c";

BOOST_AUTO_TEST_CASE(decodeSpecVector)
{
    auto decoded = Enr::fromText(c_specText);
    BOOST_REQUIRE(decoded.has_value());
    auto const& record = *decoded;
    BOOST_CHECK(record.verify());
    BOOST_CHECK_EQUAL(record.seq(), 1u);
    BOOST_REQUIRE(record.id().has_value());
    BOOST_CHECK_EQUAL(*record.id(), "v4");
    BOOST_CHECK_EQUAL(record.ipString(), "127.0.0.1");
    BOOST_REQUIRE(record.udp().has_value());
    BOOST_CHECK_EQUAL(*record.udp(), 30303);
    BOOST_CHECK(!record.tcp().has_value());
    BOOST_CHECK(!record.ip6().has_value());
    BOOST_CHECK(!record.eth().has_value());
    BOOST_REQUIRE(record.secp256k1().has_value());
    BOOST_CHECK_EQUAL(toHex(*record.secp256k1()), c_specCompressedKey);
    BOOST_CHECK_EQUAL(toHex(record.signature()), c_specSignature);
    BOOST_REQUIRE(record.nodeId().has_value());
    BOOST_CHECK_EQUAL(record.nodeId()->hex(), c_specNodeId);
    // The uncompressed key matches the spec private key's public key.
    rlpx::EccKeyPair keyPair(fromHex(std::string(c_specPrivateKey)));
    BOOST_REQUIRE(record.publicKey().has_value());
    BOOST_CHECK(*record.publicKey() == keyPair.publicKey());
    // Key order in the record is the sorted order: id, ip, secp256k1, udp.
    BOOST_REQUIRE_EQUAL(record.pairs().size(), 4u);
    BOOST_CHECK_EQUAL(record.pairs()[0].first, "id");
    BOOST_CHECK_EQUAL(record.pairs()[1].first, "ip");
    BOOST_CHECK_EQUAL(record.pairs()[2].first, "secp256k1");
    BOOST_CHECK_EQUAL(record.pairs()[3].first, "udp");
    // Byte-exact round trip through the text form.
    BOOST_CHECK_EQUAL(record.toText(), c_specText);
}

BOOST_AUTO_TEST_CASE(signReproducesSpecVector)
{
    // RFC 6979 deterministic signing: building the same record with the same key must
    // yield the spec's exact signature and text.
    Enr record;
    record.setSeq(1);
    record.setIp({127, 0, 0, 1});
    record.setUdp(30303);
    record.sign(bcos::ref(fromHex(std::string(c_specPrivateKey))));
    BOOST_CHECK(record.verify());
    BOOST_CHECK_EQUAL(toHex(record.signature()), c_specSignature);
    BOOST_CHECK_EQUAL(record.toText(), c_specText);
    // Mutation clears the signature; re-signing with a bumped seq changes it.
    record.setSeq(2);
    BOOST_CHECK(!record.isSigned());
    BOOST_CHECK(!record.verify());
    record.sign(bcos::ref(fromHex(std::string(c_specPrivateKey))));
    BOOST_CHECK(record.verify());
    BOOST_CHECK_NE(toHex(record.signature()), c_specSignature);
}

BOOST_AUTO_TEST_CASE(rejectsTamperedAndMalformed)
{
    auto decoded = Enr::fromText(c_specText);
    BOOST_REQUIRE(decoded.has_value());
    // Flip the last byte of the signed content (udp 30303 -> 30304) under the original
    // signature: the signature no longer verifies.
    auto tamperedBytes = decoded->encode();
    BOOST_REQUIRE_EQUAL(tamperedBytes.back(), 0x5f);
    tamperedBytes.back() = 0x60;
    auto tampered = Enr::decode(bcos::ref(tamperedBytes));
    BOOST_REQUIRE(tampered.has_value());
    BOOST_CHECK_EQUAL(*tampered->udp(), 30304);
    BOOST_CHECK(!tampered->verify());
    // Any mutation clears the signature -> an unsigned record never verifies.
    auto mutated = *decoded;
    mutated.setRaw("udp", {0x82, 0x76, 0x60});
    BOOST_CHECK(!mutated.isSigned());
    BOOST_CHECK(!mutated.verify());

    // Unsorted keys: swap "ip" and "id" positions in a hand-built list.
    {
        bcos::bytes payload;
        bcos::bytes sig(64, 0x11);
        bcos::codec::rlp::encode(payload, sig);
        bcos::codec::rlp::encode(payload, uint64_t(1));
        bcos::codec::rlp::encode(payload, std::string("ip"));
        bcos::codec::rlp::encode(payload, bcos::bytes{127, 0, 0, 1});
        bcos::codec::rlp::encode(payload, std::string("id"));
        bcos::codec::rlp::encode(payload, std::string("v4"));
        bcos::bytes record;
        bcos::codec::rlp::encodeHeader(record, {.isList = true, .payloadLength = payload.size()});
        record.insert(record.end(), payload.begin(), payload.end());
        BOOST_CHECK(!Enr::decode(bcos::ref(record)).has_value());
    }
    // Duplicate key.
    {
        bcos::bytes payload;
        bcos::bytes sig(64, 0x11);
        bcos::codec::rlp::encode(payload, sig);
        bcos::codec::rlp::encode(payload, uint64_t(1));
        bcos::codec::rlp::encode(payload, std::string("id"));
        bcos::codec::rlp::encode(payload, std::string("v4"));
        bcos::codec::rlp::encode(payload, std::string("id"));
        bcos::codec::rlp::encode(payload, std::string("v4"));
        bcos::bytes record;
        bcos::codec::rlp::encodeHeader(record, {.isList = true, .payloadLength = payload.size()});
        record.insert(record.end(), payload.begin(), payload.end());
        BOOST_CHECK(!Enr::decode(bcos::ref(record)).has_value());
    }
    // Oversize (> 300 bytes).
    {
        bcos::bytes big(301, 0x00);
        BOOST_CHECK(!Enr::decode(bcos::ref(big)).has_value());
        Enr large;
        large.setRaw("zz", [] {
            bcos::bytes out;
            bcos::codec::rlp::encode(out, bcos::bytes(250, 0x7f));
            return out;
        }());
        BOOST_CHECK_THROW(
            large.sign(bcos::ref(fromHex(std::string(c_specPrivateKey)))), std::invalid_argument);
    }
    // Bad text forms.
    BOOST_CHECK(!Enr::fromText("enr:!!!").has_value());
    BOOST_CHECK(!Enr::fromText("enx:AAAA").has_value());
    BOOST_CHECK(!Enr::fromText("enr:A").has_value());
    // Not a list.
    bcos::bytes notList{0x83, 0x01, 0x02, 0x03};
    BOOST_CHECK(!Enr::decode(bcos::ref(notList)).has_value());
}

BOOST_AUTO_TEST_CASE(ethEntryRoundTrip)
{
    Enr record;
    eth::ForkId forkId{0xfc64ec04, 1150000};
    record.setEth(forkId);
    // geth forkid.enrEntry: [[fc64ec04, 0x118c30]] -> ca c9 84 fc64ec04 83 118c30
    BOOST_REQUIRE(record.rawValue("eth").has_value());
    BOOST_CHECK_EQUAL(toHex(record.rawValue("eth")->toBytes()), "cac984fc64ec0483118c30");
    BOOST_REQUIRE(record.eth().has_value());
    BOOST_CHECK(*record.eth() == forkId);
    record.setTcp(30303);
    record.setUdp(30303);
    record.setIp({10, 0, 0, 2});
    record.setSeq(7);
    record.sign(bcos::ref(fromHex(std::string(c_specPrivateKey))));
    auto reparsed = Enr::fromText(record.toText());
    BOOST_REQUIRE(reparsed.has_value());
    BOOST_CHECK(reparsed->verify());
    BOOST_REQUIRE(reparsed->eth().has_value());
    BOOST_CHECK(*reparsed->eth() == forkId);
    BOOST_CHECK_EQUAL(*reparsed->tcp(), 30303);
    BOOST_CHECK_EQUAL(reparsed->seq(), 7u);
    BOOST_CHECK_EQUAL(reparsed->ipString(), "10.0.0.2");
    // Unknown keys are preserved byte-for-byte.
    record.setRaw("zzz-custom", {0x82, 0xab, 0xcd});
    record.sign(bcos::ref(fromHex(std::string(c_specPrivateKey))));
    auto again = Enr::fromText(record.toText());
    BOOST_REQUIRE(again.has_value());
    BOOST_CHECK(again->verify());
    BOOST_CHECK_EQUAL(toHex(again->rawValue("zzz-custom")->toBytes()), "82abcd");
    BOOST_CHECK_EQUAL(again->toText(), record.toText());
}

BOOST_AUTO_TEST_CASE(dnsExampleRecordsVerify)
{
    // The three leaf records from the EIP-1459 / dnsdisc.md example tree.
    for (auto text :
        {"enr:-HW4QOFzoVLaFJnNhbgMoDXPnOvcdVuj7pDpqRvh6BRDO68aVi5ZcjB3vzQRZH2IcLBGHzo8u"
         "UN3snqmgTiE56CH3AMBgmlkgnY0iXNlY3AyNTZrMaECC2_24YYkYHEgdzxlSNKQEnHhuNAbNlMlW"
         "JxrJxbAFvA",
            "enr:-HW4QAggRauloj2SDLtIHN1XBkvhFZ1vtf1raYQp9TBW2RD5EEawDzbtSmlXUfnaHcvwOizhVYLt"
            "r7e6vw7NAf6mTuoCgmlkgnY0iXNlY3AyNTZrMaECjrXI8TLNXU0f8cthpAMxEshUyQlK-AM0PW2wfrn"
            "acNI",
            "enr:-HW4QLAYqmrwllBEnzWWs7I5Ev2IAs7x_dZlbYdRdMUx5EyKHDXp7AV5CkuPGUPdvbv1_Ms1CPfh"
            "cGCvSElSosZmyoqAgmlkgnY0iXNlY3AyNTZrMaECriawHKWdDRk2xeZkrOXBQ0dfMFLHY4eENZwdufn"
            "1S1o"})
    {
        auto record = Enr::fromText(text);
        BOOST_REQUIRE_MESSAGE(record.has_value(), text);
        BOOST_CHECK_MESSAGE(record->verify(), text);
        BOOST_CHECK(!record->ip().has_value());
        BOOST_CHECK_EQUAL(record->toText(), text);
    }
}

BOOST_AUTO_TEST_CASE(base64UrlHelpers)
{
    BOOST_CHECK_EQUAL(base64UrlEncode(bcos::ref(bcos::bytes{})), "");
    BOOST_CHECK_EQUAL(base64UrlEncode(bcos::ref(bcos::bytes{0xfb, 0xff})), "-_8");
    auto decoded = base64UrlDecode("-_8");
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(toHex(*decoded), "fbff");
    BOOST_CHECK(!base64UrlDecode("-_8=").has_value());
    BOOST_CHECK(!base64UrlDecode("+/8").has_value());
    BOOST_CHECK(!base64UrlDecode("A").has_value());
    BOOST_CHECK(base64UrlDecode("")->empty());
    bcos::bytes all(256);
    for (size_t i = 0; i < all.size(); ++i)
    {
        all[i] = static_cast<bcos::byte>(i);
    }
    auto back = base64UrlDecode(base64UrlEncode(bcos::ref(all)));
    BOOST_REQUIRE(back.has_value());
    BOOST_CHECK(*back == all);
}

BOOST_AUTO_TEST_CASE(pointCompression)
{
    rlpx::EccKeyPair keyPair(fromHex(std::string(c_specPrivateKey)));
    auto compressed = compressPublicKey(bcos::ref(keyPair.publicKey()));
    BOOST_CHECK_EQUAL(toHex(compressed), c_specCompressedKey);
    auto back = decompressPublicKey(bcos::ref(compressed));
    BOOST_REQUIRE(back.has_value());
    BOOST_CHECK(*back == keyPair.publicKey());
    bcos::bytes bogus(33, 0x02);
    bogus[0] = 0x05;  // not a valid SEC1 prefix
    BOOST_CHECK(!decompressPublicKey(bcos::ref(bogus)).has_value());
    bcos::bytes shortKey(32, 0x02);
    BOOST_CHECK(!decompressPublicKey(bcos::ref(shortKey)).has_value());
    bcos::bytes zeros(64, 0x00);
    BOOST_CHECK_THROW(compressPublicKey(bcos::ref(zeros)), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
