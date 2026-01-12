#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "unittests/common/RPCFixture.h"
#include <bcos-framework/protocol/Transaction.h>
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

BOOST_AUTO_TEST_CASE(testBcosTransactionSignatureCheck) {}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test