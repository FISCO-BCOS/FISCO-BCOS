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
    MockVRFInfo(bcos::bytes vrfProof, bcos::bytes vrfPk, bcos::bytes vrfInput)
      : VRFInfo(std::move(vrfProof), std::move(vrfPk), std::move(vrfInput))
    {}
    bool verifyProof() override { return true; }
    bcos::crypto::HashType getHashFromProof() override { return 75; }
    bool isValidVRFPublicKey() override { return true; }
};

BOOST_AUTO_TEST_CASE(testRotate)
{
    using namespace std::string_literals;

    task::syncWait([this]() -> task::Task<void> {
        precompiled::WorkingSealerManagerImpl workingSealerManager(true);
        workingSealerManager.m_configuredEpochSealersSize = 2;

        std::string node1(64, '1');
        std::string node2(64, '2');
        std::string node3(64, '3');
        std::string node4(64, '4');
        workingSealerManager.m_vrfInfo = std::make_shared<MockVRFInfo>(
            bcos::bytes{}, bcos::bytes{node1.begin(), node1.end()}, bcos::bytes{});

        storage2::memory_storage::MemoryStorage<transaction_executor::StateKey, storage::Entry>
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
        nodeList.emplace_back(std::make_shared<crypto::KeyImpl>(fromHex(node1)),
            consensus::Type::consensus_candidate_sealer, 0, 70, 0);
        nodeList.emplace_back(std::make_shared<crypto::KeyImpl>(fromHex(node2)),
            consensus::Type::consensus_candidate_sealer, 0, 20, 0);
        nodeList.emplace_back(std::make_shared<crypto::KeyImpl>(fromHex(node3)),
            consensus::Type::consensus_candidate_sealer, 0, 7, 0);
        nodeList.emplace_back(std::make_shared<crypto::KeyImpl>(fromHex(node4)),
            consensus::Type::consensus_candidate_sealer, 0, 3, 0);
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

        BOOST_CHECK_NO_THROW(workingSealerManager.rotateWorkingSealer(mockExecutive, execResult));

        auto gotNodeList = co_await ledger::getNodeList(storage, 0);
        BOOST_REQUIRE_EQUAL(gotNodeList.size(), 4);
        BOOST_CHECK_EQUAL(gotNodeList[0].nodeID->hex(), node1);
        BOOST_CHECK_EQUAL(gotNodeList[0].type, consensus::Type::consensus_candidate_sealer);
        BOOST_CHECK_EQUAL(gotNodeList[1].nodeID->hex(), node2);
        BOOST_CHECK_EQUAL(gotNodeList[1].type, consensus::Type::consensus_sealer);  // 75 % 100 = 75
        BOOST_CHECK_EQUAL(gotNodeList[2].nodeID->hex(), node3);
        BOOST_CHECK_EQUAL(gotNodeList[2].type, consensus::Type::consensus_sealer);  // 75 % 80 = 75
        BOOST_CHECK_EQUAL(gotNodeList[3].nodeID->hex(), node4);
        BOOST_CHECK_EQUAL(gotNodeList[3].type, consensus::Type::consensus_candidate_sealer);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test