#include "bcos-crypto/interfaces/crypto/Hash.h"
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

class MockBlockExecutive : public executor::TransactionExecutive
{
public:
    MockBlockExecutive(executor::BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector)
      : executor::TransactionExecutive(
            blockContext, std::move(contractAddress), contextID, seq, gasInjector)
    {}
};

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
        ledger::ConsensusNodeList nodeList;
        nodeList.emplace_back(node1, 1, std::string(ledger::CONSENSUS_CANDIDATE_SEALER), "0"s, 70);
        nodeList.emplace_back(node2, 1, std::string(ledger::CONSENSUS_CANDIDATE_SEALER), "0"s, 20);
        nodeList.emplace_back(node3, 1, std::string(ledger::CONSENSUS_CANDIDATE_SEALER), "0"s, 7);
        nodeList.emplace_back(node4, 1, std::string(ledger::CONSENSUS_CANDIDATE_SEALER), "0"s, 3);
        storage::Entry consensusNodeListEntry;
        consensusNodeListEntry.setObject(nodeList);
        co_await storage2::writeOne(storage,
            transaction_executor::StateKey{ledger::SYS_CONSENSUS, "key"}, consensusNodeListEntry);

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
        auto mockExecutive =
            std::make_shared<MockBlockExecutive>(*blockContext, "0x0", 0, 0, wasm::GasInjector{});
        auto execResult = std::make_shared<precompiled::PrecompiledExecResult>();
        execResult->m_origin = precompiled::covertPublicToHexAddress(node1);

        BOOST_CHECK_NO_THROW(workingSealerManager.rotateWorkingSealer(mockExecutive, execResult));

        auto gotNodeListEntry = co_await storage2::readOne(
            storage, transaction_executor::StateKeyView{ledger::SYS_CONSENSUS, "key"});
        BOOST_REQUIRE(gotNodeListEntry);
        auto gotNodeList = gotNodeListEntry->getObject<ledger::ConsensusNodeList>();

        BOOST_CHECK_EQUAL(gotNodeList.size(), 4);
        BOOST_CHECK_EQUAL(gotNodeList[0].nodeID, node1);
        BOOST_CHECK_EQUAL(gotNodeList[0].type, ledger::CONSENSUS_CANDIDATE_SEALER);
        BOOST_CHECK_EQUAL(gotNodeList[1].nodeID, node2);
        BOOST_CHECK_EQUAL(gotNodeList[1].type, ledger::CONSENSUS_SEALER);  // 75 % 100 = 75
        BOOST_CHECK_EQUAL(gotNodeList[2].nodeID, node3);
        BOOST_CHECK_EQUAL(gotNodeList[2].type, ledger::CONSENSUS_SEALER);  // 75 % 80 = 75
        BOOST_CHECK_EQUAL(gotNodeList[3].nodeID, node4);
        BOOST_CHECK_EQUAL(gotNodeList[3].type, ledger::CONSENSUS_CANDIDATE_SEALER);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test