// bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3TxHandler.h
// ⚠️ isSystemTransaction encoding workaround: DepositTxHandler::encode() encodes it as uint32_t
// (not uint8_t) because RLPEncode.h's generic uint8_t encoding odr-uses the non-template
// toCompactBigEndian(byte, unsigned), defined only in DataConvertUtility.cpp (a pre-existing
// bcos-utilities header/library boundary defect). uint32_t matches in-header templates and emits
// the identical 1-byte RLP (0x80 or 0x01); switch back to uint8_t once the defect is fixed.
#pragma once
#include <bcos-codec/rlp/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/json.h>
#include <cstdint>

namespace bcos::rpc
{
class Web3Transaction;
// TransactionType is defined in Web3Transaction.h (enum class : uint8_t); forward-declare the
// underlying type so handlerFor can be declared without pulling in the whole header.
enum class TransactionType : uint8_t;
// NOTE: Header is actually bcos::codec::rlp::Header (Common.h:45), so it must be included, not
// forward-declared. The include must stay OUTSIDE the namespace: expanding Common.h inside
// namespace bcos::rpc would resolve `namespace bcos::codec::rlp` as a nested
// bcos::rpc::bcos::codec::rlp, polluting the namespace.

struct Web3TxHandler
{
    virtual ~Web3TxHandler() = default;
    // Signing preimage (RLP without type byte or signature)
    virtual bcos::bytes encodeForSign(const Web3Transaction&) const = 0;
    // Full RLP (with type byte for typed transactions)
    virtual bcos::bytes encode(const Web3Transaction&) const = 0;
    // RLP header (length computation)
    virtual bcos::codec::rlp::Header header(const Web3Transaction&) const = 0;
    // Decode (populates Web3Transaction; withSig controls whether the signature is parsed).
    // ⚠️ Returns Error::UniquePtr (not void): decode errors must propagate, not be silently
    // swallowed.
    virtual bcos::Error::UniquePtr decode(
        bcos::bytesRef&, Web3Transaction&, bool withSig) const = 0;
};

// Dispatch by type via a switch over the known type bytes. Unknown types get a fail-loud
// no-op sentinel (encode returns empty, decode reports UnsupportedTransactionType, ERROR log) —
// deliberately NOT a Legacy fallback: silently coding a typed payload as legacy would produce
// garbage fields (see Web3TxHandler.cpp's sentinel note).
Web3TxHandler& handlerFor(TransactionType type);
}  // namespace bcos::rpc
