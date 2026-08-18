// bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3TxHandler.cpp
#include "Web3TxHandler.h"
#include "Web3Transaction.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Log.h>
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
        codec::rlp::encodeHeader(out, header(tx));
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
                codec::rlp::DecodingError::UnexpectedList, "Unexpected list");
        }
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
            uint64_t chainId = 0;
            decodeError = codec::rlp::decode(in, chainId);
            if (decodeError != nullptr)
            {
                return decodeError;
            }
            out.chainId.emplace(chainId);
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
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
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP2930));
        codec::rlp::encodeHeader(out, headerTxBase(tx));
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
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP2930));
        codec::rlp::encodeHeader(out, header(tx));
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
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
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
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP1559));
        codec::rlp::encodeHeader(out, headerTxBase(tx));
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
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP1559));
        codec::rlp::encodeHeader(out, header(tx));
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
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        return decodeError;
    }
};

}  // namespace
}  // namespace bcos::rpc
