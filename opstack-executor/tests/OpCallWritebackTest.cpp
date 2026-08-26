// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpCallWritebackTest — round-11 F1 regression: eth_call / estimateGas (call=true) must never
// write the simulated state diff back into storage. Before the fix, m_finish applied the diff
// unconditionally, so the fabricated uint256::max() sender balance (CallSimulationView) reached
// applyDiff on an unauthenticated RPC path. This test drives the public executeTransaction with
// call=true over a real MemoryStorage and asserts the storage is unmodified afterwards.

#include "OpTestReceiptFactory.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <opstack-executor/OpstackExecutor.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <optional>
#include <string>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

/// Minimal protocol::Transaction stub — only the accessors the executeTransaction path reads
/// carry knobs; every other pure virtual is a no-op stub (same pattern as OpTxConvertTest's
/// FakeTransaction, which lives in a different test binary).
class FakeTransaction : public bcos::protocol::Transaction
{
public:
    uint8_t m_kind = 2;
    bcos::bytes m_input;
    int64_t m_gasLimit = 100000;
    std::optional<bcos::u256> m_maxFeePerGas = bcos::u256{1000};
    std::optional<bcos::u256> m_maxPriorityFeePerGas = bcos::u256{10};
    std::string m_sender = std::string(sizeof(evmc_address), '\xaa');  // raw 20 bytes
    std::string m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    bcos::u256 m_value = 0;
    std::string m_nonce = "0x0";        // hex quantity
    bcos::bytes m_extraBytes = {0x02};  // non-empty envelope satisfies opValidate
    bcos::protocol::Web3AccessList m_accessList;
    bcos::protocol::VersionedHashes m_blobHashes;
    bcos::protocol::AuthorizationList m_authList;

    uint8_t web3TypedTxKind() const override { return m_kind; }
    bcos::bytesConstRef input() const override
    {
        return bcos::bytesConstRef{m_input.data(), m_input.size()};
    }
    int64_t gasLimit() const override { return m_gasLimit; }
    std::optional<bcos::u256> gasPrice() const override { return std::nullopt; }
    std::optional<bcos::u256> maxFeePerGas() const override { return m_maxFeePerGas; }
    std::optional<bcos::u256> maxPriorityFeePerGas() const override
    {
        return m_maxPriorityFeePerGas;
    }
    std::optional<bcos::u256> maxFeePerBlobGas() const override { return std::nullopt; }
    std::string_view sender() const override { return m_sender; }
    std::string_view to() const override { return m_to; }
    bcos::u256 value() const override { return m_value; }
    std::string_view chainId() const override { return {}; }
    std::string_view nonce() const override { return m_nonce; }
    bcos::bytesConstRef extraTransactionBytes() const override
    {
        return bcos::bytesConstRef{m_extraBytes.data(), m_extraBytes.size()};
    }
    bcos::protocol::Web3AccessList web3AccessList() const override { return m_accessList; }
    bcos::protocol::AuthorizationList authorizationList() const override { return m_authList; }
    bcos::protocol::VersionedHashes blobVersionedHashes() const override { return m_blobHashes; }

    // ---- unused stubs ----
    void decode(bcos::bytesConstRef) override {}
    void encode(bcos::bytes&) const override {}
    bcos::crypto::HashType hash() const override { return {}; }
    int32_t version() const override { return 0; }
    std::string_view groupId() const override { return {}; }
    int64_t blockLimit() const override { return 0; }
    void setNonce(std::string) override {}
    std::string_view abi() const override { return {}; }
    bcos::bytesConstRef extension() const override { return {}; }
    std::string_view extraData() const override { return {}; }
    int64_t importTime() const override { return 0; }
    void setImportTime(int64_t) override {}
    uint8_t type() const override { return 1; }  // Web3Transaction
    void forceSender(const bcos::bytes&) override {}
    void clearSenderAndHash() override {}
    void calculateHash(const bcos::crypto::Hash&) override {}
    bcos::bytesConstRef signatureData() const override { return {}; }
    int32_t attribute() const override { return 0; }
    void setAttribute(int32_t) override {}
};

/// Real tars header with the minimal fields toBlockInfo reads for the call path.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeCallHeader()
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(1'010'000);  // ms; /1000 = 1010 s
    h->setGasLimit(bcos::u256(30'000'000));
    h->setBaseFee(bcos::u256(7));
    h->setCoinbase(bcos::Address{"0x4200000000000000000000000000000000000011"});
    h->setPrevRandao(bcos::h256{});
    h->setExtraData(bcos::bytes{});
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    return h;
}

/// Count every row currently in the storage.
size_t countRows(MutableStorage& storage)
{
    size_t n = 0;
    auto it = bcos::task::syncWait(storage.range());
    while (bcos::task::syncWait(it.next()))
        ++n;
    return n;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpCallWritebackTest)

/// F1 regression: a call=true transaction must leave the storage completely untouched. Before
/// the fix m_finish applied the masked diff (sender balance ~2^256) into storage, so this
/// would count rows that a dry run must never write.
BOOST_AUTO_TEST_CASE(CallPathLeavesStorageUnmodified)
{
    MutableStorage storage;
    auto header = makeCallHeader();

    bcos::ledger::LedgerConfig ledgerConfig;
    auto cfg = bcos::evm::opstack::jovianConfig();
    ledgerConfig.setEVMCRevision(cfg.rev);

    FakeTransaction tx;
    bcos::executor_v1::opstack::OpstackExecutor executor{
        bcos::evm::opstack::testutil::kOpTestReceiptFactory,
        std::make_shared<bcos::crypto::Keccak256>(), cfg};

    bcos::evm::opstack::OpFeeParams fee{};
    auto receipt = bcos::task::syncWait(executor.executeTransaction(storage, *header, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/true, fee, /*blockGasLeft=*/30'000'000,
        /*chainId=*/10, /*blockHashes=*/nullptr));

    BOOST_REQUIRE(receipt != nullptr);
    // The simulated tx executed (masked sender balance lets validation pass), but its state
    // diff — which carries the fabricated ~2^256 sender balance — must not be written back.
    BOOST_CHECK_EQUAL(countRows(storage), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
