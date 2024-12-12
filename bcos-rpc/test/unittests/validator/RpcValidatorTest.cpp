#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "unittests/common/RPCFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/validator/RpcValidator.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>


using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;

namespace bcos::test
{
class RpcValidatorFixture : public RPCFixture
{
public:
    RpcValidatorFixture() = default;

    bcos::crypto::CryptoSuite::Ptr cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);

    Secp256k1KeyPair createTestKeyPair()
    {
        h256 fixedSec("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
        auto sec = std::make_shared<KeyImpl>(fixedSec.asBytes());
        return Secp256k1KeyPair(sec);
    }
};

BOOST_FIXTURE_TEST_SUITE(testRpcValidator, RpcValidatorFixture)

BOOST_AUTO_TEST_CASE(testBcosTransactionSignatureCheck)
{
    std::cout << "===== BCOS Test Case : testBcosTransactionSignatureCheck"
              << "=====" << std::endl;
    std::string to("Target");
    bcos::bytes input(bcos::asBytes("Arguments"));
    std::string nonce("800");

    bcostars::protocol::TransactionFactoryImpl factory(cryptoSuite);
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    // auto tx = factory.createTransaction(
    //     1, to, input, nonce, 100, "testChainValidator", "testGroup", 1000, *keyPair);

    // auto result = RpcValidator::checkBcosReplayedProtected(tx, "testChainValidator");
    // BOOST_CHECK(result == true);
    // std::string chainIdError = "testChainValidatorError";
    // result = RpcValidator::checkBcosReplayedProtected(tx, chainIdError);
    // BOOST_CHECK(result == false);
    auto tx = factory.createTransaction(
        1, to, input, nonce, 100, "testChainValidator", "testGroup", 1000, *keyPair, "");
    auto resultStatus = task::syncWait(RpcValidator::checkTransactionValidator(tx, m_ledger));
    BOOST_CHECK(resultStatus == TransactionStatus::None);
    auto outRangeValue = "0x" + std::string(TRANSACTION_VALUE_MAX_LENGTH, '1');
    tx = factory.createTransaction(1, to, input, nonce, 100, "testChainValidator", "testGroup",
        1000, *keyPair, "", outRangeValue);
    resultStatus = task::syncWait(RpcValidator::checkTransactionValidator(tx, m_ledger));
    BOOST_CHECK(resultStatus == TransactionStatus::OverFlowValue);

    auto largeInput = "0x" + std::string(MAX_INITCODE_SIZE, '1');
    tx = factory.createTransaction(1, to, bcos::asBytes(largeInput), nonce, 100,
        "testChainValidator", "testGroup", 1000, *keyPair);
    resultStatus = task::syncWait(RpcValidator::checkTransactionValidator(tx, m_ledger));
    BOOST_CHECK(resultStatus == TransactionStatus::OversizedData);

    auto accountCodeHash = bcos::storage::Entry();
    accountCodeHash.set(bcos::asBytes("0x123456"));
    m_ledger->setStorageAt(
        tx->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, accountCodeHash);
    tx = factory.createTransaction(
        1, to, input, nonce, 100, "testChainValidator", "testGroup", 1000, *keyPair, "");
    resultStatus = task::syncWait(RpcValidator::checkTransactionValidator(tx, m_ledger));
    BOOST_CHECK(resultStatus == TransactionStatus::NoEOAAccount);

    tx = factory.createTransaction(
        1, to, input, nonce, 100, "testChainValidator", "testGroup", 1000, *keyPair, "", "0x1");
    accountCodeHash = bcos::storage::Entry();
    accountCodeHash.set(bcos::asBytes(""));
    m_ledger->setStorageAt(
        tx->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, accountCodeHash);
    resultStatus = task::syncWait(RpcValidator::checkTransactionValidator(tx, m_ledger));
    BOOST_CHECK(resultStatus == TransactionStatus::NoEnoughBalance);
    auto balance = bcos::storage::Entry();
    balance.set(bcos::asBytes("0x123456"));
    m_ledger->setStorageAt(tx->sender(), bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE, balance);
    resultStatus = task::syncWait(RpcValidator::checkTransactionValidator(tx, m_ledger));
    BOOST_CHECK(resultStatus == TransactionStatus::None);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test