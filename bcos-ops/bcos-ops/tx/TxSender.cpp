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
 * @file TxSender.cpp
 */
#include "TxSender.h"
#include "bcos-ops/OpsError.h"
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilderV1.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <thread>

namespace bcos::ops
{
namespace
{
constexpr int64_t c_blockLimitRange = 500;  // same as the SDK's Service::getBlockLimit

constexpr size_t c_maxValueHexDigits = 64;  // uint256

/// "007" → "7", "000" → "0"
std::string withoutLeadingZeros(std::string const& _digits)
{
    auto first = _digits.find_first_not_of('0');
    return first == std::string::npos ? "0" : _digits.substr(first);
}

/// "" → ""; "0x1f"/"0X1f"/"0x001f" → "0x1f"; "31"/"031" → "0x1f"; more than 256 bits or anything
/// else throws OpsError{1} (u256 is unchecked: 2^256 would wrap to 0 and the node's hex2u
/// truncates too). Leading zeros never count: the bound is on the value, not the digit count.
std::string hexValue(std::string const& _value)
{
    if (_value.empty())
    {
        return _value;
    }
    if (_value.starts_with("0x") || _value.starts_with("0X"))
    {
        auto digits = _value.substr(2);
        if (digits.empty() || std::any_of(digits.begin(), digits.end(),
                                  [](unsigned char c) { return std::isxdigit(c) == 0; }))
        {
            throw OpsError(c_exitUsage, "value is not a number: " + _value);
        }
        digits = withoutLeadingZeros(digits);
        if (digits.size() > c_maxValueHexDigits)
        {
            throw OpsError(c_exitUsage, "value exceeds 256 bits: " + _value);
        }
        return "0x" + digits;
    }
    if (std::any_of(
            _value.begin(), _value.end(), [](unsigned char c) { return std::isdigit(c) == 0; }))
    {
        throw OpsError(c_exitUsage, "value is not a decimal or 0x number: " + _value);
    }
    // 2^256 has 78 decimal digits; parse through the checked cpp_int, not the modular u256. Its
    // string constructor reads a leading 0 as an octal prefix ("010" would be 8, "08" throws a
    // runtime_error), so the zeros go first
    boost::multiprecision::cpp_int number(withoutLeadingZeros(_value));
    if (number >= (boost::multiprecision::cpp_int(1) << 256))
    {
        throw OpsError(c_exitUsage, "value exceeds 256 bits: " + _value);
    }
    return toQuantity(u256(number));
}

Json::Value txParams(std::string const& _group)
{
    Json::Value value(Json::arrayValue);
    value.append(_group);
    value.append("");
    return value;
}
}  // namespace

Receipt Receipt::fromJson(Json::Value const& _json)
{
    Receipt receipt;
    receipt.raw = _json;
    if (_json.isMember("status"))
    {
        receipt.status = _json["status"].isString() ? std::stoi(_json["status"].asString()) :
                                                      _json["status"].asInt();
    }
    receipt.txHash = _json.get("transactionHash", "").asString();
    receipt.contractAddress = _json.get("contractAddress", "").asString();
    receipt.output = _json.get("output", "").asString();
    receipt.message = _json.get("message", "").asString();
    if (_json.isMember("blockNumber"))
    {
        receipt.blockNumber = _json["blockNumber"].asInt64();
    }
    receipt.gasUsed = _json.get("gasUsed", "").asString();
    return receipt;
}

TxSender::TxSender(RpcCall _call, std::string _group, std::string _chainId, bool _sm,
    bcos::crypto::KeyPairInterface::UniquePtr _keyPair)
  : m_call(std::move(_call)),
    m_group(std::move(_group)),
    m_chainId(std::move(_chainId)),
    m_sm(_sm),
    m_keyPair(std::move(_keyPair))
{}

std::string TxSender::address() const
{
    bcos::crypto::Hash::Ptr hash;
    if (m_sm)
    {
        hash = std::make_shared<bcos::crypto::SM3>();
    }
    else
    {
        hash = std::make_shared<bcos::crypto::Keccak256>();
    }
    return m_keyPair->address(hash).hexPrefixed();
}

Receipt TxSender::send(std::string const& _to, bytes _data, std::string const& _abi)
{
    return send(_to, std::move(_data), _abi, SendOptions{});
}

SignedTx TxSender::sign(
    std::string const& _to, bytes _data, std::string const& _abi, SendOptions const& _options)
{
    int64_t blockLimit =
        _options.blockLimit ?
            *_options.blockLimit :
            m_call("getBlockNumber", txParams(m_group)).asInt64() + c_blockLimitRange;
    bcos::cppsdk::utilities::TransactionBuilderV1 builder;
    auto nonce = _options.nonce.empty() ? builder.generateRandomStr() : _options.nonce;
    // version 1 carries value/gasPrice/gasLimit; a plain call keeps the version-0 layout the
    // SDK samples and every older node accept. The node decodes value as a 0x hex string
    // (TransactionFactoryImpl.cpp), so a decimal CLI value is converted here.
    int32_t version = _options.value.empty() ? 0 : 1;
    auto [hash, signedTx] = builder.createSignedTransaction(*m_keyPair, 0, version, m_group,
        m_chainId, _to, nonce, std::move(_data), _abi, blockLimit, hexValue(_options.value));
    SignedTx result;
    result.hash = hash.starts_with("0x") ? hash : "0x" + hash;
    result.hex = signedTx.starts_with("0x") ? signedTx.substr(2) : signedTx;
    return result;
}

Receipt TxSender::sendRaw(std::string const& _signedHex)
{
    auto sendParams = txParams(m_group);
    sendParams.append(_signedHex.starts_with("0x") ? _signedHex.substr(2) : _signedHex);
    sendParams.append(false);
    return Receipt::fromJson(m_call("sendTransaction", sendParams));
}

Receipt TxSender::send(
    std::string const& _to, bytes _data, std::string const& _abi, SendOptions const& _options)
{
    auto signedTx = sign(_to, std::move(_data), _abi, _options);
    auto receipt = sendRaw(signedTx.hex);
    if (receipt.txHash.empty())
    {
        receipt.txHash = signedTx.hash;
    }
    return receipt;
}

Receipt TxSender::call(std::string const& _to, bytes _data)
{
    auto callParams = txParams(m_group);
    callParams.append(_to);
    callParams.append(toHex(_data));
    return Receipt::fromJson(m_call("call", callParams));
}

Json::Value fetchTransaction(
    RpcCall const& _call, std::string const& _group, std::string const& _hash, bool _proof)
{
    auto query = txParams(_group);
    query.append(_hash);
    query.append(_proof);
    Json::Value merged;
    merged["transaction"] = _call("getTransaction", query);
    merged["receipt"] = _call("getTransactionReceipt", query);
    return merged;
}
std::string normalizeRawTxHex(std::string const& _text)
{
    std::string hex;
    hex.reserve(_text.size());
    for (unsigned char c : _text)
    {
        if (std::isspace(c) == 0)
        {
            hex.push_back(static_cast<char>(c));
        }
    }
    if (hex.starts_with("0x") || hex.starts_with("0X"))
    {
        hex = hex.substr(2);
    }
    if (hex.empty() || hex.size() % 2 != 0 ||
        std::any_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) == 0; }))
    {
        throw OpsError(c_exitUsage, "raw transaction is not hex");
    }
    return hex;
}
namespace
{
int64_t ethQuantity(Json::Value const& _value, int64_t _default)
{
    if (_value.isIntegral())
    {
        return _value.asInt64();
    }
    if (!_value.isString() || _value.asString().empty())
    {
        return _default;
    }
    try
    {
        return std::stoll(_value.asString(), nullptr, 0);  // "0x1a" and "26" alike
    }
    catch (std::exception const&)
    {
        return _default;
    }
}
}  // namespace

Receipt receiptFromEth(Json::Value const& _json)
{
    Receipt receipt;
    receipt.raw = _json;
    receipt.txHash = _json.get("transactionHash", "").asString();
    receipt.status = ethQuantity(_json["status"], 0) == 1 ? 0 : 1;
    receipt.blockNumber = ethQuantity(_json["blockNumber"], -1);
    receipt.gasUsed = std::to_string(ethQuantity(_json["gasUsed"], 0));
    receipt.contractAddress =
        _json["contractAddress"].isString() ? _json["contractAddress"].asString() : "";
    return receipt;
}

Receipt sendRawEth(RpcCall const& _web3, std::string const& _hex, int _waitMs, int _pollMs,
    std::atomic<bool> const* _stop)
{
    Json::Value params(Json::arrayValue);
    params.append("0x" + normalizeRawTxHex(_hex));
    auto hash = _web3("eth_sendRawTransaction", params).asString();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_waitMs);
    while (true)
    {
        Json::Value query(Json::arrayValue);
        query.append(hash);
        auto result = _web3("eth_getTransactionReceipt", query);
        if (result.isObject())
        {
            auto receipt = receiptFromEth(result);
            if (receipt.txHash.empty())
            {
                receipt.txHash = hash;
            }
            return receipt;
        }
        if (std::chrono::steady_clock::now() >= deadline || (_stop != nullptr && _stop->load()))
        {
            Receipt pending;
            pending.txHash = hash;
            pending.status = -1;
            pending.message = "pending";
            pending.pending = true;
            pending.raw["transactionHash"] = hash;
            pending.raw["pending"] = true;
            pending.raw["status"] = Json::nullValue;
            pending.raw["message"] =
                "pending: no receipt within " + std::to_string(_waitMs / 1000) + "s";
            return pending;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(_pollMs));
    }
}
}  // namespace bcos::ops
