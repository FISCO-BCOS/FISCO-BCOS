// bcos-rlp-protocol/bcos-rlp-protocol/Web3TxHandler.cpp
#include "Web3TxHandler.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Log.h>
#include <cstddef>  // std::ptrdiff_t (ListEnd pointer arithmetic)
#include <cstdint>

// These using-declarations are NOT dead: they are the ADL bridge that lets the generic codec
// templates (RLPEncode.h encodeItems / Common.h lengthOfItems / RLPDecode.h decodeItems) find the
// AccessListEntry overloads defined at the bottom of this file. Those overloads live in namespace
// codec::rlp, which is not an associated namespace of bcos::rpc::AccessListEntry; without the
// using-declaration a standalone TU (or a unity-batch reorder) fails with "neither visible in the
// template definition nor found by argument-dependent lookup". Keep the three in sync with the
// overloads.
using bcos::codec::rlp::decode;
using bcos::codec::rlp::encode;
using bcos::codec::rlp::length;

namespace bcos::rpc
{
namespace
{
// Strip leading zero bytes from signature data (R/S) to keep RLP encoding canonical.
// Note: cannot be named getSignatureRef — it would collide with the same-named static
// function in Web3Transaction.cpp under unity build (same TU); the logic is fully equivalent.
bcos::bytesConstRef trimLeadingZeroBytes(bcos::bytesConstRef input) noexcept
{
    size_t i = 0;
    while (i < input.size() && input[i] == bcos::byte{0})
    {
        ++i;
    }
    return {input.data() + i, input.size() - i};
}

// Signature length padding matching the end of decodeTransaction (32 bytes each for R/S).
void padSignature(bcos::bytes& signatureR, bcos::bytes& signatureS) noexcept
{
    if (signatureR.size() < bcos::crypto::SECP256K1_SIGNATURE_R_LEN)
    {
        signatureR.insert(signatureR.begin(),
            bcos::crypto::SECP256K1_SIGNATURE_R_LEN - signatureR.size(), bcos::byte{0});
    }
    if (signatureS.size() < bcos::crypto::SECP256K1_SIGNATURE_S_LEN)
    {
        signatureS.insert(signatureS.begin(),
            bcos::crypto::SECP256K1_SIGNATURE_S_LEN - signatureS.size(), bcos::byte{0});
    }
}

// ⚠️ decode contract: each handler's decode is self-contained (consumes the envelope itself —
// Legacy: RLP list header; typed: type byte + RLP list header), copied field-by-field from the
// corresponding branch of Web3Transaction.cpp decodeTransaction. The dispatcher
// (Web3Transaction::decode member) should determine the type from the first byte, set out.type,
// then call handlerFor(type).decode(in, out, withSig), and must not consume the type byte first.

struct LegacyTxHandler : Web3TxHandler
{
    static codec::rlp::Header headerTxBase(const Web3Transaction& tx) noexcept
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.nonce);
        // for legacy tx, it means gas price
        h.payloadLength += codec::rlp::length(tx.maxFeePerGas);
        h.payloadLength += codec::rlp::length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.value);
        h.payloadLength += codec::rlp::length(tx.data);
        return h;
    }

    // Signing preimage: rlp([nonce, gasPrice, gasLimit, to, value, data]) + EIP-155 chainId tail
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = headerTxBase(tx);
        if (tx.chainId)
        {
            // EIP-155: chainId and the two trailing 0 placeholders
            head.payloadLength += codec::rlp::length(tx.chainId.value()) + 2;
        }
        // header() already computed the exact payload size; reserve it so the incremental
        // encode(out, ...) appends never reallocate (hot path: takeToTarsTransaction per tx).
        out.reserve(codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.nonce);
        // for legacy tx, it means gas price
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        if (tx.chainId)
        {
            // EIP-155
            codec::rlp::encode(out, tx.chainId.value());
            codec::rlp::encode(out, 0U);
            codec::rlp::encode(out, 0U);
        }
        return out;
    }

    // Full RLP (no type byte): rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = header(tx);
        out.reserve(codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.nonce);
        // for legacy tx, it means gas price
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.getSignatureV());
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return out;
    }

    // RLP header (length computation): base fields + signature length (Legacy special: signatureV
    // uses getSignatureV())
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += codec::rlp::length(tx.getSignatureV());
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // Decode: rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
    bcos::Error::UniquePtr decode(
        bcos::bytesRef& in, Web3Transaction& out, bool withSig) const override
    {
        if (in.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        auto&& [error, head] = codec::rlp::decodeHeader(in);
        if (error != nullptr)
        {
            return std::move(error);
        }
        if (!head.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnexpectedList, "legacy tx: expected RLP list");
        }
        // Fields must stay within the declared list payload (op-geth ListEnd parity).
        bcos::byte* const payloadStart = in.data();
        auto const payloadLength = head.payloadLength;
        out.type = TransactionType::Legacy;
        bcos::Error::UniquePtr decodeError = nullptr;
        if (decodeError = codec::rlp::decodeItems(in, out.nonce, out.maxPriorityFeePerGas);
            decodeError != nullptr)
        {
            return decodeError;
        }
        out.maxFeePerGas = out.maxPriorityFeePerGas;

        if (decodeError = codec::rlp::decode(in, out.gasLimit); decodeError != nullptr)
        {
            return decodeError;
        }

        if (in.empty()) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] == codec::rlp::BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (decodeError = codec::rlp::decode(in, addr); decodeError != nullptr)
            {
                return decodeError;
            }
            out.to.emplace(addr);
        }

        // ⚠️ Check value/data decode errors immediately: if deferred to the withSig branch,
        // a later successful signature decode would overwrite decodeError and misjudge
        // malformed input as valid.
        if (auto err = codec::rlp::decodeItems(in, out.value, out.data); err != nullptr)
        {
            return err;
        }
        if (withSig)
        {
            if (decodeError =
                    codec::rlp::decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
                decodeError != nullptr)
            {
                return decodeError;
            }
            // TODO: EIP-155 chainId decode from encoded bytes for sign
            auto v = out.signatureV;
            if (v == 27 || v == 28)
            {
                // pre EIP-155
                out.chainId = std::nullopt;
                out.signatureV = v - 27;
            }
            else if (v == 0 || v == 1)
            {
                // pre-EIP-155 v must be 27/28; v=0/1 is an invalid signature (treated the same as
                // v<35). The original implementation returned decodeError(nullptr), which callers
                // treated as success — changed to report an explicit error.
                out.chainId = std::nullopt;
                return BCOS_ERROR_UNIQUE_PTR(
                    codec::rlp::DecodingError::InvalidVInSignature, "Invalid V in signature");
            }
            else if (v < 35)
            {
                return BCOS_ERROR_UNIQUE_PTR(
                    codec::rlp::DecodingError::InvalidVInSignature, "Invalid V in signature");
            }
            else
            {
                // https://eips.ethereum.org/EIPS/eip-155
                // Find chain_id and y_parity ∈ {0, 1} such that
                // v = chain_id * 2 + 35 + y_parity
                out.signatureV = (v - 35) % 2;
                out.chainId = ((v - 35) >> 1);
            }
        }
        else
        {
            if (in.empty())
            {
                // Pre-EIP-155 6-field signing preimage: no chainId tail at all.
                out.chainId = std::nullopt;
            }
            else
            {
                // chainId is decoded as uint64 (narrower than EIP-155's u256) — the shared
                // RLP UnsignedIntegral width gate (RLPDecode.h) rejects payloads >8 bytes, so
                // chainIds >2^64-1 are rejected at decode rather than silently truncated. This
                // narrower accept set covers all production chains; op-geth uses uint256.Int.
                uint64_t chainId = 0;
                decodeError = codec::rlp::decode(in, chainId);
                if (decodeError != nullptr)
                {
                    return decodeError;
                }
                out.chainId.emplace(chainId);
                // An EIP-155 signing preimage ends with (chainId, 0, 0): consume the two empty
                // r/s placeholders. The tail must be exactly 2 items — a 7/8-field preimage
                // (chainId without both placeholders) is malformed (op-geth preimages are 6 or 9
                // fields), so there is no "placeholders optional" leniency.
                // Zero-copy like the deposit isSystemTransaction decode (bcos::bytes would
                // heap-allocate per decode just to assert emptiness).
                bcos::bytesRef placeholderR;
                bcos::bytesRef placeholderS;
                if (decodeError = codec::rlp::decodeItems(in, placeholderR, placeholderS);
                    decodeError != nullptr)
                {
                    return decodeError;
                }
                if (!placeholderR.empty() || !placeholderS.empty())
                {
                    return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                        "legacy signing preimage: non-empty r/s placeholders in EIP-155 tail");
                }
            }
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        // ListEnd parity (op-geth List/ListEnd): fields must consume exactly the declared
        // payload — over-consumption (fields crossing the boundary) AND under-consumption
        // (trailing bytes inside the payload) are both rejected here (data() is null only
        // after a failed decode).
        if (in.data() != nullptr &&
            in.data() - payloadStart != static_cast<std::ptrdiff_t>(payloadLength))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                "legacy tx: fields exceed the declared RLP list payload length");
        }
        return decodeError;
    }
};

struct EIP2930TxHandler : Web3TxHandler
{
    static codec::rlp::Header headerTxBase(const Web3Transaction& tx) noexcept
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.chainId.value_or(0));
        h.payloadLength += codec::rlp::length(tx.nonce);
        // EIP2930 does not encode maxPriorityFeePerGas; gasPrice is carried by maxFeePerGas
        h.payloadLength += codec::rlp::length(tx.maxFeePerGas);
        h.payloadLength += codec::rlp::length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.value);
        h.payloadLength += codec::rlp::length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
        return h;
    }

    // Signing preimage: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data,
    // accessList])
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = headerTxBase(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP2930));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        // for EIP2930 it means gasPrice
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        return out;
    }

    // Full RLP: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
    // signatureYParity, signatureR, signatureS])
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = header(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP2930));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        // for EIP2930 it means gasPrice
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        codec::rlp::encode(out, tx.signatureV);
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return out;
    }

    // RLP header (length computation): base fields + 1 (signatureV y-parity after the type byte) +
    // signature length
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // Decode: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
    // yParity, r, s])
    bcos::Error::UniquePtr decode(
        bcos::bytesRef& in, Web3Transaction& out, bool withSig) const override
    {
        if (in.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] != static_cast<bcos::byte>(TransactionType::EIP2930))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnsupportedTransactionType,
                "Unsupported transaction type");
        }
        out.type = TransactionType::EIP2930;
        in = in.getCroppedData(1);
        auto&& [e, head] = codec::rlp::decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!head.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnexpectedString, "Unexpected String");
        }
        // Fields must stay within the declared list payload (op-geth ListEnd parity).
        bcos::byte* const payloadStart = in.data();
        auto const payloadLength = head.payloadLength;
        uint64_t chainId = 0;
        if (auto error = codec::rlp::decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
            error != nullptr)
        {
            return error;
        }
        out.chainId.emplace(chainId);
        // EIP2930: gasPrice is carried in maxFeePerGas
        out.maxFeePerGas = out.maxPriorityFeePerGas;

        if (auto error = codec::rlp::decode(in, out.gasLimit); error != nullptr)
        {
            return error;
        }

        if (in.empty()) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] == codec::rlp::BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (auto error = codec::rlp::decode(in, addr); error != nullptr)
            {
                return error;
            }
            out.to.emplace(addr);
        }

        if (auto error = codec::rlp::decodeItems(in, out.value, out.data, out.accessList);
            error != nullptr)
        {
            return error;
        }

        bcos::Error::UniquePtr decodeError = nullptr;
        if (withSig)
        {
            decodeError =
                codec::rlp::decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            // For EIP-2718 typed txs the v field is y_parity (0 or 1): signatureV > 1 is invalid
            // input (same for EIP-2930/1559/4844); silently accepting it would hide bad
            // transactions.
            if (decodeError == nullptr && out.signatureV > 1)
            {
                return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InvalidVInSignature,
                    "typed tx y_parity must be 0 or 1");
            }
        }
        // Return the first decode error before ListEnd parity — if decodeItems(v,r,s)
        // failed, the cursor may be mid-field and ListEnd would report a misleading
        // "fields exceed payload" instead of the real root cause.
        if (decodeError != nullptr)
        {
            return decodeError;
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        // op-geth ListEnd parity: reject if fields crossed the declared payload boundary.
        if (in.data() != nullptr &&
            in.data() - payloadStart != static_cast<std::ptrdiff_t>(payloadLength))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                "EIP2930 tx: fields exceed the declared RLP list payload length");
        }
        return decodeError;
    }
};

struct EIP1559TxHandler : Web3TxHandler
{
    static codec::rlp::Header headerTxBase(const Web3Transaction& tx) noexcept
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.chainId.value_or(0));
        h.payloadLength += codec::rlp::length(tx.nonce);
        h.payloadLength += codec::rlp::length(tx.maxPriorityFeePerGas);
        h.payloadLength += codec::rlp::length(tx.maxFeePerGas);
        h.payloadLength += codec::rlp::length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.value);
        h.payloadLength += codec::rlp::length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
        return h;
    }

    // Signing preimage: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, destination, amount, data, access_list])
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = headerTxBase(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP1559));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        codec::rlp::encode(out, tx.maxPriorityFeePerGas);
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        return out;
    }

    // Full RLP: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, destination, amount, data, access_list, signature_y_parity, signature_r,
    // signature_s])
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = header(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP1559));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        codec::rlp::encode(out, tx.maxPriorityFeePerGas);
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        codec::rlp::encode(out, tx.signatureV);
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return out;
    }

    // RLP header (length computation)
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // Decode: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit,
    // destination, amount, data, access_list, yParity, r, s])
    bcos::Error::UniquePtr decode(
        bcos::bytesRef& in, Web3Transaction& out, bool withSig) const override
    {
        if (in.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] != static_cast<bcos::byte>(TransactionType::EIP1559))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnsupportedTransactionType,
                "Unsupported transaction type");
        }
        out.type = TransactionType::EIP1559;
        in = in.getCroppedData(1);
        auto&& [e, head] = codec::rlp::decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!head.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnexpectedString, "Unexpected String");
        }
        // Fields must stay within the declared list payload (op-geth ListEnd parity).
        bcos::byte* const payloadStart = in.data();
        auto const payloadLength = head.payloadLength;
        uint64_t chainId = 0;
        if (auto error = codec::rlp::decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
            error != nullptr)
        {
            return error;
        }
        out.chainId.emplace(chainId);
        if (auto error = codec::rlp::decode(in, out.maxFeePerGas); error != nullptr)
        {
            return error;
        }

        if (auto error = codec::rlp::decode(in, out.gasLimit); error != nullptr)
        {
            return error;
        }

        if (in.empty()) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] == codec::rlp::BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (auto error = codec::rlp::decode(in, addr); error != nullptr)
            {
                return error;
            }
            out.to.emplace(addr);
        }

        if (auto error = codec::rlp::decodeItems(in, out.value, out.data, out.accessList);
            error != nullptr)
        {
            return error;
        }

        bcos::Error::UniquePtr decodeError = nullptr;
        if (withSig)
        {
            decodeError =
                codec::rlp::decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            // For EIP-2718 typed txs the v field is y_parity (0 or 1): signatureV > 1 is invalid
            // input (same for EIP-2930/1559/4844); silently accepting it would hide bad
            // transactions.
            if (decodeError == nullptr && out.signatureV > 1)
            {
                return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InvalidVInSignature,
                    "typed tx y_parity must be 0 or 1");
            }
        }
        // Return the first decode error before ListEnd parity — if decodeItems(v,r,s)
        // failed, the cursor may be mid-field and ListEnd would report a misleading
        // "fields exceed payload" instead of the real root cause.
        if (decodeError != nullptr)
        {
            return decodeError;
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        // op-geth ListEnd parity: reject if fields crossed the declared payload boundary.
        if (in.data() != nullptr &&
            in.data() - payloadStart != static_cast<std::ptrdiff_t>(payloadLength))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                "EIP1559 tx: fields exceed the declared RLP list payload length");
        }
        return decodeError;
    }
};

struct DepositTxHandler : Web3TxHandler
{
    // Full RLP: 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data])
    // — self-contained inline (consistent with the other typed handlers in this file); the 8-field
    // unsigned layout's field order/types verified against op-geth DepositTx; field-by-field
    // layout and to-nilability behaviour cross-checked against op-geth's deposit encoding.
    // `to` nilability changes the RLP shape (empty string vs 20-byte address),
    // so the contract-creation branch must be encoded separately.
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        out.push_back(static_cast<bcos::byte>(TransactionType::Deposit));
        // isSystemTransaction uses uint32_t (not uint8_t): RLPEncode.h's uint8_t generic scalar
        // encoding odr-uses the non-template toCompactBigEndian(byte, unsigned) overload, whose
        // only definition lives in DataConvertUtility.cpp rather than a header (a pre-existing
        // bcos-utilities header/library boundary defect); uint32_t only matches in-header
        // templates and produces identical bytes.
        const uint32_t isSystemTransactionByte = tx.isSystemTx ? 1 : 0;
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.sourceHash, tx.from, *tx.to, tx.mint, tx.value, tx.gasLimit,
                isSystemTransactionByte, tx.data);
        }
        else
        {
            codec::rlp::encode(out, tx.sourceHash, tx.from, bcos::bytesConstRef{}, tx.mint,
                tx.value, tx.gasLimit, isSystemTransactionByte, tx.data);
        }
        return out;
    }

    // deposit has no signature: the signing preimage is the full envelope (aligned with
    // op-geth DepositTx.SigningHash, used for extraTransactionBytes).
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override { return encode(tx); }

    // RLP header (length computation): sum of the 8 field lengths (excluding the type byte —
    // consistent with the other typed handlers).
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.sourceHash);  // h256
        h.payloadLength += codec::rlp::length(tx.from);        // Address
        // to (optional Address): empty string 0x80 takes 1 byte, or 20-byte address + 1-byte length
        // header
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.mint);      // u256
        h.payloadLength += codec::rlp::length(tx.value);     // u256
        h.payloadLength += codec::rlp::length(tx.gasLimit);  // uint64
        h.payloadLength += 1;  // isSystemTransaction (0x80 empty string or 0x01)
        h.payloadLength += codec::rlp::length(tx.data);  // bytes
        return h;
    }

    // Decode: 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data])
    // ⚠️ decode contract: typed handler is self-contained (consumes the type byte + list header
    // itself); the dispatcher (Web3Transaction::decode) does not trim the type byte first.
    bcos::Error::UniquePtr decode(
        bcos::bytesRef& in, Web3Transaction& out, bool /*withSig*/) const override
    {
        if (in.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] != static_cast<bcos::byte>(TransactionType::Deposit))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnsupportedTransactionType,
                "Unsupported transaction type");
        }
        out.type = TransactionType::Deposit;
        in = in.getCroppedData(1);
        auto&& [e, head] = codec::rlp::decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!head.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnexpectedString, "deposit: expected RLP list");
        }
        // Fields must stay within the declared list payload (op-geth ListEnd parity).
        bcos::byte* const payloadStart = in.data();
        auto const payloadLength = head.payloadLength;
        // Check and propagate errors on every field decode (must not swallow silently)
        if (auto err = codec::rlp::decode(in, out.sourceHash); err != nullptr)
            return err;  // h256
        if (auto err = codec::rlp::decode(in, out.from); err != nullptr)
            return err;  // Address
        // to (optional Address; empty string 0x80 = nullopt contract creation)
        if (in.empty()) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] == codec::rlp::BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (auto err = codec::rlp::decode(in, addr); err != nullptr)
                return err;
            out.to.emplace(addr);
        }
        // Width note: this layer is display-grade — mint/value/gas go through the shared RLP
        // UnsignedIntegral width gate (RLPDecode.h: payloadLength > digits/8 → UnexpectedLength),
        // so over-wide integers are REJECTED here too, matching the consensus path's accept set
        // (no silent truncation). Only non-canonical prefixes (leading zeros / bare 0x00) remain
        // display-layer-tolerant; the consensus path additionally rejects those via
        // decodeDepositEnvelope (OpstackExecutor.h, integerPayloadLength canonicality checks).
        if (auto err = codec::rlp::decode(in, out.mint); err != nullptr)
            return err;  // u256
        if (auto err = codec::rlp::decode(in, out.value); err != nullptr)
            return err;  // u256
        if (auto err = codec::rlp::decode(in, out.gasLimit); err != nullptr)
            return err;  // uint64
        // ⚠️ isSystemTransaction cannot use codec::rlp::decode(bool) (RLPDecode.h:200-217
        // requires payloadLength==1 and would report UnexpectedLength for the golden 0x80 empty
        // string). Byte semantics are required: empty string 0x80 → false (op-geth RLP nil);
        // single byte 0x01 → true; anything else is an error.
        {
            // Zero-copy view into the input (not bcos::bytes, which heap-allocates per decode).
            bcos::bytesRef raw{};
            if (auto err = codec::rlp::decode(in, raw); err != nullptr)
                return err;
            if (raw.empty())
            {
                out.isSystemTx = false;
            }
            else if (raw.size() == 1 && raw[0] == 1)
            {
                out.isSystemTx = true;
            }
            else
            {
                return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedLength,
                    "deposit: invalid isSystemTransaction value");
            }
        }
        if (auto err = codec::rlp::decode(in, out.data); err != nullptr)
            return err;  // bytes
        out.nonce = 0;   // deposit nonce is always 0
        // op-geth ListEnd parity: reject if fields crossed the declared payload boundary.
        if (in.data() != nullptr &&
            in.data() - payloadStart != static_cast<std::ptrdiff_t>(payloadLength))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                "deposit tx: fields exceed the declared RLP list payload length");
        }
        return nullptr;
    }
};

struct EIP4844TxHandler : Web3TxHandler
{
    static codec::rlp::Header headerTxBase(const Web3Transaction& tx) noexcept
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.chainId.value_or(0));
        h.payloadLength += codec::rlp::length(tx.nonce);
        h.payloadLength += codec::rlp::length(tx.maxPriorityFeePerGas);
        h.payloadLength += codec::rlp::length(tx.maxFeePerGas);
        h.payloadLength += codec::rlp::length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.value);
        h.payloadLength += codec::rlp::length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
        h.payloadLength += codec::rlp::length(tx.maxFeePerBlobGas);
        h.payloadLength += codec::rlp::length(tx.blobVersionedHashes);
        return h;
    }

    // Signing preimage: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes])
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = headerTxBase(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP4844));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        codec::rlp::encode(out, tx.maxPriorityFeePerGas);
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        codec::rlp::encode(out, tx.maxFeePerBlobGas);
        codec::rlp::encode(out, tx.blobVersionedHashes);
        return out;
    }

    // Full RLP: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes,
    // signature_y_parity, signature_r, signature_s])
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = header(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP4844));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        codec::rlp::encode(out, tx.maxPriorityFeePerGas);
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        codec::rlp::encode(out, tx.maxFeePerBlobGas);
        codec::rlp::encode(out, tx.blobVersionedHashes);
        codec::rlp::encode(out, tx.signatureV);
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return out;
    }

    // RLP header (length computation)
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // Decode: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit,
    // to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes, yParity, r, s])
    bcos::Error::UniquePtr decode(
        bcos::bytesRef& in, Web3Transaction& out, bool withSig) const override
    {
        if (in.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] != static_cast<bcos::byte>(TransactionType::EIP4844))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnsupportedTransactionType,
                "Unsupported transaction type");
        }
        out.type = TransactionType::EIP4844;
        in = in.getCroppedData(1);
        auto&& [e, head] = codec::rlp::decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!head.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnexpectedString, "Unexpected String");
        }
        // Fields must stay within the declared list payload (op-geth ListEnd parity).
        bcos::byte* const payloadStart = in.data();
        auto const payloadLength = head.payloadLength;
        uint64_t chainId = 0;
        if (auto error = codec::rlp::decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
            error != nullptr)
        {
            return error;
        }
        out.chainId.emplace(chainId);
        if (auto error = codec::rlp::decode(in, out.maxFeePerGas); error != nullptr)
        {
            return error;
        }

        if (auto error = codec::rlp::decode(in, out.gasLimit); error != nullptr)
        {
            return error;
        }

        if (in.empty()) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] == codec::rlp::BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (auto error = codec::rlp::decode(in, addr); error != nullptr)
            {
                return error;
            }
            out.to.emplace(addr);
        }

        if (auto error = codec::rlp::decodeItems(in, out.value, out.data, out.accessList);
            error != nullptr)
        {
            return error;
        }

        if (auto error = codec::rlp::decodeItems(in, out.maxFeePerBlobGas, out.blobVersionedHashes);
            error != nullptr)
        {
            return error;
        }

        bcos::Error::UniquePtr decodeError = nullptr;
        if (withSig)
        {
            decodeError =
                codec::rlp::decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            // For EIP-2718 typed txs the v field is y_parity (0 or 1): signatureV > 1 is invalid
            // input (same for EIP-2930/1559/4844); silently accepting it would hide bad
            // transactions.
            if (decodeError == nullptr && out.signatureV > 1)
            {
                return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InvalidVInSignature,
                    "typed tx y_parity must be 0 or 1");
            }
        }
        // Return the first decode error before ListEnd parity — if decodeItems(v,r,s)
        // failed, the cursor may be mid-field and ListEnd would report a misleading
        // "fields exceed payload" instead of the real root cause.
        if (decodeError != nullptr)
        {
            return decodeError;
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        // op-geth ListEnd parity: reject if fields crossed the declared payload boundary.
        if (in.data() != nullptr &&
            in.data() - payloadStart != static_cast<std::ptrdiff_t>(payloadLength))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                "EIP4844 tx: fields exceed the declared RLP list payload length");
        }
        return decodeError;
    }
};

struct EIP7702TxHandler : Web3TxHandler
{
    static codec::rlp::Header headerTxBase(const Web3Transaction& tx) noexcept
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.chainId.value_or(0));
        h.payloadLength += codec::rlp::length(tx.nonce);
        h.payloadLength += codec::rlp::length(tx.maxPriorityFeePerGas);
        h.payloadLength += codec::rlp::length(tx.maxFeePerGas);
        h.payloadLength += codec::rlp::length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.value);
        h.payloadLength += codec::rlp::length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
        h.payloadLength += codec::rlp::length(tx.authorizationList);
        return h;
    }

    // Signing preimage: 0x04 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, destination, amount, data, access_list, authorization_list])
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = headerTxBase(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP7702));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        codec::rlp::encode(out, tx.maxPriorityFeePerGas);
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        codec::rlp::encode(out, tx.authorizationList);
        return out;
    }

    // Full RLP: 0x04 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, destination, amount, data, access_list, authorization_list, signature_y_parity,
    // signature_r, signature_s])
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = header(tx);
        out.reserve(1 + codec::rlp::lengthOfLength(head.payloadLength) + head.payloadLength);
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP7702));
        codec::rlp::encodeHeader(out, head);
        codec::rlp::encode(out, tx.chainId.value_or(0));
        codec::rlp::encode(out, tx.nonce);
        codec::rlp::encode(out, tx.maxPriorityFeePerGas);
        codec::rlp::encode(out, tx.maxFeePerGas);
        codec::rlp::encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            codec::rlp::encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, tx.value);
        codec::rlp::encode(out, tx.data);
        codec::rlp::encode(out, tx.accessList);
        codec::rlp::encode(out, tx.authorizationList);
        codec::rlp::encode(out, tx.signatureV);
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return out;
    }

    // RLP header (length computation)
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // Decode: 0x04 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit,
    // destination, amount, data, access_list, authorization_list, yParity, r, s])
    bcos::Error::UniquePtr decode(
        bcos::bytesRef& in, Web3Transaction& out, bool withSig) const override
    {
        if (in.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] != static_cast<bcos::byte>(TransactionType::EIP7702))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnsupportedTransactionType,
                "Unsupported transaction type");
        }
        out.type = TransactionType::EIP7702;
        in = in.getCroppedData(1);
        auto&& [e, head] = codec::rlp::decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!head.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnexpectedString, "Unexpected String");
        }
        // Fields must stay within the declared list payload (op-geth ListEnd parity).
        bcos::byte* const payloadStart = in.data();
        auto const payloadLength = head.payloadLength;
        uint64_t chainId = 0;
        if (auto error = codec::rlp::decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
            error != nullptr)
        {
            return error;
        }
        out.chainId.emplace(chainId);
        if (auto error = codec::rlp::decode(in, out.maxFeePerGas); error != nullptr)
        {
            return error;
        }

        if (auto error = codec::rlp::decode(in, out.gasLimit); error != nullptr)
        {
            return error;
        }

        if (in.empty()) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::InputTooShort, "Input too short");
        }
        if (in[0] == codec::rlp::BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (auto error = codec::rlp::decode(in, addr); error != nullptr)
            {
                return error;
            }
            out.to.emplace(addr);
        }

        if (auto error = codec::rlp::decodeItems(in, out.value, out.data, out.accessList);
            error != nullptr)
        {
            return error;
        }

        if (auto error = codec::rlp::decode(in, out.authorizationList); error != nullptr)
        {
            return error;
        }

        bcos::Error::UniquePtr decodeError = nullptr;
        if (withSig)
        {
            decodeError =
                codec::rlp::decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            // For EIP-2718 typed txs the v field is y_parity (0 or 1): signatureV > 1 is invalid
            // input (same for EIP-2930/1559/4844/7702); silently accepting it would hide bad
            // transactions.
            if (decodeError == nullptr && out.signatureV > 1)
            {
                return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InvalidVInSignature,
                    "typed tx y_parity must be 0 or 1");
            }
        }
        // Return the first decode error before ListEnd parity — if decodeItems(v,r,s)
        // failed, the cursor may be mid-field and ListEnd would report a misleading
        // "fields exceed payload" instead of the real root cause.
        if (decodeError != nullptr)
        {
            return decodeError;
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        // op-geth ListEnd parity: reject if fields crossed the declared payload boundary.
        if (in.data() != nullptr &&
            in.data() - payloadStart != static_cast<std::ptrdiff_t>(payloadLength))
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnexpectedListElements,
                "EIP7702 tx: fields exceed the declared RLP list payload length");
        }
        return decodeError;
    }
};
}  // namespace

Web3TxHandler& handlerFor(TransactionType type)
{
    static LegacyTxHandler legacy;
    static EIP2930TxHandler eip2930;
    static EIP1559TxHandler eip1559;
    static EIP4844TxHandler eip4844;
    static DepositTxHandler deposit;
    static EIP7702TxHandler eip7702;
    switch (type)
    {
    case TransactionType::Legacy:
        return legacy;
    case TransactionType::EIP2930:
        return eip2930;
    case TransactionType::EIP1559:
        return eip1559;
    case TransactionType::EIP4844:
        return eip4844;
    case TransactionType::Deposit:
        return deposit;
    case TransactionType::EIP7702:
        return eip7702;
    default:
        break;
    }
    // Unknown TransactionType: a new enum value was added without updating this switch. Return a
    // no-op sentinel handler instead of falling back to Legacy (which would silently decode/encode
    // as the wrong format producing garbage fields); encode/encodeForSign return empty bytes
    // (fail-safe) and decode returns UnsupportedTransactionType.
    // ERROR, not FATAL: the fatal level makes the log sink call std::abort(), which would kill
    // the process instead of degrading to the fail-safe sentinel below.
    BCOS_LOG(ERROR) << "handlerFor: unhandled TransactionType " << static_cast<int>(type)
                    << " — update the switch to handle the new type";
    static struct : Web3TxHandler
    {
        bcos::bytes encodeForSign(const Web3Transaction&) const override { return {}; }
        bcos::bytes encode(const Web3Transaction&) const override { return {}; }
        codec::rlp::Header header(const Web3Transaction&) const override
        {
            return {.isList = true, .payloadLength = 0};
        }
        bcos::Error::UniquePtr decode(bcos::bytesRef&, Web3Transaction&, bool) const override
        {
            return BCOS_ERROR_UNIQUE_PTR(
                codec::rlp::DecodingError::UnsupportedTransactionType, "Unknown transaction type");
        }
    } sentinel;
    return sentinel;
}
}  // namespace bcos::rpc

// AccessListEntry RLP codec overloads — defined here (not in Web3Transaction.cpp) because the
// handlers are the primary consumer: they encode/decode tx.accessList. The three using-declarations
// above make these visible at the template POI so the generic codec can find them.
namespace bcos::codec::rlp
{
using namespace bcos::rpc;
Header header(const AccessListEntry& entry) noexcept
{
    auto len = length(entry.storageKeys);
    return {.isList = true, .payloadLength = Address::SIZE + 1 + len};
}

size_t length(AccessListEntry const& entry) noexcept
{
    auto head = header(entry);
    return lengthOfLength(head.payloadLength) + head.payloadLength;
}

void encode(bcos::bytes& out, const AccessListEntry& entry) noexcept
{
    encodeHeader(out, header(entry));
    encode(out, entry.account.ref());
    encode(out, entry.storageKeys);
}

bcos::Error::UniquePtr decode(bcos::bytesRef& in, AccessListEntry& out) noexcept
{
    return decode(in, out.account, out.storageKeys);
}

Header header(const AuthorizationListEntry& entry) noexcept
{
    auto len = codec::rlp::length(entry.chainId) + Address::SIZE + 1 +
               codec::rlp::length(entry.nonce) +
               codec::rlp::length(static_cast<uint64_t>(entry.yParity)) +
               codec::rlp::length(entry.r) + codec::rlp::length(entry.s);
    return {.isList = true, .payloadLength = len};
}

size_t length(AuthorizationListEntry const& entry) noexcept
{
    auto head = header(entry);
    return lengthOfLength(head.payloadLength) + head.payloadLength;
}

void encode(bcos::bytes& out, const AuthorizationListEntry& entry) noexcept
{
    encodeHeader(out, header(entry));
    encode(out, entry.chainId);
    encode(out, entry.address.ref());
    encode(out, entry.nonce);
    encode(out, static_cast<uint64_t>(entry.yParity));
    encode(out, entry.r);
    encode(out, entry.s);
}
}  // namespace bcos::codec::rlp
