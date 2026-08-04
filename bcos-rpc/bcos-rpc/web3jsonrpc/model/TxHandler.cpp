// bcos-rpc/bcos-rpc/web3jsonrpc/model/TxHandler.cpp
#include "TxHandler.h"
#include "Web3Transaction.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <cstdint>

namespace bcos::rpc
{
namespace
{
// 去掉签名数据(R/S)的前导零,保证 RLP 编码规范(canonical)。
// 注意:不能命名为 getSignatureRef —— 与 Web3Transaction.cpp 的同名 static 函数在 unity build
// (同一 TU)下会冲突;本函数逻辑与之完全等价。
bcos::bytesConstRef trimLeadingZeroBytes(bcos::bytesConstRef input) noexcept
{
    size_t i = 0;
    while (i < input.size() && input[i] == bcos::byte{0})
    {
        ++i;
    }
    return {input.data() + i, input.size() - i};
}

// 与 decodeTransaction 末尾一致的签名长度补齐(R/S 各 32 字节)。
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

// RPC JSON 输出共用的 accessList 序列化(仅 EIP2930+ 输出)。
void outputAccessList(Json::Value& result, const Web3Transaction& tx)
{
    result["accessList"] = Json::arrayValue;
    result["accessList"].resize(tx.accessList.size());
    for (const auto& accessList : tx.accessList)
    {
        Json::Value access = Json::objectValue;
        access["address"] = accessList.account.hexPrefixed();
        access["storageKeys"] = Json::arrayValue;
        access["storageKeys"].resize(accessList.storageKeys.size());
        for (const auto& j : accessList.storageKeys)
        {
            access["storageKeys"].append(j.hexPrefixed());
        }
        result["accessList"].append(std::move(access));
    }
}

// RPC JSON 输出共用的 blob 字段序列化(仅 EIP4844 输出)。
void outputBlobFields(Json::Value& result, const Web3Transaction& tx)
{
    result["maxFeePerBlobGas"] = tx.maxFeePerBlobGas.str();
    result["blobVersionedHashes"] = Json::arrayValue;
    result["blobVersionedHashes"].resize(tx.blobVersionedHashes.size());
    for (const auto& blobVersionedHashe : tx.blobVersionedHashes)
    {
        result["blobVersionedHashes"].append(blobVersionedHashe.hexPrefixed());
    }
}

// RPC JSON 输出的公共字段(nonce/type/value/chainId,全部类型)。
void outputCommonFields(Json::Value& result, const Web3Transaction& tx)
{
    result["nonce"] = toQuantity(tx.nonce);
    result["type"] = toQuantity(static_cast<uint8_t>(tx.type));
    result["value"] = toQuantity(tx.value);
    result["chainId"] = toQuantity(tx.chainId.value_or(0));
}

// ⚠️ decode 契约:每个 handler 的 decode 自包含(自行消费 envelope —— Legacy 为 RLP list header,
// typed 为 type byte + RLP list header),逐字段照抄 Web3Transaction.cpp decodeTransaction 的
// 对应分支。分派方(Web3Transaction::decode 成员)应先根据首字节判定类型、设置 out.type,然后
// 调用 handlerFor(type).decode(in, out, withSig),且不要预先消费 type byte。

struct LegacyTxHandler : TxHandler
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

    // 签名预映像: rlp([nonce, gasPrice, gasLimit, to, value, data]) + EIP-155 chainId 尾巴
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        auto head = headerTxBase(tx);
        if (tx.chainId)
        {
            // EIP-155: chainId 及其后两个 0 占位
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

    // 完整 RLP(无 type byte): rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
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

    // RLP header(长度计算): 基础字段 + 签名长度(Legacy 特殊: signatureV 用 getSignatureV())
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += codec::rlp::length(tx.getSignatureV());
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // 解码: rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
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

        // ⚠️ 立即检查 value/data 解码错误:若延迟到 withSig 分支再赋值,
        // 后续签名解码成功会覆盖 decodeError,把畸形输入误判为成功。
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
                out.chainId = std::nullopt;
                return decodeError;
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
            out.chainId.emplace(chainId);
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        return decodeError;
    }

    // RPC JSON 输出(类型相关字段): 仅公共字段
    void toJson(const Web3Transaction& tx, Json::Value& result) const override
    {
        outputCommonFields(result, tx);
    }
};

struct EIP2930TxHandler : TxHandler
{
    static codec::rlp::Header headerTxBase(const Web3Transaction& tx) noexcept
    {
        codec::rlp::Header h{.isList = true};
        h.payloadLength += codec::rlp::length(tx.chainId.value_or(0));
        h.payloadLength += codec::rlp::length(tx.nonce);
        // EIP2930 不编码 maxPriorityFeePerGas,gasPrice 由 maxFeePerGas 承载
        h.payloadLength += codec::rlp::length(tx.maxFeePerGas);
        h.payloadLength += codec::rlp::length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += codec::rlp::length(tx.value);
        h.payloadLength += codec::rlp::length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
        return h;
    }

    // 签名预映像: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList])
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

    // 完整 RLP: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
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

    // RLP header(长度计算): 基础字段 + 1(type byte 之后的 signatureV y-parity)+ 签名长度
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // 解码: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList, yParity,
    // r, s])
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
        // EIP2930: gasPrice 承载在 maxFeePerGas
        out.maxFeePerGas = out.maxPriorityFeePerGas;

        if (auto error = codec::rlp::decode(in, out.gasLimit); error != nullptr)
        {
            return error;
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
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        return decodeError;
    }

    // RPC JSON 输出: 公共字段 + accessList
    void toJson(const Web3Transaction& tx, Json::Value& result) const override
    {
        outputCommonFields(result, tx);
        outputAccessList(result, tx);
    }
};

struct EIP1559TxHandler : TxHandler
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

    // 签名预映像: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
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

    // 完整 RLP: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
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

    // RLP header(长度计算)
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // 解码: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit,
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
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        return decodeError;
    }

    // RPC JSON 输出: 公共字段 + accessList + maxPriorityFeePerGas/maxFeePerGas
    void toJson(const Web3Transaction& tx, Json::Value& result) const override
    {
        outputCommonFields(result, tx);
        outputAccessList(result, tx);
        result["maxPriorityFeePerGas"] = toQuantity(tx.maxPriorityFeePerGas);
        result["maxFeePerGas"] = toQuantity(tx.maxFeePerGas);
    }
};

struct EIP4844TxHandler : TxHandler
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

    // 签名预映像: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes])
    bcos::bytes encodeForSign(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP4844));
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
        codec::rlp::encode(out, tx.maxFeePerBlobGas);
        codec::rlp::encode(out, tx.blobVersionedHashes);
        return out;
    }

    // 完整 RLP: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
    // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes,
    // signature_y_parity, signature_r, signature_s])
    bcos::bytes encode(const Web3Transaction& tx) const override
    {
        bcos::bytes out;
        out.push_back(static_cast<bcos::byte>(TransactionType::EIP4844));
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
        codec::rlp::encode(out, tx.maxFeePerBlobGas);
        codec::rlp::encode(out, tx.blobVersionedHashes);
        codec::rlp::encode(out, tx.signatureV);
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        codec::rlp::encode(out, trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return out;
    }

    // RLP header(长度计算)
    codec::rlp::Header header(const Web3Transaction& tx) const override
    {
        auto h = headerTxBase(tx);
        h.payloadLength += 1;
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureR)));
        h.payloadLength += codec::rlp::length(trimLeadingZeroBytes(bcos::ref(tx.signatureS)));
        return h;
    }

    // 解码: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit,
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
        }
        if (withSig)
        {
            // rehandle signature and chainId
            padSignature(out.signatureR, out.signatureS);
        }
        return decodeError;
    }

    // RPC JSON 输出: 公共字段 + accessList + maxPriorityFeePerGas/maxFeePerGas + blob 字段
    void toJson(const Web3Transaction& tx, Json::Value& result) const override
    {
        outputCommonFields(result, tx);
        outputAccessList(result, tx);
        result["maxPriorityFeePerGas"] = toQuantity(tx.maxPriorityFeePerGas);
        result["maxFeePerGas"] = toQuantity(tx.maxFeePerGas);
        outputBlobFields(result, tx);
    }
};
}  // namespace

TxHandler& handlerFor(TransactionType type)
{
    static LegacyTxHandler legacy;
    static EIP2930TxHandler eip2930;
    static EIP1559TxHandler eip1559;
    static EIP4844TxHandler eip4844;
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
        // TODO(Task 5): 真正的 DepositTxHandler;当前防御性回退到 Legacy,导致 deposit 解码失败(红)
        return legacy;
    }
    return legacy;
}
}  // namespace bcos::rpc
