// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpstackExecutorTest.cpp — drives OpstackExecutor over BCOS storage. The Fixture follows the
// v0 (feat-opstack-executor branch) test that already passed end-to-end; storage is a plain
// MutableStorage. The executeTransaction cases exercise the INJECTION-style pipeline (opValidate
// + opTransition) on a real, decodable EIP-2930 signed tx, mirroring the orchestrator's call
// shape (fee/blockGasLeft/chainId passed explicitly, not via defaults).

#include "opstack-executor/OpstackExecutor.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpPredeploys.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <gtest/gtest.h>
#include <evmc/evmc.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::executor_v1::opstack;
using namespace evmc::literals;  // _address / _bytes32

namespace
{
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

// ---- 真实可解码的 EIP-2930 签名交易(复用 v0 测试的 fixture 字节)----
constexpr std::string_view kRawEip2930Tx =
    "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000"
    "906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a000"
    "00000000000000000000000000000000000000000000000000000000000003a0000000000000000000000000000000"
    "0000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061"
    "a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a6"
    "2787b4510465fadbfe39ee4094";

struct Fixture : public ::testing::Test
{
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;

    const bcos::evm::opstack::OpForkConfig& fork = bcos::evm::opstack::jovianConfig();

    Fixture()
    {
        ledgerConfig.setEVMCRevision(fork.rev);
        ledgerConfig.setGasLimit({30000000, 0});
        ledgerConfig.setGasPrice({"0x0", 0});
    }

    bcostars::protocol::TransactionImpl buildWeb3Tx()
    {
        auto bytes = fromHexWithPrefix(std::string(kRawEip2930Tx));
        auto bRef = ref(bytes);
        bcos::rpc::Web3Transaction w3{};
        [&] { ASSERT_EQ(bcos::codec::rlp::decode(bRef, w3), nullptr); }();
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        auto const txHash = w3.txHash();
        tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
        return bcostars::protocol::TransactionImpl([tarsHolder]() { return tarsHolder.get(); });
    }

    template <class T>
    static T sync(task::Task<T> t)
    {
        return task::syncWait(std::move(t));
    }
};
}  // namespace

TEST(OpstackExecutor, ConstructsWithJovianFork)
{
    bcostars::protocol::TransactionReceiptFactoryImpl rf{makeCryptoSuite()};
    OpstackExecutor executor(rf, makeCryptoSuite()->hashImpl());
    EXPECT_NE(&executor.vm(), nullptr);
}

TEST_F(Fixture, ExecutesNormalTransferEndToEnd)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};

    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildWeb3Tx();
    // evmone validation does not verify signatures — the sender is an input. Force a known sender
    // (recovering the real signer would need an ecrecover round-trip) and seed exactly that
    // account. clearSenderAndHash() taints the tx so forceSender is permitted; the executor never
    // reads hash(), so clearing it is harmless.
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        co_await acc.create();
        // Cover the fixture tx's ~2 ETH value plus gas_limit*gasPrice with headroom.
        co_await acc.setBalance(u256("100000000000000000000"));
        // EIP-3607: evmone validate_transaction (used by opValidate) rejects a sender whose
        // code_hash != EMPTY_CODE_HASH. A freshly created BCOS account has code_hash 0x0, which
        // StorageStateView passes through verbatim, so it reads as "has code". Seed the canonical
        // empty-code hash so the sender is recognised as an EOA.
        co_await acc.setCode({}, "",
            bcos::crypto::HashType(
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        // Seed the account nonce to match the tx nonce so validate_transaction's nonce check
        // passes.
        std::string nonceStr(tx.nonce());
        if (nonceStr.empty())
            nonceStr = "0";
        co_await acc.setNonce(nonceStr);
        co_return;
    }());

    bcos::evm::opstack::OpFeeParams fee{};  // zero fee: base fee 0, no L1/operator cost
    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/30000000,
        /*chainId=*/10));
    ASSERT_NE(receipt, nullptr);
    // STRONG assertions: status + gasUsed >= 21000 (intrinsic) — distinguishes a successful
    // transfer from a reverted one (a revert also burns gas but yields a non-success status).
    // NOTE: status uses the BCOS internal convention — evmoneReceiptToBcos maps EVMC_SUCCESS to
    // 0 and EVMC_REVERT to 16 (the Ethereum RPC 0↔1 flip happens in ReceiptResponse.cpp), so the
    // v0/brief's status()==1 reads as status()==0 here.
    EXPECT_EQ(receipt->status(), 0);
    EXPECT_GE(receipt->gasUsed(), 21000u);
    EXPECT_EQ(receipt->blockNumber(), 1);
    EXPECT_FALSE(receipt->opReceiptMeta().empty());
}

TEST_F(Fixture, RejectsForkRevisionMismatch)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    ledgerConfig.setEVMCRevision(EVMC_FRONTIER);  // deliberately != fork.rev
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    bcos::evm::opstack::OpFeeParams fee{};
    EXPECT_THROW(task::syncWait(executor.executeTransaction(
                     storage, blockHeader, tx, 0, ledgerConfig, false, fee, 30000000, 10)),
        bcos::executor_v1::opstack::OpForkRevisionMismatch);
}

TEST_F(Fixture, RejectsInvalidTx)
{
    // Balance 0 sender + value transfer → insufficient balance → OpTxValidationFailed.
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
    // No account created → balance 0 → validation fails.
    bcos::evm::opstack::OpFeeParams fee{};
    EXPECT_THROW(task::syncWait(executor.executeTransaction(
                     storage, blockHeader, tx, 0, ledgerConfig, false, fee, 30000000, 10)),
        bcos::executor_v1::opstack::OpTxValidationFailed);
}

TEST_F(Fixture, ConstructsWithForkAndExposesConcept)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    auto ctx = task::syncWait(
        executor.createExecuteContext(storage, blockHeader, tx, 0, ledgerConfig, false));
    EXPECT_EQ(&ctx.ledgerConfig, &ledgerConfig);
    EXPECT_FALSE(ctx.call);
    // concept lifecycle reachable (compile-time: prepare/execute/finish exist)
}
