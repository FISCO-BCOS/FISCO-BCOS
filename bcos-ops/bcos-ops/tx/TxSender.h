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
 * @brief signs with bcos-cpp-sdk's TransactionBuilder and sends through RpcCall, so the same
 *        code works over WebSocket and the local socket. Sender is the seam the smoke test fakes.
 * @file TxSender.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <json/json.h>
#include <memory>
#include <optional>
#include <string>

namespace bcos::ops
{
struct Receipt
{
    Json::Value raw;
    int32_t status = -1;
    std::string txHash;
    std::string contractAddress;
    std::string output;  // 0x-hex
    std::string message;
    int64_t blockNumber = -1;
    std::string gasUsed;
    bool ok() const { return status == 0; }
    static Receipt fromJson(Json::Value const& _json);
};

class Sender
{
public:
    virtual ~Sender() = default;
    /// deploy when _to is empty; blocks until the receipt arrives
    virtual Receipt send(std::string const& _to, bytes _data, std::string const& _abi) = 0;
    /// read-only call; returns {status, output}
    virtual Receipt call(std::string const& _to, bytes _data) = 0;
};

/// per-transaction overrides; every field empty means the builder's defaults (random nonce,
/// blockLimit = current height + 500, no value → transaction version 0)
struct SendOptions
{
    std::string value;  // decimal wei; non-empty switches to the v1 transaction layout
    std::string nonce;
    std::optional<int64_t> blockLimit;
};

struct SignedTx
{
    std::string hash;  // 0x-prefixed
    std::string hex;   // tars-encoded signed transaction, no 0x
};

class TxSender : public Sender
{
public:
    TxSender(RpcCall _call, std::string _group, std::string _chainId, bool _sm,
        bcos::crypto::KeyPairInterface::UniquePtr _keyPair);
    Receipt send(std::string const& _to, bytes _data, std::string const& _abi) override;
    /// send with explicit value / nonce / blockLimit
    Receipt send(
        std::string const& _to, bytes _data, std::string const& _abi, SendOptions const& _options);
    /// the signed transaction without sending it (`tx sign`, raw-tx round trips)
    SignedTx sign(
        std::string const& _to, bytes _data, std::string const& _abi, SendOptions const& _options);
    /// `sendTransaction [group,"",hex,false]` for an already signed transaction (0x optional)
    Receipt sendRaw(std::string const& _signedHex);
    Receipt call(std::string const& _to, bytes _data) override;
    std::string address() const;

private:
    RpcCall m_call;
    std::string m_group;
    std::string m_chainId;
    bool m_sm;
    bcos::crypto::KeyPairInterface::UniquePtr m_keyPair;
};

/// getTransaction + getTransactionReceipt merged: {"transaction": ..., "receipt": ...};
/// _proof asks the node for txProof / receiptProof
Json::Value fetchTransaction(
    RpcCall const& _call, std::string const& _group, std::string const& _hash, bool _proof = false);
}  // namespace bcos::ops
