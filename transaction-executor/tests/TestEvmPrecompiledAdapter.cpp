/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TestEvmPrecompiledAdapter.cpp
 * @brief Equivalence pin for EvmPrecompiledAdapter.h: every precompile whose
 *        executor was swapped from the legacy bcos-evm implementation
 *        (PrecompiledRegistrar) to the shared eth::evm implementation
 *        (ethereum-executor/EVMPrecompiles.h) must produce byte-identical
 *        {success, output} results on the same input corpus — including
 *        malformed, truncated and edge-case inputs whose failure-output shape
 *        is consensus-visible (revert data).
 */

#include "../bcos-transaction-executor/precompiled/EvmPrecompiledAdapter.h"
#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string_view>
#include <vector>

using namespace bcos;
using namespace bcos::executor_v1;

namespace
{
bytes randomBytes(size_t size, uint32_t seed)
{
    // Deterministic xorshift32 — no engine dependency, stable across runs.
    bytes out(size, 0);
    uint32_t x = seed ? seed : 1;
    for (auto& b : out)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<byte>(x & 0xff);
    }
    return out;
}

bytes zeroBytes(size_t size)
{
    return bytes(size, byte(0));
}

bytes ffBytes(size_t size)
{
    return bytes(size, byte(0xff));
}

/// 32-byte big-endian encoding of a small value.
bytes be32(uint64_t v)
{
    bytes out(32, byte(0));
    for (int i = 0; i < 8; ++i)
        out[31 - i] = byte((v >> (8 * i)) & 0xff);
    return out;
}

bytes concat(std::initializer_list<bytes> parts)
{
    bytes out;
    for (auto const& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

bytesConstRef refOf(bytes const& in)
{
    return in.empty() ? bytesConstRef{} : bytesConstRef(in.data(), in.size());
}

void checkEquivalent(executor::PrecompiledExecutor const& legacy,
    executor::PrecompiledExecutor const& adapted, bytes const& input, std::string_view what)
{
    auto [oldOk, oldOut] = legacy(refOf(input));
    auto [newOk, newOut] = adapted(refOf(input));
    BOOST_TEST_CONTEXT("precompile case: " << what << " inputSize=" << input.size())
    {
        BOOST_CHECK_EQUAL(oldOk, newOk);
        BOOST_CHECK(oldOut == newOut);
    }
}

void checkCorpus(executor::PrecompiledExecutor const& legacy,
    executor::PrecompiledExecutor const& adapted, std::vector<bytes> corpus, std::string_view name)
{
    for (size_t i = 0; i < corpus.size(); ++i)
    {
        checkEquivalent(legacy, adapted, corpus[i],
            std::string(name) + " #" + std::to_string(i));
    }
}

std::vector<bytes> genericCorpus()
{
    std::vector<bytes> corpus = {{}, zeroBytes(32), ffBytes(32), fromHex("616263")};
    for (size_t size : {1, 31, 32, 33, 64, 96, 127, 128, 129, 160, 191, 192, 193, 213, 255, 256, 384})
    {
        corpus.push_back(randomBytes(size, static_cast<uint32_t>(size * 2654435761u)));
    }
    return corpus;
}

// bn254 G1 generator (1, 2) and the field modulus.
const auto BN254_P = fromHex(
    "30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47");
// p-1 / p+1 (the modulus' low byte is 0x47, no carry/borrow).
bytes const BN254_P_MINUS1 = [] {
    auto v = BN254_P;
    v[31] = v[31] - 1;
    return v;
}();
bytes const BN254_P_PLUS1 = [] {
    auto v = BN254_P;
    v[31] = v[31] + 1;
    return v;
}();
bytes const BN254_G1 = concat({be32(1), be32(2)});
bytes const BN254_G1_NEG = concat({be32(1), [] {
    // p - 2
    auto p = BN254_P;
    p[31] = p[31] - 2;
    return p;
}()});
// Canonical G2 generator (EIP-197 encoding: x_im, x_re, y_im, y_re).
bytes const BN254_G2 = concat({
    fromHex("198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2"),
    fromHex("1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed"),
    fromHex("090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b"),
    fromHex("12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"),
});
// BLS12-381 base field modulus (48 bytes; EIP-2537 pads it left to 64).
const auto BLS12_P = fromHex(
    "1a0111ea397fe69a4b1ba7b6434bacd764774b84f38512bf6730d2a0f6b0f6241eabfffeb153ffffb9feffffff"
    "ffffaaab");
}  // namespace

BOOST_AUTO_TEST_SUITE(EvmPrecompiledAdapterTest)

BOOST_AUTO_TEST_CASE(sha256Equivalence)
{
    auto legacy = executor::PrecompiledRegistrar::executor("sha256");
    auto adapted = adaptEvmPrecompiled(eth_evm::sha256_execute, eth_evm::sha256_analyze, 0);
    checkCorpus(legacy, adapted, genericCorpus(), "sha256");

    // Known-answer sanity: sha256("abc").
    auto [ok, out] = adapted(refOf(fromHex("616263")));
    BOOST_CHECK(ok);
    BOOST_CHECK(out == fromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

BOOST_AUTO_TEST_CASE(ripemd160Equivalence)
{
    auto legacy = executor::PrecompiledRegistrar::executor("ripemd160");
    auto adapted = adaptEvmPrecompiled(eth_evm::ripemd160_execute, eth_evm::ripemd160_analyze, 0);
    checkCorpus(legacy, adapted, genericCorpus(), "ripemd160");
}

BOOST_AUTO_TEST_CASE(identityEquivalence)
{
    auto legacy = executor::PrecompiledRegistrar::executor("identity");
    checkCorpus(legacy, executor::PrecompiledExecutor(identityExecutor), genericCorpus(),
        "identity");
}

BOOST_AUTO_TEST_CASE(blake2Equivalence)
{
    auto legacy = executor::PrecompiledRegistrar::executor("blake2_compression");
    auto adapted =
        adaptEvmPrecompiled(eth_evm::blake2bf_execute, eth_evm::blake2bf_analyze, 0, blake2InputOk);

    std::vector<bytes> corpus = genericCorpus();
    // Valid shape: 213 bytes, rounds=12, f=0 / f=1; and the invalid f=2 / f=0xff.
    for (byte finalBlock : {byte(0), byte(1), byte(2), byte(0xff)})
    {
        auto input = randomBytes(213, 0xB1A4E2u);
        input[0] = input[1] = input[2] = byte(0);
        input[3] = byte(12);  // rounds
        input[212] = finalBlock;
        corpus.push_back(std::move(input));
    }
    // rounds edge values: 0 and 0xffffffff, f=1.
    for (byte roundsMsb : {byte(0), byte(0xff)})
    {
        auto input = randomBytes(213, 0xB1A4E3u);
        input[0] = input[1] = input[2] = input[3] = roundsMsb;
        input[212] = byte(1);
        corpus.push_back(std::move(input));
    }
    checkCorpus(legacy, adapted, std::move(corpus), "blake2");
}

BOOST_AUTO_TEST_CASE(bn254AddMulEquivalence)
{
    auto legacyAdd = executor::PrecompiledRegistrar::executor("alt_bn128_G1_add");
    auto adaptedAdd = adaptEvmPrecompiled(eth_evm::ecadd_execute, eth_evm::ecadd_analyze, 64);
    auto legacyMul = executor::PrecompiledRegistrar::executor("alt_bn128_G1_mul");
    auto adaptedMul = adaptEvmPrecompiled(eth_evm::ecmul_execute, eth_evm::ecmul_analyze, 64);

    std::vector<bytes> corpus = genericCorpus();
    corpus.push_back(concat({BN254_G1, BN254_G1}));              // valid: G1 + G1
    corpus.push_back(zeroBytes(128));                            // infinity + infinity
    corpus.push_back(concat({BN254_G1, zeroBytes(64)}));         // G1 + infinity
    corpus.push_back(concat({BN254_G1, ffBytes(64)}));           // invalid second point
    corpus.push_back(concat({ffBytes(64), BN254_G1}));           // invalid first point
    corpus.push_back(concat({BN254_P, be32(2)}));                // x == p (non-canonical)
    corpus.push_back(concat({BN254_P_PLUS1, be32(2)}));          // x == p+1
    corpus.push_back(concat({be32(1), BN254_P, BN254_G1}));      // y == p
    corpus.push_back(concat({be32(1), be32(1), BN254_G1}));      // first point off curve
    corpus.push_back(concat({BN254_P_MINUS1, BN254_P_MINUS1, BN254_G1}));  // coords p-1
    corpus.push_back(concat({BN254_G1, BN254_G1}) );             // duplicate ok
    {
        auto truncated = concat({BN254_G1, BN254_G1});
        truncated.resize(100);  // right-padded with zeros by the implementation
        corpus.push_back(std::move(truncated));
    }
    checkCorpus(legacyAdd, adaptedAdd, corpus, "bn254add");

    std::vector<bytes> mulCorpus;
    mulCorpus.push_back(concat({BN254_G1, be32(0)}));    // scalar 0 -> infinity
    mulCorpus.push_back(concat({BN254_G1, be32(2)}));    // 2*G1
    mulCorpus.push_back(concat({zeroBytes(64), be32(7)}));
    mulCorpus.push_back(concat({ffBytes(64), be32(2)}));  // invalid point
    mulCorpus.push_back(concat({BN254_P, be32(2), be32(2)}));   // x == p
    mulCorpus.push_back(concat({be32(1), BN254_P, be32(2)}));   // y == p
    mulCorpus.push_back(concat({be32(1), be32(1), be32(2)}));   // off-curve point
    mulCorpus.push_back(concat({BN254_G1, ffBytes(32)}));       // scalar 2^256-1
    mulCorpus.push_back(concat({BN254_G1}));              // truncated (scalar zero-padded)
    mulCorpus.insert(mulCorpus.end(), corpus.begin(), corpus.end());
    checkCorpus(legacyMul, adaptedMul, mulCorpus, "bn254mul");
}

BOOST_AUTO_TEST_CASE(bn254PairingEquivalence)
{
    auto legacy = executor::PrecompiledRegistrar::executor("alt_bn128_pairing_product");
    auto adapted = adaptEvmPrecompiled(eth_evm::ecpairing_execute, eth_evm::ecpairing_analyze, 32);

    // The G2 tail with its first component (x_im) replaced.
    auto g2With = [](bytes const& firstComponent) {
        return concat(
            {firstComponent, bytes(BN254_G2.begin() + 32, BN254_G2.end())});
    };

    std::vector<bytes> corpus = {
        {},                // zero pairs -> true
        zeroBytes(192),    // (infinity, infinity)
        zeroBytes(384),    // two such pairs
        randomBytes(191, 7), randomBytes(193, 8),  // not a whole number of pairs
        randomBytes(192, 9),
        concat({BN254_G1, BN254_G2}),                    // e(G1, G2) != 1 -> 0
        concat({BN254_G1, BN254_G2, BN254_G1_NEG, BN254_G2}),  // e(G1,G2)*e(-G1,G2) == 1
        concat({ffBytes(64), BN254_G2}),                 // invalid G1
        concat({BN254_G1, ffBytes(128)}),                // invalid G2
        // The one structural wrapper difference: the legacy executor validates
        // G1 explicitly (from_bytes + validate) while the shared one raw-loads
        // G1 and relies on pairing_check's internal validation. Sweep the G1
        // rejection surface: non-canonical coordinates, off-curve points.
        concat({BN254_P, be32(2), BN254_G2}),            // G1.x == p
        concat({BN254_P_PLUS1, be32(2), BN254_G2}),      // G1.x == p+1
        concat({be32(1), BN254_P, BN254_G2}),            // G1.y == p
        concat({BN254_P_MINUS1, BN254_P_MINUS1, BN254_G2}),  // G1 coords p-1
        concat({be32(1), be32(1), BN254_G2}),            // G1 off curve: 1^2 != 1^3+3
        concat({zeroBytes(64), BN254_G2}),               // G1 infinity -> valid pair
        concat({BN254_G1, BN254_G2, BN254_P, be32(2), BN254_G2}),  // bad G1 in 2nd pair
        // G2 rejection surface (raw-loaded in both, but pin it anyway).
        concat({BN254_G1, g2With(BN254_P)}),             // G2.x_im == p
        concat({BN254_G1, g2With(ffBytes(32))}),         // G2.x_im == 2^256-1
        concat({BN254_G1, g2With(BN254_P_MINUS1)}),      // G2.x_im == p-1
    };
    checkCorpus(legacy, adapted, std::move(corpus), "bn254pairing");
}

BOOST_AUTO_TEST_CASE(blsEquivalence)
{
    auto legacyG1Add = executor::PrecompiledRegistrar::executor("bls12_g1add");
    auto adaptedG1Add =
        adaptEvmPrecompiled(eth_evm::bls12_g1add_execute, eth_evm::bls12_g1add_analyze, 0);
    auto legacyG1Msm = executor::PrecompiledRegistrar::executor("bls12_g1msm");
    auto adaptedG1Msm = adaptEvmPrecompiled(eth_evm::bls12_g1msm_execute,
        eth_evm::bls12_g1msm_analyze, 0, multipleOf(160));
    auto legacyG2Add = executor::PrecompiledRegistrar::executor("bls12_g2add");
    auto adaptedG2Add =
        adaptEvmPrecompiled(eth_evm::bls12_g2add_execute, eth_evm::bls12_g2add_analyze, 0);
    auto legacyG2Msm = executor::PrecompiledRegistrar::executor("bls12_g2msm");
    auto adaptedG2Msm = adaptEvmPrecompiled(eth_evm::bls12_g2msm_execute,
        eth_evm::bls12_g2msm_analyze, 0, multipleOf(288));
    auto legacyPairing = executor::PrecompiledRegistrar::executor("bls12_pairing_check");
    auto adaptedPairing = adaptEvmPrecompiled(eth_evm::bls12_pairing_check_execute,
        eth_evm::bls12_pairing_check_analyze, 0, multipleOf(384));
    auto legacyMapG1 = executor::PrecompiledRegistrar::executor("bls12_map_fp_to_g1");
    auto adaptedMapG1 = adaptEvmPrecompiled(
        eth_evm::bls12_map_fp_to_g1_execute, eth_evm::bls12_map_fp_to_g1_analyze, 0);
    auto legacyMapG2 = executor::PrecompiledRegistrar::executor("bls12_map_fp2_to_g2");
    auto adaptedMapG2 = adaptEvmPrecompiled(
        eth_evm::bls12_map_fp2_to_g2_execute, eth_evm::bls12_map_fp2_to_g2_analyze, 0);

    // BLS12-381 G1 generator (each Fp element is 48 bytes, left-padded to 64).
    const bytes blsG1 = concat({
        zeroBytes(16),
        fromHex("17f1d3a73197d7942695638c4fa9ac0fc3688c4f9774b905a14e3a3f171bac586c55e83ff97a1ae"
                 "ffb3af00adb22c6bb"),
        zeroBytes(16),
        fromHex("08b3f481e3aaa0f1a09e30ed741d8ae4fcf5e095d5d00af600db18cb2c04b3edd03cc744a288"
                 "8ae40caa232946c5e7e1"),
    });

    // Non-canonical field element: the base field modulus itself (EIP-2537
    // requires every Fp encoding < p; p must be rejected).
    const bytes blsPaddedP = concat({zeroBytes(16), BLS12_P});

    std::vector<bytes> corpus = genericCorpus();
    corpus.push_back(zeroBytes(256));                    // g1add: infinity + infinity
    corpus.push_back(concat({blsG1, zeroBytes(128)}));   // g1add: G1 + infinity
    corpus.push_back(concat({blsG1, blsG1}));            // g1add: G1 + G1
    corpus.push_back(concat({blsG1, ffBytes(128)}));     // g1add: invalid point
    corpus.push_back(concat({blsPaddedP, zeroBytes(192)}));  // g1add: x1 == p
    checkCorpus(legacyG1Add, adaptedG1Add, corpus, "bls_g1add");

    std::vector<bytes> msmCorpus = {
        {}, randomBytes(159, 11), randomBytes(161, 12),  // not whole pairs
        zeroBytes(160),                                  // (infinity, scalar 0)
        concat({blsG1, be32(2)}),                        // 2*G1 via MSM
        zeroBytes(320),                                  // two pairs
        concat({ffBytes(128), be32(1)}),                 // invalid point
        concat({blsPaddedP, be32(1)}),                   // point x == p
    };
    checkCorpus(legacyG1Msm, adaptedG1Msm, msmCorpus, "bls_g1msm");

    std::vector<bytes> g2Corpus = genericCorpus();
    g2Corpus.push_back(zeroBytes(512));  // g2add: infinity + infinity
    g2Corpus.push_back(randomBytes(512, 13));
    checkCorpus(legacyG2Add, adaptedG2Add, g2Corpus, "bls_g2add");

    std::vector<bytes> g2MsmCorpus = {
        {}, randomBytes(287, 14), zeroBytes(288), zeroBytes(576), randomBytes(288, 15)};
    checkCorpus(legacyG2Msm, adaptedG2Msm, g2MsmCorpus, "bls_g2msm");

    std::vector<bytes> pairingCorpus = {
        {}, randomBytes(383, 16), zeroBytes(384), zeroBytes(768), randomBytes(384, 17)};
    checkCorpus(legacyPairing, adaptedPairing, pairingCorpus, "bls_pairing");

    std::vector<bytes> mapCorpus = genericCorpus();
    mapCorpus.push_back(zeroBytes(64));   // map_fp_to_g1: u=0
    mapCorpus.push_back(zeroBytes(128));  // map_fp2_to_g2: (0, 0)
    mapCorpus.push_back(blsPaddedP);      // fp == p (non-canonical)
    mapCorpus.push_back(concat({blsPaddedP, zeroBytes(64)}));  // fp2.c0 == p
    checkCorpus(legacyMapG1, adaptedMapG1, mapCorpus, "bls_map_g1");
    checkCorpus(legacyMapG2, adaptedMapG2, mapCorpus, "bls_map_g2");
}

BOOST_AUTO_TEST_CASE(p256Equivalence)
{
    auto legacy = executor::PrecompiledRegistrar::executor("p256verify");
    auto adapted =
        adaptEvmPrecompiled(eth_evm::p256verify_execute, eth_evm::p256verify_analyze, 0);
    checkCorpus(legacy, adapted,
        {{}, zeroBytes(160), ffBytes(160), randomBytes(159, 21), randomBytes(160, 22),
            randomBytes(161, 23), zeroBytes(32)},
        "p256verify");
}

BOOST_AUTO_TEST_CASE(unsampledExecutorsUnchanged)
{
    // ecrecover (0x01) and modexp (0x05) deliberately keep the legacy bcos-evm
    // executors (edge semantics differ — see EvmPrecompiledAdapter.h). Pin the
    // wiring: the manager's entries must produce exactly the registrar's results.
    PrecompiledManager manager(std::make_shared<crypto::Keccak256>());

    auto checkManager = [&](unsigned long address, char const* name, bytes const& input) {
        auto const* entry = manager.getPrecompiled(address);
        BOOST_REQUIRE(entry != nullptr);
        auto const& contract = std::get<executor::PrecompiledContract>(entry->m_precompiled);
        auto [expectedOk, expectedOut] =
            executor::PrecompiledRegistrar::executor(name)(refOf(input));
        auto [actualOk, actualOut] = contract.execute(refOf(input));
        BOOST_CHECK_EQUAL(expectedOk, actualOk);
        BOOST_CHECK(expectedOut == actualOut);
    };

    for (auto const& input : {zeroBytes(128), randomBytes(128, 31), randomBytes(96, 32),
             randomBytes(200, 33), zeroBytes(96)})
    {
        checkManager(1, "ecrecover", input);
        checkManager(5, "modexp", input);
    }
}

BOOST_AUTO_TEST_CASE(swappedExecutorsEquivalentThroughManager)
{
    // The per-precompile cases above construct their own adapters; this case
    // drives the swapped addresses (0x02-0x04, 0x06-0x09) through the
    // production PrecompiledManager wiring against the legacy registrar
    // executors, so the registered execute/analyze pairs and the
    // analyze-derived buffer sizes are what get compared.
    PrecompiledManager manager(std::make_shared<crypto::Keccak256>());

    auto checkManager = [&](unsigned long address, char const* name, bytes const& input) {
        auto const* entry = manager.getPrecompiled(address);
        BOOST_REQUIRE(entry != nullptr);
        auto const& contract = std::get<executor::PrecompiledContract>(entry->m_precompiled);
        auto [expectedOk, expectedOut] =
            executor::PrecompiledRegistrar::executor(name)(refOf(input));
        auto [actualOk, actualOut] = contract.execute(refOf(input));
        BOOST_TEST_CONTEXT("address 0x" << std::hex << address << " inputSize=" << std::dec
                                        << input.size())
        {
            BOOST_CHECK_EQUAL(expectedOk, actualOk);
            BOOST_CHECK(expectedOut == actualOut);
        }
    };

    std::vector<bytes> corpus = genericCorpus();
    corpus.push_back(concat({BN254_G1, BN254_G1}));   // ecadd/ecmul shaped
    corpus.push_back(concat({BN254_P, be32(2)}));     // non-canonical x
    corpus.push_back(zeroBytes(213));                 // blake2 shaped (f=0)
    for (auto const& input : corpus)
    {
        checkManager(2, "sha256", input);
        checkManager(3, "ripemd160", input);
        checkManager(4, "identity", input);
        checkManager(6, "alt_bn128_G1_add", input);
        checkManager(7, "alt_bn128_G1_mul", input);
        checkManager(9, "blake2_compression", input);
    }

    // Pairing inputs must be a whole number of 192-byte pairs to leave the
    // failure path, so it gets its own corpus.
    for (auto const& input :
        {bytes{}, zeroBytes(192), zeroBytes(384), randomBytes(191, 41), randomBytes(192, 42),
            concat({BN254_G1, BN254_G2}), concat({BN254_G1, BN254_G2, BN254_G1_NEG, BN254_G2}),
            concat({BN254_P, be32(2), BN254_G2}), concat({zeroBytes(64), BN254_G2})})
    {
        checkManager(8, "alt_bn128_pairing_product", input);
    }
}

BOOST_AUTO_TEST_SUITE_END()
