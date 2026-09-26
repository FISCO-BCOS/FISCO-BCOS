// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file Kzg4844Test.cpp
// @brief Closed-loop and known-answer tests for the c-kzg-4844 isolation layer
// (bcos-crypto/kzg/Kzg4844.h). Built only under FULLNODE (the c-kzg-4844 port is a
// fullnode manifest feature); compiles to an empty TU otherwise.
#ifdef BCOS_CRYPTO_WITH_KZG

#include <bcos-crypto/kzg/Kzg4844.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto::kzg;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(Kzg4844Test, TestPromptFixture)

BOOST_AUTO_TEST_CASE(commitmentAndVersionedHashKnownAnswer)
{
    // The all-zero blob is the zero polynomial: its commitment is the BLS12-381 point at
    // infinity, canonically encoded as 0xc000...00 (the well-known "empty" commitment),
    // and its versioned hash is the value L1 clients hard-code for empty blob lists.
    bytes const zeroBlob(BlobSize, byte{0});
    bytes commitment;
    BOOST_REQUIRE(blobToKzgCommitment(ref(zeroBlob), commitment));
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(ref(commitment)),
        "0xc00000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000");
    auto const versionedHash = versionedHashFromCommitment(ref(commitment));
    BOOST_CHECK_EQUAL(versionedHash.hexPrefixed(),
        "0x010657f37554c781402a22917dee2f75def7ab966d7b770905398eba3c444014");
}

BOOST_AUTO_TEST_CASE(commitProveVerifyRoundtrip)
{
    bytes blob(BlobSize, byte{0});
    // Some non-trivial field elements (each 32-byte word must stay below the BLS scalar
    // field modulus; small values are always valid).
    blob[0] = byte{1};
    blob[31] = byte{7};
    blob[64] = byte{42};

    bytes commitment;
    BOOST_REQUIRE(blobToKzgCommitment(ref(blob), commitment));
    BOOST_CHECK_EQUAL(commitment.size(), CommitmentSize);
    BOOST_CHECK_EQUAL(versionedHashFromCommitment(ref(commitment))[0],
        VERSIONED_HASH_VERSION_KZG);

    bytes proof;
    BOOST_REQUIRE(computeBlobKzgProof(ref(blob), ref(commitment), proof));
    BOOST_CHECK_EQUAL(proof.size(), ProofSize);
    BOOST_CHECK(verifyBlobKzgProof(ref(blob), ref(commitment), ref(proof)));

    // A tampered blob / commitment / proof must NOT verify.
    auto tamperedBlob = blob;
    tamperedBlob[100] = byte{9};
    BOOST_CHECK(!verifyBlobKzgProof(ref(tamperedBlob), ref(commitment), ref(proof)));
    auto tamperedCommitment = commitment;
    tamperedCommitment[47] ^= byte{1};
    BOOST_CHECK(!verifyBlobKzgProof(ref(blob), ref(tamperedCommitment), ref(proof)));
    auto tamperedProof = proof;
    tamperedProof[47] ^= byte{1};
    BOOST_CHECK(!verifyBlobKzgProof(ref(blob), ref(commitment), ref(tamperedProof)));
}

BOOST_AUTO_TEST_CASE(batchVerify)
{
    std::vector<bytes> blobs(3), commitments(3), proofs(3);
    for (std::size_t i = 0; i < blobs.size(); ++i)
    {
        blobs[i].assign(BlobSize, byte{0});
        blobs[i][0] = byte{static_cast<uint8_t>(i + 1)};
        BOOST_REQUIRE(blobToKzgCommitment(ref(blobs[i]), commitments[i]));
        BOOST_REQUIRE(computeBlobKzgProof(ref(blobs[i]), ref(commitments[i]), proofs[i]));
    }
    BOOST_CHECK(verifyBlobKzgProofBatch(blobs, commitments, proofs));

    // One bad proof fails the whole batch; a length mismatch is rejected outright.
    proofs[1][10] ^= byte{1};
    BOOST_CHECK(!verifyBlobKzgProofBatch(blobs, commitments, proofs));
    std::vector<bytes> shortCommitments(commitments.begin(), commitments.end() - 1);
    BOOST_CHECK(!verifyBlobKzgProofBatch(blobs, shortCommitments, proofs));

    // The empty batch verifies vacuously.
    std::vector<bytes> none;
    BOOST_CHECK(verifyBlobKzgProofBatch(none, none, none));
}

BOOST_AUTO_TEST_CASE(rejectsWrongSizes)
{
    bytes commitment, proof;
    bytes shortBlob(1024, byte{0});
    BOOST_CHECK(!blobToKzgCommitment(ref(shortBlob), commitment));
    bytes blob(BlobSize, byte{0});
    BOOST_CHECK(!computeBlobKzgProof(ref(blob), ref(shortBlob), proof));
    BOOST_CHECK(!verifyBlobKzgProof(ref(blob), ref(shortBlob), ref(shortBlob)));
    BOOST_CHECK(!verifyBlobKzgProof(ref(shortBlob), ref(shortBlob), ref(shortBlob)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
#endif  // BCOS_CRYPTO_WITH_KZG
