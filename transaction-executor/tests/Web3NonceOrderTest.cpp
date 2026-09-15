// Regression: web3 EOA nonce must be independent of intra-block transaction
// execution order. When two web3 transactions from the same sender land in one
// block and the scheduler executes them high-nonce-first, the legacy arithmetic
// (max(callNonce, storageNonce) + 1) over-increments the account nonce because
// the second (lower-nonce) tx reads the storage nonce already raised by the
// first. The fix (max(callNonce + 1, storageNonce), gated on
// bugfix_nonce_ordering) makes the final nonce order-independent.

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;

class Web3NonceOrderFixture
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
    bcos::executor_v1::TransactionExecutorImpl executor{
        receiptFactory, cryptoSuite->hashImpl(), precompiledManager};

    Web3NonceOrderFixture()
    {
        bcos::executor_v1::clearGlobalExecutableCache();
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    }

    // Execute two web3 deploys from one sender against the same storage, higher
    // nonce first, at the given compatibility version. Returns the sender's final
    // stored nonce.
    std::string runOutOfOrder(bcos::protocol::BlockVersion version)
    {
        return task::syncWait([this, version]() -> task::Task<std::string> {
            bcostars::protocol::BlockHeaderImpl blockHeader;
            blockHeader.setVersion(static_cast<uint32_t>(version));
            blockHeader.calculateHash(*cryptoSuite->hashImpl());

            auto features = ledgerConfig.features();
            features.setGenesisFeatures(version);
            ledgerConfig.setFeatures(features);

            bcos::bytes bytecode;
            boost::algorithm::unhex(helloworldBytecode, std::back_inserter(bytecode));

            evmc_address sender = bcos::unhexAddress("abcdef0123456789abcdef0123456789abcdef01");

            auto deploy = [&](std::string_view nonceHex)
                -> task::Task<bcos::protocol::TransactionReceipt::Ptr> {
                // version 0: the factory forces gasLimit to 0, so the EVM runs with the
                // full system gas budget and this test never depends on gas accounting.
                auto tx = transactionFactory.createTransaction(
                    0, "", bytecode, std::string(nonceHex), 0, "", "", 0);
                tx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
                dynamic_cast<bcostars::protocol::TransactionImpl&>(*tx).mutableInner().type = 1;
                co_return co_await executor.executeTransaction(
                    storage, blockHeader, *tx, 0, ledgerConfig, false);
            };

            // Intra-block order is high-nonce-first (the scheduler does not sort
            // same-sender txs by nonce), reproducing the field failure.
            auto highReceipt = co_await deploy("0x6");
            BOOST_CHECK_EQUAL(
                highReceipt->status(), static_cast<int32_t>(protocol::TransactionStatus::None));
            auto lowReceipt = co_await deploy("0x5");
            BOOST_CHECK_EQUAL(
                lowReceipt->status(), static_cast<int32_t>(protocol::TransactionStatus::None));

            ledger::account::EVMAccount senderAccount(storage, sender, false);
            co_return (co_await senderAccount.nonce()).value();
        }());
    }
};

BOOST_FIXTURE_TEST_SUITE(Web3NonceOrder, Web3NonceOrderFixture)

// With the fix active (>= 3.17.0), the final nonce equals maxTxNonce + 1 = 7,
// independent of the high-nonce-first execution order.
BOOST_AUTO_TEST_CASE(orderIndependentWithFix)
{
    BOOST_CHECK_EQUAL(runOutOfOrder(bcos::protocol::BlockVersion::V3_17_0_VERSION), "7");
}

// Legacy behavior (< 3.17.0, flag off): the lower-nonce tx re-reads the already
// raised storage nonce and over-increments to 8. Locks the backward-compat gate.
BOOST_AUTO_TEST_CASE(legacyOverIncrementsWithoutFix)
{
    BOOST_CHECK_EQUAL(runOutOfOrder(bcos::protocol::BlockVersion::V3_16_4_VERSION), "8");
}

BOOST_AUTO_TEST_SUITE_END()
