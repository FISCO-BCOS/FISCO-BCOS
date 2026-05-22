/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TxValidatorEip7702Test.cpp
 * @brief Unit tests for TxValidator::validateEip7702Admission (T31 / spec §5.3).
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/sm2/SM2Crypto.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/ledger/SystemConfigs.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/Web3AuthorizationList.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include "bcos-task/Wait.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <functional>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

namespace bcos::test
{
namespace
{

std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3Eip7702Tx(
    std::size_t authorizationCount = 1)
{
    auto holder = std::make_shared<bcostars::Transaction>();
    holder->type = static_cast<tars::Char>(TransactionType::Web3Transaction);
    holder->web3TypedTxKind = static_cast<tars::Char>(4);

    holder->data.authorizationList.reserve(authorizationCount);
    for (std::size_t i = 0; i < authorizationCount; ++i)
    {
        bcostars::Web3AuthorizationListEntry entry;
        entry.chainId = "1";
        entry.address = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        entry.nonce = std::to_string(i);
        entry.yParity = 0;
        entry.r.assign(32, static_cast<char>(0xaa));
        entry.s.assign(32, static_cast<char>(0xbb));
        holder->data.authorizationList.emplace_back(std::move(entry));
    }

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        std::function<bcostars::Transaction*()>([holder]() { return holder.get(); }));
}

struct LedgerMockHolder
{
    fakeit::Mock<ledger::LedgerInterface> mock;
    std::shared_ptr<ledger::LedgerInterface> iface;
};

std::shared_ptr<ledger::LedgerInterface> makeLedgerWithExecutorVersion(int version)
{
    auto holder = std::make_shared<LedgerMockHolder>();
    fakeit::When(Method(holder->mock, asyncGetSystemConfigByKey))
        .AlwaysDo(
            [version](std::string_view key,
                std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                if (key != "executor_version")
                {
                    callback(BCOS_ERROR_PTR(LedgerError::EmptyEntry, "not found"), {}, 0);
                    return;
                }
                callback(nullptr, std::to_string(version), 1);
            });
    holder->iface = std::shared_ptr<ledger::LedgerInterface>(
        &holder->mock.get(), [holder](ledger::LedgerInterface*) {});
    return holder->iface;
}

TxValidator makeValidator(crypto::CryptoSuite::Ptr cryptoSuite)
{
    auto txPoolNonceChecker = std::make_shared<Web3NonceChecker>(nullptr);
    return TxValidator{nullptr, txPoolNonceChecker, std::move(cryptoSuite), "group0", "chain0"};
}

TransactionStatus runAdmission(
    TxValidator& validator, Transaction const& tx, std::shared_ptr<ledger::LedgerInterface> ledger)
{
    return task::syncWait(validator.validateEip7702Admission(tx, std::move(ledger)));
}

}  // namespace

BOOST_AUTO_TEST_SUITE(TxValidatorEip7702)

BOOST_AUTO_TEST_CASE(non_web3_tx_returns_none)
{
    auto holder = std::make_shared<bcostars::Transaction>();
    holder->type = static_cast<tars::Char>(TransactionType::BCOSTransaction);
    bcostars::protocol::TransactionImpl tx(
        std::function<bcostars::Transaction*()>([holder]() { return holder.get(); }));

    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr));

    BOOST_CHECK_EQUAL(
        runAdmission(validator, tx, makeLedgerWithExecutorVersion(1)), TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(web3_without_authorization_list_returns_none)
{
    auto holder = std::make_shared<bcostars::Transaction>();
    holder->type = static_cast<tars::Char>(TransactionType::Web3Transaction);
    holder->web3TypedTxKind = static_cast<tars::Char>(2);
    bcostars::protocol::TransactionImpl tx(
        std::function<bcostars::Transaction*()>([holder]() { return holder.get(); }));

    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr));

    BOOST_CHECK_EQUAL(
        runAdmission(validator, tx, makeLedgerWithExecutorVersion(1)), TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(oversized_authorization_list_returns_malformed)
{
    auto tx = makeWeb3Eip7702Tx(WEB3_EIP7702_MAX_AUTHORIZATION_LIST_ENTRIES + 1);
    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr));

    BOOST_CHECK_EQUAL(runAdmission(validator, *tx, makeLedgerWithExecutorVersion(1)),
        TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(non_secp256k1_chain_returns_malformed)
{
    auto tx = makeWeb3Eip7702Tx(1);
    auto keccak = std::make_shared<crypto::Keccak256>();
    auto sm2 = std::make_shared<crypto::SM2Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, sm2, nullptr));

    BOOST_CHECK_EQUAL(runAdmission(validator, *tx, makeLedgerWithExecutorVersion(1)),
        TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(executor_version_zero_returns_malformed)
{
    auto tx = makeWeb3Eip7702Tx(1);
    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr));

    BOOST_CHECK_EQUAL(runAdmission(validator, *tx, makeLedgerWithExecutorVersion(0)),
        TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(missing_executor_config_defaults_to_zero_malformed)
{
    auto holder = std::make_shared<LedgerMockHolder>();
    fakeit::When(Method(holder->mock, asyncGetSystemConfigByKey))
        .AlwaysDo(
            [](std::string_view /*key*/,
                std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                callback(BCOS_ERROR_PTR(LedgerError::EmptyEntry, "not found"), {}, 0);
            });
    auto ledgerPtr = std::shared_ptr<ledger::LedgerInterface>(
        &holder->mock.get(), [holder](ledger::LedgerInterface*) {});

    auto tx = makeWeb3Eip7702Tx(1);
    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr));

    BOOST_CHECK_EQUAL(runAdmission(validator, *tx, ledgerPtr), TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(executor_version_one_returns_none)
{
    auto tx = makeWeb3Eip7702Tx(1);
    auto keccak = std::make_shared<crypto::Keccak256>();
    auto secp = std::make_shared<crypto::Secp256k1Crypto>();
    auto validator = makeValidator(std::make_shared<crypto::CryptoSuite>(keccak, secp, nullptr));

    BOOST_CHECK_EQUAL(
        runAdmission(validator, *tx, makeLedgerWithExecutorVersion(1)), TransactionStatus::None);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
