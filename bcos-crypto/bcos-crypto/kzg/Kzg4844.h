// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file Kzg4844.h
// @brief The single point of contact with c-kzg-4844: all KZG types, constants and calls in
// the tree go through this header, so a backend swap touches one file. The trusted setup is
// the mainnet parameters embedded into libckzg at build time (no runtime file lookup);
// loading happens once behind a function-local static and the returned settings are
// read-only afterwards — c-kzg's compute/verify entry points are documented as safe for
// concurrent use on a shared KZGSettings.
#pragma once

#include <bcos-crypto/hash/Sha256.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>
#include <ckzg.h>
#include <ckzg_embed.h>
#include <cstring>

namespace bcos::crypto::kzg
{
DERIVE_BCOS_EXCEPTION(KzgSetupFailure);

inline constexpr std::size_t BlobSize = BYTES_PER_BLOB;              // 131072
inline constexpr std::size_t CommitmentSize = BYTES_PER_COMMITMENT;  // 48
inline constexpr std::size_t ProofSize = BYTES_PER_PROOF;            // 48
// EIP-7594: the extended blob is split into CELLS_PER_EXT_BLOB cells, each with its own
// KZG proof; a version-1 sidecar carries all of them per blob.
inline constexpr std::size_t CellsPerExtBlob = CELLS_PER_EXT_BLOB;  // 128
// EIP-4844: VERSIONED_HASH_VERSION_KZG prefixes sha256(commitment).
inline constexpr bcos::byte VERSIONED_HASH_VERSION_KZG{0x01};

/// The process-wide trusted setup, loaded from the bytes embedded into libckzg. Precompute
/// level 0: proof generation is slower than geth's tuned tables but the result is identical.
/// Throws KzgSetupFailure when the embedded parameters fail to load — a build defect, never
/// runtime input, so there is no error return.
inline KZGSettings const* kzgSettings()
{
    static KZGSettings* const settings = [] {
        auto* s = new KZGSettings{};
        if (load_trusted_setup_embedded(s, 0) != C_KZG_OK) [[unlikely]]
        {
            delete s;
            bcos::throwTrace(KzgSetupFailure{} << bcos::errinfo_comment(
                                 "c-kzg-4844 embedded trusted setup failed to load"));
        }
        return s;
    }();
    return settings;
}

/// The EIP-4844 versioned hash of a KZG commitment: sha256(commitment) with the first byte
/// replaced by VERSIONED_HASH_VERSION_KZG. Header-only, no c-kzg call.
inline h256 versionedHashFromCommitment(bcos::bytesConstRef commitment)
{
    auto hash = sha256Hash(commitment);
    hash[0] = VERSIONED_HASH_VERSION_KZG;
    return h256(hash);
}

inline bool blobToKzgCommitment(bcos::bytesConstRef blob, bcos::bytes& commitmentOut)
{
    if (blob.size() != BlobSize)
    {
        return false;
    }
    Blob kzgBlob;
    std::memcpy(kzgBlob.bytes, blob.data(), BlobSize);
    KZGCommitment commitment;
    if (::blob_to_kzg_commitment(&commitment, &kzgBlob, kzgSettings()) != C_KZG_OK)
    {
        return false;
    }
    commitmentOut.assign(commitment.bytes, commitment.bytes + CommitmentSize);
    return true;
}

/// The blob-level proof (EIP-4844 compute_blob_kzg_proof): proves the blob matches the
/// commitment. @p commitment must be the 48-byte commitment of @p blob.
inline bool computeBlobKzgProof(
    bcos::bytesConstRef blob, bcos::bytesConstRef commitment, bcos::bytes& proofOut)
{
    if (blob.size() != BlobSize || commitment.size() != CommitmentSize)
    {
        return false;
    }
    Blob kzgBlob;
    std::memcpy(kzgBlob.bytes, blob.data(), BlobSize);
    Bytes48 kzgCommitment;
    std::memcpy(kzgCommitment.bytes, commitment.data(), CommitmentSize);
    KZGProof proof;
    if (::compute_blob_kzg_proof(&proof, &kzgBlob, &kzgCommitment, kzgSettings()) != C_KZG_OK)
    {
        return false;
    }
    proofOut.assign(proof.bytes, proof.bytes + ProofSize);
    return true;
}

inline bool verifyBlobKzgProof(
    bcos::bytesConstRef blob, bcos::bytesConstRef commitment, bcos::bytesConstRef proof)
{
    if (blob.size() != BlobSize || commitment.size() != CommitmentSize || proof.size() != ProofSize)
    {
        return false;
    }
    Blob kzgBlob;
    std::memcpy(kzgBlob.bytes, blob.data(), BlobSize);
    Bytes48 kzgCommitment;
    std::memcpy(kzgCommitment.bytes, commitment.data(), CommitmentSize);
    Bytes48 kzgProof;
    std::memcpy(kzgProof.bytes, proof.data(), ProofSize);
    bool ok = false;
    return ::verify_blob_kzg_proof(&ok, &kzgBlob, &kzgCommitment, &kzgProof, kzgSettings()) ==
               C_KZG_OK &&
           ok;
}

/// Batch form of verifyBlobKzgProof; the three spans must be the same length. An empty
/// batch verifies vacuously (c-kzg returns OK with ok=true).
inline bool verifyBlobKzgProofBatch(std::span<const bcos::bytes> blobs,
    std::span<const bcos::bytes> commitments, std::span<const bcos::bytes> proofs)
{
    if (blobs.size() != commitments.size() || blobs.size() != proofs.size())
    {
        return false;
    }
    std::vector<Blob> kzgBlobs(blobs.size());
    std::vector<Bytes48> kzgCommitments(blobs.size());
    std::vector<Bytes48> kzgProofs(blobs.size());
    for (std::size_t i = 0; i < blobs.size(); ++i)
    {
        if (blobs[i].size() != BlobSize || commitments[i].size() != CommitmentSize ||
            proofs[i].size() != ProofSize)
        {
            return false;
        }
        std::memcpy(kzgBlobs[i].bytes, blobs[i].data(), BlobSize);
        std::memcpy(kzgCommitments[i].bytes, commitments[i].data(), CommitmentSize);
        std::memcpy(kzgProofs[i].bytes, proofs[i].data(), ProofSize);
    }
    bool ok = false;
    return ::verify_blob_kzg_proof_batch(&ok, kzgBlobs.data(), kzgCommitments.data(),
               kzgProofs.data(), blobs.size(), kzgSettings()) == C_KZG_OK &&
           ok;
}

/// EIP-7594 compute_cells_and_kzg_proofs, proofs half: the CellsPerExtBlob cell proofs of
/// @p blob, in cell order. @p proofsOut is replaced.
inline bool computeCellKzgProofs(bcos::bytesConstRef blob, std::vector<bcos::bytes>& proofsOut)
{
    if (blob.size() != BlobSize)
    {
        return false;
    }
    Blob kzgBlob;
    std::memcpy(kzgBlob.bytes, blob.data(), BlobSize);
    std::vector<KZGProof> proofs(CellsPerExtBlob);
    if (::compute_cells_and_kzg_proofs(nullptr, proofs.data(), &kzgBlob, kzgSettings()) != C_KZG_OK)
    {
        return false;
    }
    proofsOut.clear();
    proofsOut.reserve(CellsPerExtBlob);
    for (auto const& proof : proofs)
    {
        proofsOut.emplace_back(proof.bytes, proof.bytes + ProofSize);
    }
    return true;
}

/// EIP-7594 verify_cell_kzg_proof_batch over whole blobs: @p cellProofs holds
/// CellsPerExtBlob proofs per blob, blob-major (the wrapper-version-1 / BlobsBundleV2
/// layout). The cells are recomputed from the blobs here (compute_cells); the commitment of
/// blob i is paired with each of its cells (osaka.md: commitments repeated
/// CELLS_PER_EXT_BLOB times, cell_indices = range(CELLS_PER_EXT_BLOB) per blob). An empty
/// batch verifies vacuously.
inline bool verifyCellKzgProofBatch(std::span<const bcos::bytes> blobs,
    std::span<const bcos::bytes> commitments, std::span<const bcos::bytes> cellProofs)
{
    if (blobs.size() != commitments.size() || cellProofs.size() != blobs.size() * CellsPerExtBlob)
    {
        return false;
    }
    auto const cellCount = cellProofs.size();
    std::vector<Cell> cells(cellCount);
    std::vector<Bytes48> kzgCommitments(cellCount);
    std::vector<Bytes48> kzgProofs(cellCount);
    std::vector<uint64_t> cellIndices(cellCount);
    for (std::size_t i = 0; i < blobs.size(); ++i)
    {
        if (blobs[i].size() != BlobSize || commitments[i].size() != CommitmentSize)
        {
            return false;
        }
        Blob kzgBlob;
        std::memcpy(kzgBlob.bytes, blobs[i].data(), BlobSize);
        if (::compute_cells_and_kzg_proofs(
                cells.data() + i * CellsPerExtBlob, nullptr, &kzgBlob, kzgSettings()) != C_KZG_OK)
        {
            return false;
        }
        for (std::size_t j = 0; j < CellsPerExtBlob; ++j)
        {
            auto const k = i * CellsPerExtBlob + j;
            if (cellProofs[k].size() != ProofSize)
            {
                return false;
            }
            std::memcpy(kzgCommitments[k].bytes, commitments[i].data(), CommitmentSize);
            std::memcpy(kzgProofs[k].bytes, cellProofs[k].data(), ProofSize);
            cellIndices[k] = j;
        }
    }
    bool ok = false;
    return ::verify_cell_kzg_proof_batch(&ok, kzgCommitments.data(), cellIndices.data(),
               cells.data(), kzgProofs.data(), cellCount, kzgSettings()) == C_KZG_OK &&
           ok;
}

/// Whether @p sidecar-shaped lists verify under their wrapper version: version 0 batch
/// blob proofs, version 1 cell proofs. The one entry point the two admission gates (RPC,
/// devp2p gossip) call, so neither can verify a sidecar under the wrong rule.
inline bool verifyBlobSidecarProofs(std::uint8_t sidecarVersion, std::span<const bcos::bytes> blobs,
    std::span<const bcos::bytes> commitments, std::span<const bcos::bytes> proofs)
{
    return sidecarVersion == 1 ? verifyCellKzgProofBatch(blobs, commitments, proofs) :
                                 verifyBlobKzgProofBatch(blobs, commitments, proofs);
}
}  // namespace bcos::crypto::kzg
