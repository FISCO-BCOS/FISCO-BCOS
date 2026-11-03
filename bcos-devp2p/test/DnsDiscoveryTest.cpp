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
 * @file DnsDiscoveryTest.cpp
 * @brief EIP-1459 tests on a fake resolver: the spec example tree, a bad root
 *        signature, resync on a seq bump with the hash cache, and an opt-in live
 *        lookup of geth's Sepolia list (FISCO_DISCV4_LIVE=1).
 * @date 2026/11/3
 */
#include <bcos-devp2p/disc/DnsDiscovery.h>
#include <bcos-devp2p/disc/Enr.h>
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::disc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(DnsDiscoveryTest)

namespace
{
class FakeResolver : public DnsResolverInterface
{
public:
    std::map<std::string, std::vector<std::string>> zone;
    std::map<std::string, size_t> queries;
    std::string failing;  // a name whose lookup throws

    std::vector<std::string> lookupTxt(std::string const& _name) override
    {
        ++queries[_name];
        if (_name == failing)
        {
            throw std::runtime_error("SERVFAIL " + _name);
        }
        auto it = zone.find(_name);
        return it == zone.end() ? std::vector<std::string>{} : it->second;
    }
};

// dnsdisc.md "Example in zone file format". The root is signed by go-ethereum's
// p2p/dnsdisc signingKeyForTesting (client_test.go), whose URL key is AKPYQI...; the
// AM5FCQ... key the markdown quotes is the link target's (morenodes.example.org), and the
// 0x049f88... hex it prints matches neither. The root signature does not cover the
// domain, so geth's "n" tree verifies unchanged under nodes.example.org.
constexpr char const* c_exampleDomain = "nodes.example.org";
constexpr char const* c_exampleUrl =
    "enrtree://AKPYQIUQIL7PSIACI32J7FGZW56E5FKHEFCCOFHILBIMW3M6LWXS2@nodes.example.org";
constexpr std::string_view c_exampleSigningKey =
    "dc599867fc513f8f5e2c2c9c489cde5e71362d1d9ec6e693e0de063236ed1240";
constexpr char const* c_exampleRoot =
    "enrtree-root:v1 e=JWXYDBPXYWG6FX3GMDIBFA6CJ4 l=C7HRFPF3BLGF3YR4DY5KX3SMBE seq=1 "
    "sig=o908WmNp7LibOfPsr4btQwatZJ5URBr2ZAuxvK4UWHlsB9sUOTJQaGAlLPVAhM__XJesCHxLISo94z5Z2a463gA";
constexpr char const* c_exampleLink =
    "enrtree://AM5FCQLWIZX2QFPNJAP7VUERCCRNGRHWZG3YYHIUV7BVDQ5FDPRT2@morenodes.example.org";
constexpr char const* c_exampleBranch =
    "enrtree-branch:2XS2367YHAXJFGLZHVAWLQD4ZY,H4FHT4B454P6UXFD7JCYQ5PWDY,"
    "MHTDO6TMUBRIA2XWG5LUDACK24";
constexpr char const* c_exampleLeaves[] = {
    "enr:-"
    "HW4QOFzoVLaFJnNhbgMoDXPnOvcdVuj7pDpqRvh6BRDO68aVi5ZcjB3vzQRZH2IcLBGHzo8uUN3snqmgTiE56CH3AM"
    "BgmlkgnY0iXNlY3AyNTZrMaECC2_24YYkYHEgdzxlSNKQEnHhuNAbNlMlWJxrJxbAFvA",
    "enr:-"
    "HW4QAggRauloj2SDLtIHN1XBkvhFZ1vtf1raYQp9TBW2RD5EEawDzbtSmlXUfnaHcvwOizhVYLtr7e6vw7NAf6mTuo"
    "CgmlkgnY0iXNlY3AyNTZrMaECjrXI8TLNXU0f8cthpAMxEshUyQlK-AM0PW2wfrnacNI",
    "enr:-HW4QLAYqmrwllBEnzWWs7I5Ev2IAs7x_dZlbYdRdMUx5EyKHDXp7AV5CkuPGUPdvbv1_"
    "Ms1CPfhcGCvSElSosZmyoq"
    "AgmlkgnY0iXNlY3AyNTZrMaECriawHKWdDRk2xeZkrOXBQ0dfMFLHY4eENZwdufn1S1o",
};
constexpr char const* c_exampleLeafHashes[] = {
    "2XS2367YHAXJFGLZHVAWLQD4ZY", "H4FHT4B454P6UXFD7JCYQ5PWDY", "MHTDO6TMUBRIA2XWG5LUDACK24"};

std::shared_ptr<FakeResolver> exampleZone()
{
    auto resolver = std::make_shared<FakeResolver>();
    std::string const domain = c_exampleDomain;
    resolver->zone[domain] = {c_exampleRoot};
    resolver->zone["C7HRFPF3BLGF3YR4DY5KX3SMBE." + domain] = {c_exampleLink};
    resolver->zone["JWXYDBPXYWG6FX3GMDIBFA6CJ4." + domain] = {c_exampleBranch};
    for (size_t i = 0; i < 3; ++i)
    {
        resolver->zone[std::string(c_exampleLeafHashes[i]) + "." + domain] = {c_exampleLeaves[i]};
    }
    return resolver;
}

// A signable tree of our own: records under one branch, no links.
struct TestTree
{
    rlpx::EccKeyPair key;
    std::string domain{"test.local"};
    std::vector<std::string> leaves;
    uint64_t seq{1};

    std::string url() const
    {
        return "enrtree://" +
               base32Encode(bcos::ref(compressPublicKey(bcos::ref(key.publicKey())))) + "@" +
               domain;
    }
    void addRecord(uint16_t _udp)
    {
        rlpx::EccKeyPair nodeKey;
        Enr record;
        record.setSeq(1);
        record.setIp({203, 0, 113, static_cast<uint8_t>(_udp & 0xff)});
        record.setUdp(_udp);
        record.setTcp(_udp);
        record.sign(bcos::ref(nodeKey.privateKey()));
        leaves.push_back(record.toText());
    }
    void publish(FakeResolver& _resolver) const
    {
        std::string branch = "enrtree-branch:";
        for (size_t i = 0; i < leaves.size(); ++i)
        {
            auto hash = enrTreeEntryHash(leaves[i]);
            _resolver.zone[hash + "." + domain] = {leaves[i]};
            branch += (i == 0 ? "" : ",") + hash;
        }
        std::string const linkBranch = "enrtree-branch:";
        auto const enrRoot = enrTreeEntryHash(branch);
        auto const linkRoot = enrTreeEntryHash(linkBranch);
        _resolver.zone[enrRoot + "." + domain] = {branch};
        _resolver.zone[linkRoot + "." + domain] = {linkBranch};
        std::string const content =
            "enrtree-root:v1 e=" + enrRoot + " l=" + linkRoot + " seq=" + std::to_string(seq);
        auto hash = rlpx::keccak256(
            bytesConstRef(reinterpret_cast<bcos::byte const*>(content.data()), content.size()));
        auto signature = rlpx::signRecoverable(bcos::ref(hash), bcos::ref(key.privateKey()));
        _resolver.zone[domain] = {content + " sig=" + base64UrlEncode(bcos::ref(signature))};
    }
};
}  // namespace

BOOST_AUTO_TEST_CASE(base32AndUrlParsing)
{
    BOOST_CHECK_EQUAL(base32Encode(bcos::ref(bcos::bytes{})), "");
    BOOST_CHECK_EQUAL(base32Encode(bcos::ref(bcos::bytes{'f', 'o', 'o', 'b', 'a', 'r'})),
        "MZXW6YTBOI");  // RFC 4648 vector without padding
    auto decoded = base32Decode("MZXW6YTBOI");
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(std::string(decoded->begin(), decoded->end()), "foobar");
    BOOST_CHECK(!base32Decode("MZXW6YTBOI=").has_value());
    BOOST_CHECK(!base32Decode("1").has_value());
    BOOST_CHECK(!base32Decode("M").has_value());

    auto link = parseEnrTreeUrl(c_exampleUrl);
    BOOST_REQUIRE(link.has_value());
    BOOST_CHECK_EQUAL(link->domain, "nodes.example.org");
    rlpx::EccKeyPair signingKey(fromHex(std::string(c_exampleSigningKey)));
    BOOST_CHECK(link->publicKey == signingKey.publicKey());
    // Round trip through the base32 key form.
    BOOST_CHECK_EQUAL(
        "enrtree://" +
            base32Encode(bcos::ref(compressPublicKey(bcos::ref(signingKey.publicKey())))) +
            "@nodes.example.org",
        c_exampleUrl);
    auto morenodes = parseEnrTreeUrl(c_exampleLink);
    BOOST_REQUIRE(morenodes.has_value());
    BOOST_CHECK_EQUAL(morenodes->domain, "morenodes.example.org");
    BOOST_CHECK_EQUAL(morenodes->publicKey.size(), 64u);
    BOOST_CHECK(!parseEnrTreeUrl("enrtree://nodes.example.org").has_value());
    BOOST_CHECK(!parseEnrTreeUrl("enrtree://AAAA@nodes.example.org").has_value());
    BOOST_CHECK(!parseEnrTreeUrl("https://x@y").has_value());
}

BOOST_AUTO_TEST_CASE(rootAndEntryHashes)
{
    auto root = parseEnrTreeRoot(c_exampleRoot);
    BOOST_REQUIRE(root.has_value());
    BOOST_CHECK_EQUAL(root->enrRoot, "JWXYDBPXYWG6FX3GMDIBFA6CJ4");
    BOOST_CHECK_EQUAL(root->linkRoot, "C7HRFPF3BLGF3YR4DY5KX3SMBE");
    BOOST_CHECK_EQUAL(root->seq, 1u);
    BOOST_CHECK_EQUAL(root->signature.size(), 65u);
    auto link = parseEnrTreeUrl(c_exampleUrl);
    BOOST_REQUIRE(link.has_value());
    BOOST_CHECK(verifyEnrTreeRoot(*root, bcos::ref(link->publicKey)));
    // Tampered seq / foreign key fail.
    auto tampered = *root;
    tampered.seq = 2;
    BOOST_CHECK(!verifyEnrTreeRoot(tampered, bcos::ref(link->publicKey)));
    rlpx::EccKeyPair other;
    BOOST_CHECK(!verifyEnrTreeRoot(*root, bcos::ref(other.publicKey())));
    BOOST_CHECK(!parseEnrTreeRoot("enrtree-root:v1 e=X l=Y seq=1 sig=AA").has_value());
    BOOST_CHECK(
        !parseEnrTreeRoot("enrtree-root:v2 e=JWXYDBPXYWG6FX3GMDIBFA6CJ4 "
                          "l=C7HRFPF3BLGF3YR4DY5KX3SMBE seq=1 sig=AA")
            .has_value());
    // Subdomain hashes of the example entries.
    BOOST_CHECK_EQUAL(enrTreeEntryHash(c_exampleBranch), "JWXYDBPXYWG6FX3GMDIBFA6CJ4");
    BOOST_CHECK_EQUAL(enrTreeEntryHash(c_exampleLink), "C7HRFPF3BLGF3YR4DY5KX3SMBE");
    for (size_t i = 0; i < 3; ++i)
    {
        BOOST_CHECK_EQUAL(enrTreeEntryHash(c_exampleLeaves[i]), c_exampleLeafHashes[i]);
    }
}

BOOST_AUTO_TEST_CASE(syncExampleTree)
{
    auto resolver = exampleZone();
    std::vector<Enr> delivered;
    DnsDiscovery discovery(
        {c_exampleUrl}, resolver, [&](Enr const& _record) { delivered.push_back(_record); });
    auto report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 3u);
    BOOST_CHECK_EQUAL(report.links, 1u);
    BOOST_CHECK_EQUAL(report.trees, 2u);   // the linked morenodes.example.org was tried
    BOOST_CHECK_EQUAL(report.errors, 1u);  // ... and has no root in the fake zone
    BOOST_CHECK_EQUAL(report.lastSeq, 1u);
    BOOST_CHECK_EQUAL(delivered.size(), 3u);
    BOOST_CHECK_EQUAL(discovery.records().size(), 3u);
    for (auto const& record : discovery.records())
    {
        BOOST_CHECK(record.verify());
    }
    BOOST_CHECK_EQUAL(resolver->queries["morenodes.example.org"], 1u);
    // Unchanged seq: the second pass touches only the roots.
    auto before = resolver->queries;
    report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 0u);
    BOOST_CHECK_EQUAL(report.trees, 2u);
    BOOST_CHECK_EQUAL(delivered.size(), 3u);
    BOOST_CHECK_EQUAL(resolver->queries[c_exampleDomain], before[c_exampleDomain] + 1);
    BOOST_CHECK_EQUAL(resolver->queries["JWXYDBPXYWG6FX3GMDIBFA6CJ4.nodes.example.org"],
        before["JWXYDBPXYWG6FX3GMDIBFA6CJ4.nodes.example.org"]);
}

BOOST_AUTO_TEST_CASE(badRootSignatureIsRejected)
{
    auto resolver = exampleZone();
    std::string bad = c_exampleRoot;
    auto const sigAt = bad.find("sig=") + 4 + 10;  // inside r, not in the padding bits
    bad[sigAt] = (bad[sigAt] == 'A') ? 'B' : 'A';
    resolver->zone[c_exampleDomain] = {bad};
    size_t sinkCalls = 0;
    DnsDiscovery discovery({c_exampleUrl}, resolver, [&](Enr const&) { ++sinkCalls; });
    auto report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 0u);
    BOOST_CHECK_EQUAL(report.errors, 1u);
    BOOST_CHECK_EQUAL(sinkCalls, 0u);
    BOOST_CHECK(discovery.records().empty());
    // No leaf was fetched under an unverified root.
    BOOST_CHECK_EQUAL(resolver->queries["JWXYDBPXYWG6FX3GMDIBFA6CJ4.nodes.example.org"], 0u);
    // A root signed by a different key than the URL's is rejected too.
    TestTree foreign;
    foreign.domain = c_exampleDomain;
    foreign.addRecord(30303);
    foreign.publish(*resolver);  // overwrites the root with one signed by `foreign.key`
    report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 0u);
    BOOST_CHECK_EQUAL(report.errors, 1u);
    // Resolver failures surface as errors, not exceptions.
    resolver->failing = c_exampleDomain;
    report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.errors, 1u);
    BOOST_CHECK(!DnsDiscovery({"enrtree://bad"}, resolver).syncOnce().trees);
}

BOOST_AUTO_TEST_CASE(resyncOnSeqChangeUsesCache)
{
    auto resolver = std::make_shared<FakeResolver>();
    TestTree tree;
    tree.addRecord(30301);
    tree.addRecord(30302);
    tree.publish(*resolver);
    size_t sinkCalls = 0;
    DnsDiscovery discovery({tree.url()}, resolver, [&](Enr const&) { ++sinkCalls; });
    auto report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 2u);
    BOOST_CHECK_EQUAL(report.errors, 0u);
    BOOST_CHECK_EQUAL(report.lastSeq, 1u);
    BOOST_CHECK_EQUAL(sinkCalls, 2u);

    // Same content republished under the same seq: nothing happens.
    tree.publish(*resolver);
    BOOST_CHECK_EQUAL(discovery.syncOnce().records, 0u);
    // A lower seq is ignored as well (rollback protection).
    tree.seq = 0;
    tree.publish(*resolver);
    BOOST_CHECK_EQUAL(discovery.syncOnce().records, 0u);

    // seq bump with one new leaf: only the new leaf and the new branch are fetched; the
    // two old leaves come from the hash cache.
    tree.seq = 5;
    tree.addRecord(30303);
    tree.publish(*resolver);
    auto before = resolver->queries;
    report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 1u);
    BOOST_CHECK_EQUAL(report.lastSeq, 5u);
    BOOST_CHECK_EQUAL(sinkCalls, 3u);
    BOOST_CHECK_EQUAL(discovery.records().size(), 3u);
    for (size_t i = 0; i < 2; ++i)
    {
        auto name = enrTreeEntryHash(tree.leaves[i]) + "." + tree.domain;
        BOOST_CHECK_EQUAL(resolver->queries[name], before[name]);
    }
    BOOST_CHECK_EQUAL(resolver->queries[enrTreeEntryHash(tree.leaves[2]) + "." + tree.domain], 1u);
    // A leaf served under the wrong hash name is ignored.
    tree.seq = 6;
    rlpx::EccKeyPair nodeKey;
    Enr bogus;
    bogus.setSeq(1);
    bogus.setIp({203, 0, 113, 9});
    bogus.setUdp(30309);
    bogus.sign(bcos::ref(nodeKey.privateKey()));
    tree.leaves.push_back(bogus.toText());
    tree.publish(*resolver);
    resolver->zone[enrTreeEntryHash(bogus.toText()) + "." + tree.domain] = {tree.leaves[0]};
    report = discovery.syncOnce();
    BOOST_CHECK_EQUAL(report.records, 0u);
    BOOST_CHECK_EQUAL(discovery.records().size(), 3u);
    BOOST_CHECK_EQUAL(discovery.lastReport().lastSeq, 6u);
    // start/stop on the thread form runs a pass too.
    discovery.start(std::chrono::seconds(3600));
    discovery.stop();
    BOOST_CHECK(!discovery.running());
}

// Opt-in: FISCO_DISCV4_LIVE=1 resolves geth's Sepolia list through libresolv.
BOOST_AUTO_TEST_CASE(liveSepoliaList)
{
    if (std::getenv("FISCO_DISCV4_LIVE") == nullptr)
    {
        return;
    }
    DnsDiscovery discovery(
        {"enrtree://"
         "AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net"},
        std::make_shared<ResolvDnsResolver>());
    auto report = discovery.syncOnce();
    std::cout << "[live-dns] trees=" << report.trees << " records=" << report.records
              << " links=" << report.links << " errors=" << report.errors
              << " seq=" << report.lastSeq << std::endl;
    size_t withEth = 0;
    for (auto const& record : discovery.records())
    {
        withEth += record.eth().has_value();
    }
    std::cout << "[live-dns] records with eth entry=" << withEth << std::endl;
    BOOST_CHECK_MESSAGE(report.records > 0, "no records: DNS blocked or list unreachable");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
