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
        // dataHash covers TransactionData (field 1), so the fields the signature protects and
        // the fields execution reads are the same bytes there.
        //
        // It does NOT cover the outer fields. `attribute` (field 5) in particular sits outside
        // it and BlockExecutive reads it to route into the Liquid path and to change DAG
        // scheduling -- the same field this function zeroes for Web3 below. That is a
        // pre-existing exposure for BCOS transactions, not one this module introduces, and it is
        // left alone deliberately: the SDK legitimately sets `attribute` on BCOS transactions,
        // so zeroing it here would break them. Stated rather than papered over.
        return TransactionStatus::None;
    }

    // Step 1 -- classify the SIGNED envelope. Before the decode on purpose: the decoder's
    // magic_enum::enum_cast does not know 0x7e, so a deposit would come back as Malformed rather
    // than TxTypeNotSupported. 0x03 has the opposite problem -- it IS in the enum (EIP4844), so
    // the decoder would happily accept it instead of reporting BlobTxNotAllowed. Neither is
    // fixable by tuning decoder error codes.
    auto const payload = tx.extraTransactionBytes();
    switch (engine::dispatchRawTransaction(payload))
    {
    case engine::RawTransactionKind::Blob:
        // Same classifier and same verdict as the pool-side gate #5520 added, so the same code:
        // a blob envelope is well-formed and simply never admitted here.
        return TransactionStatus::BlobTxNotAllowed;
    case engine::RawTransactionKind::Deposit:
        // Deposits reach a node through the Engine newPayload path, where their trust anchor is
        // sourceHash. One arriving through a transaction pool is forged by construction: it
        // carries no signature and the pool never checks sourceHash.
        //
        // #5520's gate reports this as InvalidChainId, reached through the chainId classifier
        // rather than this one. That is a less accurate answer -- its own comment says deposits
        // have no chainId -- so this reports the type, not a chainId that was never there. The
        // wiring PR that replaces that gate carries the change in observable status.
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
        //
        // The only branch here that logs, and the only one that is not about the input: every
        // other rejection returns a distinct status the caller reports. This one means some
        // caller is passing a Transaction implementation this module cannot read, which would
        // otherwise present as every transaction failing as Malformed with nothing to debug.
        BCOS_LOG(WARNING) << LOG_BADGE("TXVALIDATOR")
                          << LOG_DESC("normalize() got a non-tars Transaction; wiring error");
        return TransactionStatus::Malformed;
    }
    auto const wireHash = impl->inner().extraTransactionHash;

    // Step 3 -- decode the envelope. This is the authoritative content of the transaction.
    // Decoded from a COPY, not through a const_cast over tx.extraTransactionBytes(). That
    // buffer is the only thing the signature covers, and decodeFromPayload takes a mutable
    // bytesRef because decodeHeader crops the header off the cursor as it parses --
    // reassembleWeb3RawTransaction copies for the same reason and says so. The copy is paid for
    // again inside canonicalHash() on the next line; this adds one buffer, not a new order of
    // cost.
    rpc::Web3Transaction decoded;
    bcos::bytes preimage(payload.begin(), payload.end());
    auto payloadRef = bcos::bytesRef(preimage.data(), preimage.size());
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
    // Dead for Web3 -- hash() reads extraTransactionHash, not this -- but it is an arbitrary,
    // unsigned, uncharged byte string that would otherwise ride into a block and into storage.
    // Cleared rather than left, so that every one of the 14 outer fields is accounted for below.
    mutableInner.dataHash.clear();

    // Every outer field of bcostars::Transaction is accounted for, because the argument for
    // rebuilding rather than field-by-field assignment is that enumeration is what rots:
    //
    //   rebuilt from the envelope: data (1), extraTransactionHash (11), web3TypedTxKind (12)
    //   reset:                     attribute (5), extraData (8), dataHash (2), sourceHash (13),
    //                              mint (14), isSystemTransaction (15)
    //   preserved:                 signature (3), importTime (4), sender (7), type (9),
    //                              extraTransactionBytes (10) -- the authority itself
    //
    // plus the C++-side pool/consensus bookkeeping, none of which lives in the tars struct.
    return TransactionStatus::None;
}

}  // namespace bcos::txvalidator
