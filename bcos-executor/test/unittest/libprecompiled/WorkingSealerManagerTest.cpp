#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "executive/TransactionExecutive.h"
#include "mock/MockLedger.h"
#include "precompiled/common/VRFInfo.h"
#include "precompiled/common/WorkingSealerManagerImpl.h"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <range/v3/algorithm/is_sorted.hpp>

namespace bcos::test
{
struct WorkingSealerManagerFixture
{
    bcos::crypto::Hash::Ptr hashImpl;

    WorkingSealerManagerFixture()
    {
        hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        executor::GlobalHashImpl::g_hashImpl = hashImpl;
    }
};

BOOST_FIXTURE_TEST_SUITE(WorkingSealerManagerTest, WorkingSealerManagerFixture)

class MockVRFInfo : public precompiled::VRFInfo
{
public:
    MockVRFInfo(
        bcos::bytes vrfProof, bcos::bytes vrfPk, bcos::bytes vrfInput, bcos::crypto::HashType hash)
      : VRFInfo(std::move(vrfProof), std::move(vrfPk), std::move(vrfInput)), m_hash(hash)
    {}
    bool verifyProof() override { return true; }
    bcos::crypto::HashType getHashFromProof() override { return m_hash; }
    bool isValidVRFPublicKey() override { return true; }

    bcos::crypto::HashType m_hash;
};

BOOST_AUTO_TEST_CASE(testRotate)
{
    using namespace std::string_literals;

    task::syncWait([this]() -> task::Task<void> {
        precompiled::WorkingSealerManagerImpl workingSealerManager(true);
        workingSealerManager.setConfiguredEpochSealersSize(2);

        std::string node1(64, '0');
        std::string node2(64, '1');
        std::string node3(64, '2');
        std::string node4(64, '3');
        workingSealerManager.createVRFInfo(std::make_unique<MockVRFInfo>(
            bcos::bytes{}, bcos::bytes{node1.begin(), node1.end()}, bcos::bytes{}, 75));
        // 75 % 100 = 75 -- select node2
        // 75 % 80 = 75 -- select node3

        storage2::memory_storage::MemoryStorage<transaction_executor::StateKey, storage::Entry,
            storage2::memory_storage::Attribute::ORDERED>
            storage;

        // Should rotate
        storage::Entry notifyRotateEntry;
        ledger::SystemConfigEntry systemConfigEntry{"1", 0};
        notifyRotateEntry.setObject(systemConfigEntry);
        co_await storage2::writeOne(storage,
            transaction_executor::StateKey{
                ledger::SYS_CONFIG, ledger::INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE},
            notifyRotateEntry);

        // Node list
        consensus::ConsensusNodeList nodeList;
        nodeList.emplace_back(
            consensus::ConsensusNode{std::make_shared<crypto::KeyImpl>(fromHex(node1)),
                consensus::Type::consensus_candidate_sealer, 0, 70, 0});
        nodeList.emplace_back(
            consensus::ConsensusNode{std::make_shared<crypto::KeyImpl>(fromHex(node2)),
                consensus::Type::consensus_candidate_sealer, 0, 20, 0});
        nodeList.emplace_back(
            consensus::ConsensusNode{std::make_shared<crypto::KeyImpl>(fromHex(node3)),
                consensus::Type::consensus_candidate_sealer, 0, 7, 0});
        nodeList.emplace_back(
            consensus::ConsensusNode{std::make_shared<crypto::KeyImpl>(fromHex(node4)),
                consensus::Type::consensus_candidate_sealer, 0, 3, 0});
        co_await ledger::setNodeList(storage, nodeList);

        auto storageWrapper =
            std::make_shared<storage::LegacyStateStorageWrapper<std::decay_t<decltype(storage)>>>(
                storage);
        auto blockHeader = std::make_unique<bcostars::protocol::BlockHeaderImpl>(
            [m_header = bcostars::BlockHeader()]() mutable { return std::addressof(m_header); });
        bcos::protocol::ParentInfo parentInfo;
        blockHeader->setParentInfo(RANGES::views::single(parentInfo));
        blockHeader->calculateHash(*hashImpl);

        auto blockContext = std::make_unique<executor::BlockContext>(storageWrapper, nullptr,
            executor::GlobalHashImpl::g_hashImpl, *blockHeader, false, false);
        auto mockExecutive = std::make_shared<executor::TransactionExecutive>(
            *blockContext, "0x0", 0, 0, wasm::GasInjector{});
        auto execResult = std::make_shared<precompiled::PrecompiledExecResult>();
        execResult->m_origin = precompiled::covertPublicToHexAddress(node1);

        BOOST_CHECK_NO_THROW(
            co_await workingSealerManager.rotateWorkingSealer(mockExecutive, execResult));

        auto gotNodeList = co_await ledger::getNodeList(storage);
        BOOST_REQUIRE_EQUAL(gotNodeList.size(), 4);
        auto nodeIDView = ::ranges::views::transform(
            gotNodeList, [](auto const& node) { return node.nodeID->data(); });
        auto gotIDView = ::ranges::views::transform(
            gotNodeList, [](auto const& node) { return node.nodeID->data(); });
        BOOST_CHECK_EQUAL_COLLECTIONS(
            nodeIDView.begin(), nodeIDView.end(), gotIDView.begin(), gotIDView.end());
        BOOST_CHECK_EQUAL(gotNodeList[0].type, consensus::Type::consensus_candidate_sealer);
        BOOST_CHECK_EQUAL(gotNodeList[1].type, consensus::Type::consensus_sealer);  // 75 % 100 = 75
        BOOST_CHECK_EQUAL(gotNodeList[2].type, consensus::Type::consensus_sealer);  // 75 % 80 = 75
        BOOST_CHECK_EQUAL(gotNodeList[3].type, consensus::Type::consensus_candidate_sealer);

        // Test 0 termWeight
        std::string node5(64, '4');
        nodeList.emplace_back(
            consensus::ConsensusNode{std::make_shared<crypto::KeyImpl>(fromHex(node5)),
                consensus::Type::consensus_candidate_sealer, 0, 0, 0});
        co_await ledger::setNodeList(storage, nodeList);

        precompiled::WorkingSealerManagerImpl workingSealerManager2(true);
        workingSealerManager2.setConfiguredEpochSealersSize(2);
        workingSealerManager2.createVRFInfo(std::make_unique<MockVRFInfo>(
            bcos::bytes{}, bcos::bytes{node1.begin(), node1.end()}, bcos::bytes{}, 99));
        // 96 % 100 = 96 -- select node4
        // 96 % 97 = 96 -- select node3

        BOOST_CHECK_NO_THROW(
            co_await workingSealerManager2.rotateWorkingSealer(mockExecutive, execResult));
        gotNodeList = co_await ledger::getNodeList(storage);

        BOOST_REQUIRE_EQUAL(gotNodeList.size(), 5);
        auto gotIDView2 = ::ranges::views::transform(
            gotNodeList, [](auto const& node) { return node.nodeID->data(); });
        BOOST_CHECK_EQUAL_COLLECTIONS(
            nodeIDView.begin(), nodeIDView.end(), gotIDView2.begin(), gotIDView2.end());
        BOOST_CHECK_EQUAL(gotNodeList[0].type, consensus::Type::consensus_sealer);
        BOOST_CHECK_EQUAL(gotNodeList[1].type, consensus::Type::consensus_candidate_sealer);
        BOOST_CHECK_EQUAL(gotNodeList[2].type, consensus::Type::consensus_candidate_sealer);
        BOOST_CHECK_EQUAL(gotNodeList[3].type, consensus::Type::consensus_sealer);
        BOOST_CHECK_EQUAL(gotNodeList[4].type, consensus::Type::consensus_candidate_sealer);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test