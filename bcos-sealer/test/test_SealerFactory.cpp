#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-framework/testutils/faker/FakeConsensus.h"
#include "bcos-sealer/SealerConfig.h"
#include "bcos-sealer/SealerFactory.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
#include <optional>

using namespace bcos::storage;
using namespace std;

namespace bcos::test
{
struct TestSealerFactoryFixture
{
    TestSealerFactoryFixture()
    {
        boost::log::core::get()->set_logging_enabled(false);
        hashImpl = std::make_shared<crypto::Keccak256>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        assert(cryptoSuite);
    }


    ~TestSealerFactoryFixture() { boost::log::core::get()->set_logging_enabled(true); }
    inline protocol::BlockFactory::Ptr createBlockFactory()
    {
        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        auto transactionFactory =
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
        auto receiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
        blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
        return blockFactory;
    }
    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    protocol::BlockFactory::Ptr blockFactory;
    crypto::CryptoSuite::Ptr cryptoSuite = nullptr;
};

BOOST_FIXTURE_TEST_SUITE(TestSealerFactory, TestSealerFactoryFixture)


BOOST_AUTO_TEST_CASE(constructor)
{
    auto fixedSec1 = h256(
        "bcec428d5205abe0f0cc8a7340839"
        "08d9eb8563e31f943d760786edf42ad67dd");
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto sec1 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec1.asBytes());
    auto keyPair1 = signatureImpl->createKeyPair(sec1);
    auto blockFactory = createBlockFactory();
    auto sealerConfig =
        std::make_shared<bcos::sealer::SealerConfig>(blockFactory, nullptr, nullptr);
    sealerConfig->setChainId("chainId");
    BOOST_TEST(sealerConfig->chainId() == "chainId");
    sealerConfig->setGroupId("groupId");
    BOOST_TEST(sealerConfig->groupId() == "groupId");
    sealerConfig->setMinSealTime(1000);
    BOOST_TEST(sealerConfig->minSealTime() == 1000);
    bcos::crypto::KeyPairInterface::Ptr keyPair = std::move(keyPair1);
    sealerConfig->setKeyPair(keyPair);
    BOOST_TEST(sealerConfig->keyPair() == keyPair);
    sealerConfig->setConsensusInterface(nullptr);
    BOOST_TEST(sealerConfig->consensus() == nullptr);
    BOOST_TEST(sealerConfig->txpool() == nullptr);
    BOOST_TEST(sealerConfig->nodeTimeMaintenance() == nullptr);
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    auto factory = std::make_shared<bcos::sealer::SealerFactory>(
        nodeConfig, blockFactory, nullptr, nullptr, nullptr);
    auto sealer = factory->createSealer();

    consensus::ConsensusInterface::Ptr consensus = std::make_shared<test::FakeConsensus>();
    sealer->init(consensus);

    BOOST_TEST(sealer != nullptr);
    BOOST_TEST(sealer->hookWhenSealBlock(nullptr) == true);
    sealer->start();
    sealer->start();
    sealer->stop();
    auto vrfSealer = factory->createVRFBasedSealer();
    BOOST_TEST(vrfSealer != nullptr);
    vrfSealer->start();
    vrfSealer->stop();
    auto sealingManager = std::make_shared<bcos::sealer::SealingManager>(sealerConfig);
    BOOST_TEST(sealingManager != nullptr);
    sealingManager->stop();
    consensus->stop();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test