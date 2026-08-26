/**
 * @file Web3AccessListResolverTest.cpp
 * @brief End-to-end: resolve typed Web3 tx → HostContext prepare() → access list warm.
 */

#include "bcos-executor/src/Web3AccessListResolver.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "Eip2929TestHelpers.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <cstring>
#include <memory>
#include <optional>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::hostcontext;

namespace bcos::test
{

class Web3AccessListResolverTEFixture
{
public:
    bcos::crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    MutableStorage storage;
    Rollbackable<decltype(storage)> rollbackableStorage;
    using MemoryStorageType =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue,
            bcos::storage2::memory_storage::Attribute(
                bcos::storage2::memory_storage::ORDERED |
                bcos::storage2::memory_storage::LOGICAL_DELETION)>;
    MemoryStorageType transientStorage;
    Rollbackable<MemoryStorageType> rollbackableTransientStorage;
    std::optional<PrecompiledManager> precompiledManager;
    bcos::ledger::LedgerConfig ledgerConfig;
    bcostars::protocol::BlockHeaderImpl blockHeader;
    int64_t seq = 0;

    Web3AccessListResolverTEFixture()
      : rollbackableStorage(storage), rollbackableTransientStorage(transientStorage)
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = hashImpl;
        precompiledManager.emplace(hashImpl);
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);
    }

    HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)> makeHost(
        bcos::ledger::Features const& features,
        std::shared_ptr<const bcos::executor::Eip2930AccessList> eip2930AccessList,
        uint8_t web3TypedTxKindForAccessList)
    {
        ledgerConfig.setFeatures(features);
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);

        evmc_address origin{};
        origin.bytes[19] = 0x01;
        evmc_address recipient{};
        recipient.bytes[19] = 0x02;

        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1'000'000,
            .recipient = recipient,
            .sender = {},
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = {},
            .code = nullptr,
            .code_size = 0};
        return HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>(
            rollbackableStorage, rollbackableTransientStorage, blockHeader, message, origin, "", 0,
            seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0, bcos::task::syncWait,
            std::move(eip2930AccessList), web3TypedTxKindForAccessList,
            std::make_shared<bcos::executor::Eip2929AccessState>());
    }
};

BOOST_FIXTURE_TEST_SUITE(Web3AccessListResolverTE, Web3AccessListResolverTEFixture)

BOOST_AUTO_TEST_CASE(Web3AccessListResolver_end_to_end_warm)
{
    // Same EIP-2930 fixture as bcos-rpc Web3TypeTest::testEIP2930Transaction
    constexpr std::string_view rawTx =
        "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec800"
        "00906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842"
        "a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000"
        "000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080"
        "a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd34"
        "7c5ed9577ef293a62787b4510465fadbfe39ee4094";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = ref(bytes);
    bcos::rpc::Web3Transaction w3{};
    BOOST_REQUIRE(bcos::codec::rlp::decode(bRef, w3) == nullptr);
    BOOST_CHECK(w3.type == bcos::rpc::TransactionType::EIP2930);

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    bcostars::protocol::TransactionImpl txImpl([tarsHolder]() { return tarsHolder.get(); });

    auto const resolved = bcos::executor::resolveWeb3AccessList(txImpl);
    BOOST_CHECK_EQUAL(
        resolved.web3TypedTxKind, static_cast<uint8_t>(bcos::rpc::TransactionType::EIP2930));
    BOOST_REQUIRE(resolved.accessList);
    BOOST_CHECK_EQUAL(resolved.accessList->size(), 2U);

    auto const features = eip2929::makeFeaturesPragueEip2929();
    auto host = makeHost(features, resolved.accessList, resolved.web3TypedTxKind);
    syncWait([&host]() -> task::Task<void> {
        co_await host.prepare();
        co_return;
    }());

    evmc_address const addr1 = unhexAddress("de0b295669a9fd93d5f28d9ec85e40f4cb697bae");
    evmc_address const addr2 = unhexAddress("bb9bc244d798123fde783fcc1c72d3bb8c189413");
    h256 const slot3(3);
    h256 const slot7(7);
    evmc_bytes32 evmKey3{};
    std::memcpy(evmKey3.bytes, slot3.data(), h256::SIZE);
    evmc_bytes32 evmKey7{};
    std::memcpy(evmKey7.bytes, slot7.data(), h256::SIZE);

    BOOST_CHECK_EQUAL(host.accessAccount(addr1), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(addr1, evmKey3), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(addr1, evmKey7), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(addr2), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
