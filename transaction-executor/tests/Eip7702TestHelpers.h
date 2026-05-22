/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared helpers for EIP-7702 transaction-executor tests.
 */

#pragma once

#include "bcos-executor/src/Web3Eip7702Apply.h"
#include "bcos-executor/src/Web3Eip7702Fill.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-transaction-executor/Eip7702Common.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <cstring>
#include <intx/intx.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bcos::test::eip7702
{

inline std::shared_ptr<bcos::crypto::KeyImpl> testAuthorityKey()
{
    // Same key as bcos-rpc Web3TypeTest (deterministic authority).
    return std::make_shared<bcos::crypto::KeyImpl>(
        bcos::fromHex("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"));
}

inline bcos::crypto::Secp256k1KeyPair testAuthorityKeyPair()
{
    return bcos::crypto::Secp256k1KeyPair(testAuthorityKey());
}

inline void setPragueFeatures(bcos::ledger::LedgerConfig& ledgerConfig)
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);
    features.set(bcos::ledger::Features::Flag::feature_balance);
    features.set(bcos::ledger::Features::Flag::feature_balance_policy1);
    ledgerConfig.setFeatures(features);
}

inline void setLedgerChainId(bcos::ledger::LedgerConfig& ledgerConfig, uint64_t chainId = 1)
{
    evmc_uint256be evmcChain{};
    intx::be::store(evmcChain.bytes, intx::uint256{chainId});
    ledgerConfig.setChainId(evmcChain);
}

inline bcos::executor::Eip7702Authorization signAuthorizationTuple(
    bcos::crypto::Hash::Ptr const& hashImpl, bcos::crypto::KeyPairInterface const& keyPair,
    uint64_t chainId, bcos::Address const& target, uint64_t nonce)
{
    bcos::executor::Eip7702Authorization auth;
    auth.chainId = chainId;
    auth.address = target;
    auth.nonce = nonce;

    bcos::bytes rlpList;
    bcos::codec::rlp::encode(rlpList, chainId, target, nonce);
    bcos::bytes signDomain;
    signDomain.reserve(1 + rlpList.size());
    signDomain.push_back(0x05);
    signDomain.insert(signDomain.end(), rlpList.begin(), rlpList.end());
    auto const hash = hashImpl->hash(bcos::bytesConstRef(signDomain.data(), signDomain.size()));

    bcos::crypto::Secp256k1Crypto secp;
    auto const signature = secp.sign(keyPair, hash, false);
    std::memcpy(auth.r.data(), signature->data(), sizeof(auth.r));
    std::memcpy(
        auth.s.data(), signature->data() + bcos::crypto::SECP256K1_SIGNATURE_R_LEN, sizeof(auth.s));
    auth.yParity = signature->back();

    return auth;
}

inline bcos::bytes makeDelegationIndicatorCode(bcos::Address const& target)
{
    bcos::bytes code;
    code.reserve(bcos::executor_v1::EIP_7702_DELEGATION_CODE_SIZE);
    code.insert(code.end(), std::begin(bcos::executor_v1::EIP_7702_DELEGATION_PREFIX),
        std::end(bcos::executor_v1::EIP_7702_DELEGATION_PREFIX));
    code.insert(code.end(), target.begin(), target.end());
    return code;
}

template <class Storage>
task::Task<void> setDelegationIndicator(Storage& storage, bcos::crypto::Hash::Ptr const& hashImpl,
    evmc_address const& authority, bcos::Address const& target, bool binaryAddress = false)
{
    bcos::ledger::account::EVMAccount<Storage> account(storage, authority, binaryAddress);
    if (!co_await account.exists())
    {
        co_await account.create();
    }
    auto const code = makeDelegationIndicatorCode(target);
    auto const codeHash =
        hashImpl->hash(bcos::bytesConstRef(code.data(), static_cast<size_t>(code.size())));
    co_await account.setCode(code, std::string{}, codeHash);
}

template <class Storage>
task::Task<std::optional<bcos::storage::Entry>> readAccountCode(
    Storage& storage, evmc_address const& addr, bool binaryAddress = false)
{
    bcos::ledger::account::EVMAccount<Storage> account(storage, addr, binaryAddress);
    co_return co_await account.code();
}

inline std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3Type4Transaction(
    bcos::crypto::CryptoSuite& cryptoSuite,
    std::vector<bcos::executor::Eip7702Authorization> const& authorizationList,
    evmc_address const& sender, std::optional<bcos::Address> const& to, bcos::bytes const& data,
    uint64_t txNonce = 0, uint64_t gasLimit = 500'000)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP7702;
    w3.chainId = 1;
    w3.nonce = txNonce;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 2;
    w3.gasLimit = gasLimit;
    w3.to = to;
    w3.value = 0;
    w3.data = data;
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 27;

    for (auto const& auth : authorizationList)
    {
        bcos::rpc::AuthorizationListEntry entry;
        entry.chainId = auth.chainId;
        entry.address = auth.address;
        entry.nonce = auth.nonce;
        entry.yParity = auth.yParity;
        entry.r = auth.r;
        entry.s = auth.s;
        w3.authorizationList.push_back(entry);
    }

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    auto const txHash = w3.hashForSign();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
}

inline bcos::Address authorityAddressFromKey(
    bcos::crypto::Hash::Ptr const& hashImpl, bcos::crypto::KeyPairInterface const& keyPair)
{
    bcos::crypto::Secp256k1Crypto secp;
    auto const pub = keyPair.publicKey();
    return bcos::crypto::calculateAddress(hashImpl, pub);
}

/// PUSH1 42 PUSH1 0 SSTORE STOP — writes 0x2a to slot 0 in the callee's storage context.
inline bcos::bytes const& storageWriterBytecode()
{
    static bcos::bytes const code{0x60, 0x2a, 0x60, 0x00, 0x55, 0x00};
    return code;
}

/// PUSH1 0 PUSH1 0 REVERT
inline bcos::bytes const& revertBytecode()
{
    static bcos::bytes const code{0x60, 0x00, 0x60, 0x00, 0xfd};
    return code;
}

}  // namespace bcos::test::eip7702
