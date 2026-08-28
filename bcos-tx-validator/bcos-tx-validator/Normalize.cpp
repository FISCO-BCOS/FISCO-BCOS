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
 * @file Normalize.cpp
 * @date 2026/8/25
 */

#include "bcos-tx-validator/Normalize.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"
#include "bcos-utilities/BoostLog.h"
#include <boost/exception/diagnostic_information.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::txvalidator
{
namespace
{
constexpr auto kBcosType = static_cast<uint8_t>(TransactionType::BCOSTransaction);
constexpr auto kWeb3Type = static_cast<uint8_t>(TransactionType::Web3Transaction);

/// keccak256 of the re-assembled signed transaction, i.e. the canonical Ethereum tx hash.
/// Derived only from the envelope and the signature, never from the mirror -- which is the whole
/// point: it is a value the forger cannot move without invalidating the signature.
std::optional<crypto::HashType> canonicalHash(Transaction const& tx)
{
    try
    {
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx.extraTransactionBytes(), tx.signatureData());
        return crypto::keccak256Hash(bcos::ref(raw));
    }
    catch (std::exception const& e)
    {
        // A malformed envelope or a signature that is not 65 bytes. Both mean the transaction
        // cannot be authenticated at all.
        BCOS_LOG(DEBUG) << LOG_BADGE("TXVALIDATOR") << LOG_DESC("canonical hash recompute failed")
                        << LOG_KV("reason", boost::diagnostic_information(e));
        return std::nullopt;
    }
}
}  // namespace

TransactionStatus normalize(Transaction& tx)
{
    // Step 0 -- outer type whitelist. An outer `type` outside the known set is neither a valid
    // BCOS transaction nor something to run Web3 normalization on; letting it through would
    // carry undefined semantics down the whole pipeline.
    auto const outerType = tx.type();
    if (outerType != kBcosType && outerType != kWeb3Type) [[unlikely]]
    {
        return TransactionStatus::Malformed;
    }
    if (outerType == kBcosType)
    {
        // dataHash already covers the entire TransactionData: signature and execution read the
        // same bytes, so there is no mirror to reconcile.
        return TransactionStatus::None;
    }

    // Step 1 -- classify the SIGNED envelope. Before the decode on purpose: the decoder's
    // magic_enum::enum_cast does not know 0x7e, so a deposit would come back as Malformed rather
    // than TxTypeNotSupported. 0x03 has the opposite problem -- it IS in the enum (EIP4844), so
    // the decoder would happily accept it. Neither is fixable by tuning decoder error codes.
    auto const payload = tx.extraTransactionBytes();
    switch (engine::dispatchRawTransaction(payload))
    {
    case engine::RawTransactionKind::Blob:
    case engine::RawTransactionKind::Deposit:
        // Deposits reach a node through the Engine newPayload path, where their trust anchor is
        // sourceHash. One arriving through a transaction pool is forged by construction: it
        // carries no signature and the pool never checks sourceHash.
        return TransactionStatus::TxTypeNotSupported;
    case engine::RawTransactionKind::Unsupported:
        return TransactionStatus::Malformed;
    default:
        break;
    }

    // Step 2 -- the wire-supplied hash, before anything can overwrite it.
    //
    // Pointer cast, not a reference cast: normalize() is reachable from admit(), whose contract
    // is "return a status; throw only when the data needed to decide is unavailable". A
    // reference cast against a Transaction that is not the tars implementation (another
    // implementation, a test double) throws std::bad_cast, and neither admit() nor
    // TransactionSync::importDownloadedTxs catches it -- a type mismatch would terminate the
    // process instead of rejecting one transaction. The same pointer is reused for the commit
    // below, so the cast happens once.
    auto* impl = dynamic_cast<bcostars::protocol::TransactionImpl*>(&tx);
    if (impl == nullptr) [[unlikely]]
    {
        // Web3 normalization needs the tars mirror; without it there is nothing to reconcile
        // the signed envelope against, so fail closed rather than admit an unchecked mirror.
        return TransactionStatus::Malformed;
    }
    auto const wireHash = impl->inner().extraTransactionHash;

    // Step 3 -- decode the envelope. This is the authoritative content of the transaction.
    rpc::Web3Transaction decoded;
    auto payloadRef = bcos::bytesRef(const_cast<bcos::byte*>(payload.data()), payload.size());
    if (auto error = codec::rlp::decodeFromPayload(payloadRef, decoded); error != nullptr)
        [[unlikely]]
    {
        return TransactionStatus::Malformed;
    }

    // Step 4 -- recompute and compare. Still no mutation.
    auto const recomputed = canonicalHash(tx);
    if (!recomputed.has_value()) [[unlikely]]
    {
        return TransactionStatus::InvalidSignature;
    }
    // memcmp, not std::equal: the tars field is vector<tars::Char> (SIGNED char) while the hash
    // is unsigned bytes. An element-wise == promotes both to int, so every byte >= 0x80 compares
    // negative-against-positive and no genuine hash would ever match.
    if (!wireHash.empty() &&
        (wireHash.size() != recomputed->size() ||
            std::memcmp(wireHash.data(), recomputed->data(), wireHash.size()) != 0)) [[unlikely]]
    {
        // The peer committed to a different transaction than the one it sent.
        return TransactionStatus::InvalidSignature;
    }

    // Step 5 -- commit. Everything below is infallible.
    auto& mutableInner = impl->mutableInner();

    // takeToTarsTransaction produces a fresh struct whose `data` holds exactly the envelope's
    // values and leaves every other TransactionData field default-constructed. Moving the whole
    // sub-struct therefore covers both buckets at once -- authoritative fields get the signed
    // value, and the ones a Web3 transaction must not carry (abi, extension, groupID,
    // blockLimit, version, maxFeePerBlobGas, blobVersionedHashes) go back to their defaults.
    // Assigning field by field would silently miss the next field added to TransactionData,
    // which is exactly how #5364 came about.
    auto rebuilt = decoded.takeToTarsTransaction();
    mutableInner.data = std::move(rebuilt.data);
    mutableInner.web3TypedTxKind = rebuilt.web3TypedTxKind;

    // Outer fields that are not covered by the signature and that a Web3 transaction has no
    // legitimate use for. `attribute` is the one with execution consequences: BlockExecutive
    // reads LIQUID_SCALE_CODEC / LIQUID_CREATE to route a transaction into the Liquid path and
    // setCreate, and DAG to change parallel scheduling. takeToTarsTransaction never sets it, so
    // for anything that arrived over RPC this is already 0 and zeroing is a no-op; for anything
    // that arrived over P2P it closes a path to changing execution semantics.
    mutableInner.attribute = 0;
    mutableInner.extraData.clear();
    // Deposit-only mirrors. Deposits are rejected in step 1, so these can only be leftovers.
    mutableInner.sourceHash.clear();
    mutableInner.mint.clear();
    mutableInner.isSystemTransaction = 0;

    mutableInner.extraTransactionHash.assign(recomputed->begin(), recomputed->end());

    // Deliberately preserved: signature (3), importTime (4), sender (7), type (9),
    // extraTransactionBytes (10) -- the authority itself -- and the C++-side pool/consensus
    // bookkeeping, none of which lives in the tars struct.
    return TransactionStatus::None;
}

}  // namespace bcos::txvalidator
