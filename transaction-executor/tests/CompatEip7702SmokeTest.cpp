/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief End-to-end smoke for EIP-7702 on transaction-executor (type-4, executor v1 path).
 *  @file CompatEip7702SmokeTest.cpp
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-executor/src/Common.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::rpc;

namespace
{
Web3Transaction makeEip7702SmokeTx()
{
    Web3Transaction w3;
    w3.type = TransactionType::EIP7702;
    w3.chainId = 1;
    w3.nonce = 0;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 2;
    w3.gasLimit = 500'000;
    w3.to = Address("0x1111111111111111111111111111111111111111");
    w3.value = 0;
    w3.data = bytes{};
    w3.signatureR = bytes(32, 0x11);
    w3.signatureS = bytes(32, 0x22);
    w3.signatureV = 27;

    AuthorizationListEntry auth;
    auth.chainId = 1;
    auth.address = Address("0x2222222222222222222222222222222222222222");
    auth.nonce = 0;
    auth.yParity = 0;
    auth.r = h256(fromHex(std::string(64, 'a')));
    auth.s = h256(fromHex(std::string(64, 'b')));
    w3.authorizationList.push_back(auth);
    return w3;
}
}  // namespace

class CompatEip7702SmokeFixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    TransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl(), precompiledManager};

    CompatEip7702SmokeFixture()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        auto features = ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
        features.set(ledger::Features::Flag::feature_evm_cancun);
        features.set(ledger::Features::Flag::feature_evm_prague);
        features.set(ledger::Features::Flag::feature_evm_eip2929);
        ledgerConfig.setFeatures(features);
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatEip7702Smoke, CompatEip7702SmokeFixture)

BOOST_AUTO_TEST_CASE(type4_executeTransaction_smoke)
{
    task::syncWait([this]() -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        auto w3 = makeEip7702SmokeTx();
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        auto const signBytes = w3.encodeForSign();
        tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
        auto const txHash = w3.hashForSign();
        tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());

        evmc_address sender{};
        sender.bytes[19] = 0x99;
        tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));

        bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

        ledger::account::EVMAccount<decltype(storage)> senderAccount(storage, sender, false);
        co_await senderAccount.create();
        co_await senderAccount.setBalance(u256(1) << 96);

        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, txImpl, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
