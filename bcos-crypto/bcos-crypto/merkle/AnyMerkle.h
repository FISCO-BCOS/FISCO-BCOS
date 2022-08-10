#pragma once

#include "Merkle.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <variant>

namespace bcos::crypto::merkle
{
class AnyMerkle
{
public:
    template <class Merkle>
    AnyMerkle(Merkle merkle) : m_merkle(std::move(merkle))
    {}
    AnyMerkle(const AnyMerkle&) = default;
    AnyMerkle(AnyMerkle&&) = default;
    AnyMerkle& operator=(const AnyMerkle&) = default;
    AnyMerkle& operator=(AnyMerkle&&) = default;

    bool verifyMerkleProof(ProofRange auto const& proof, bcos::concepts::bytebuffer::Hash auto hash,
        bcos::concepts::bytebuffer::Hash auto const& root)
    {
        bool result = false;
        std::visit(
            [&result, &proof, m_hash = std::move(hash), &root](auto& merkleObj) {
                result = merkleObj.verifyMerkleProof(proof, std::move(m_hash), root);
            },
            m_merkle);
        return result;
    }

    void generateMerkleProof(HashRange auto const& originHashes, MerkleRange auto const& merkle,
        bcos::concepts::bytebuffer::Hash auto const& hash, ProofRange auto& out) const
    {
        std::visit(
            [&originHashes, &merkle, &hash, &out](auto& merkleObj) {
                merkleObj.generateMerkleProof(originHashes, merkle, hash, out);
            },
            m_merkle);
    }

    void generateMerkleProof(HashRange auto const& originHashes, MerkleRange auto const& merkle,
        std::integral auto index, ProofRange auto& out) const
    {
        std::visit(
            [&originHashes, &merkle, index, &out](auto& merkleObj) {
                merkleObj.generateMerkleProof(originHashes, merkle, index, out);
            },
            m_merkle);
    }

    void generateMerkle(HashRange auto const& originHashes, MerkleRange auto& out) const
    {
        std::visit(
            [&originHashes, &out](auto& merkleObj) { merkleObj.generateMerkle(originHashes, out); },
            m_merkle);
    }

private:
    std::variant<bcos::crypto::merkle::Merkle<crypto::hasher::openssl::OpenSSL_Keccak256_Hasher, 2>,
        bcos::crypto::merkle::Merkle<crypto::hasher::openssl::OpenSSL_SM3_Hasher, 2>>
        m_merkle;
};
}  // namespace bcos::crypto::merkle