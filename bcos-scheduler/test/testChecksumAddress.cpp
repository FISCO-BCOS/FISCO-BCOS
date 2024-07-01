#include "ExecutorManager.h"
#include "SchedulerImpl.h"
#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/protocol/TransactionSubmitResult.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "mock/MockExecutor.h"
#include "mock/MockExecutor3.h"
#include "mock/MockExecutorForCall.h"
#include "mock/MockExecutorForCreate.h"
#include "mock/MockExecutorForMessageDAG.h"
#include "mock/MockLedger.h"
#include "mock/MockMultiParallelExecutor.h"
#include "mock/MockRPC.h"
#include "mock/MockTransactionalStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionMetaDataImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/latch.hpp>
#include <future>
#include <memory>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
struct ChecksumFixture
{
    ChecksumFixture() { hashImpl = std::make_shared<Keccak256>(); }

    bcos::crypto::Hash::Ptr hashImpl;

    void check(const std::string& addr, uint64_t chainId = 0)
    {
        auto ret = addr;
        if (chainId != 0)
        {
            toCheckSumAddressWithChainId(ret, hashImpl, chainId);
        }
        else
        {
            toCheckSumAddress(ret, hashImpl);
        }
        BOOST_CHECK_EQUAL(addr, ret);
    }
};

BOOST_FIXTURE_TEST_SUITE(utils, ChecksumFixture)

BOOST_AUTO_TEST_CASE(checksumAddress)
{
    check("5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
    check("fB6916095ca1df60bB79Ce92cE3Ea74c37c5d359");
    check("dbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB");
    check("D1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb");
    check("fB6916095ca1df60bB79Ce92cE3Ea74c37c5d359");
    check("3C589CB0Be25f651b0563e052DEa63d3844C33e6");
}

BOOST_AUTO_TEST_CASE(checkEIP1191)
{
    // test case from https://eips.ethereum.org/EIPS/eip-1191
    auto const eth_mainnet = std::to_array<std::string>({
        "27b1fdb04752bbc536007a920d24acb045561c26",
        "3599689E6292b81B2d85451025146515070129Bb",
        "42712D45473476b98452f434e72461577D686318",
        "52908400098527886E0F7030069857D2E4169EE7",
        "5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed",
        "6549f4939460DE12611948b3f82b88C3C8975323",
        "66f9664f97F2b50F62D13eA064982f936dE76657",
        "8617E340B3D01FA5F11F306F4090FD50E238070D",
        "88021160C5C792225E4E5452585947470010289D",
        "D1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb",
        "dbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB",
        "de709f2102306220921060314715629080e2fb77",
        "fB6916095ca1df60bB79Ce92cE3Ea74c37c5d359",
    });

    for (auto& addr : eth_mainnet)
    {
        check(addr, 1);
    }

    auto const rsk_mainnet = std::to_array<std::string>({
        "27b1FdB04752BBc536007A920D24ACB045561c26",
        "3599689E6292B81B2D85451025146515070129Bb",
        "42712D45473476B98452f434E72461577d686318",
        "52908400098527886E0F7030069857D2E4169ee7",
        "5aaEB6053f3e94c9b9a09f33669435E7ef1bEAeD",
        "6549F4939460DE12611948B3F82B88C3C8975323",
        "66F9664f97f2B50F62d13EA064982F936de76657",
        "8617E340b3D01Fa5f11f306f4090fd50E238070D",
        "88021160c5C792225E4E5452585947470010289d",
        "D1220A0Cf47c7B9BE7a2e6ba89F429762E7B9adB",
        "DBF03B407c01E7CD3cBea99509D93F8Dddc8C6FB",
        "De709F2102306220921060314715629080e2FB77",
        "Fb6916095cA1Df60bb79ce92cE3EA74c37c5d359",
    });

    for (auto& addr : rsk_mainnet)
    {
        check(addr, 30);
    }

    auto const rsk_testnet = std::to_array<std::string>({
        "27B1FdB04752BbC536007a920D24acB045561C26",
        "3599689e6292b81b2D85451025146515070129Bb",
        "42712D45473476B98452F434E72461577D686318",
        "52908400098527886E0F7030069857D2e4169EE7",
        "5aAeb6053F3e94c9b9A09F33669435E7EF1BEaEd",
        "6549f4939460dE12611948b3f82b88C3c8975323",
        "66f9664F97F2b50f62d13eA064982F936DE76657",
        "8617e340b3D01fa5F11f306F4090Fd50e238070d",
        "88021160c5C792225E4E5452585947470010289d",
        "d1220a0CF47c7B9Be7A2E6Ba89f429762E7b9adB",
        "dbF03B407C01E7cd3cbEa99509D93f8dDDc8C6fB",
        "DE709F2102306220921060314715629080e2Fb77",
        "Fb6916095CA1dF60bb79CE92ce3Ea74C37c5D359",
    });

    for (auto& addr : rsk_testnet)
    {
        check(addr, 31);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test