#include "bcos-sealer/Sealer.h"
#include "SealingManager.h"
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace bcos::test
{

struct FakeTxPool : public txpool::TxPoolInterface
{
    std::tuple<bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr> sealTxs(
        uint64_t _txsLimit, txpool::TxsHashSetPtr _avoidTxs) override
    {
        return {};
    }
};

struct FakeSealingManager : public sealer::SealingManager
{
    
};

struct TestSealerFixture
{
    TestSealerFixture()
    {
        hashImpl = std::make_shared<crypto::Keccak256>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        assert(cryptoSuite);

        auto txpool = std::make_shared<FakeTxPool>();
        sealerConfig = std::make_shared<sealer::SealerConfig>(nullptr, txpool, nullptr);
        sealer = std::make_shared<sealer::Sealer>(sealerConfig);
    }

    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    protocol::BlockFactory::Ptr blockFactory;
    crypto::CryptoSuite::Ptr cryptoSuite = nullptr;
    sealer::SealerConfig::Ptr sealerConfig;
    sealer::Sealer::Ptr sealer;
};

BOOST_FIXTURE_TEST_SUITE(TestSealerFactory, TestSealerFixture)

BOOST_AUTO_TEST_CASE(constructor) {}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test