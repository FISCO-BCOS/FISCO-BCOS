/**
 * @file EvmExecutionPathTest.cpp
 * @brief VMFactory CREATE/CALL path and baseline analysis smoke tests.
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "vm/VMFactory.h"
#include <boost/algorithm/hex.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
bytes hexToBytes(std::string_view hex)
{
    bytes out;
    boost::algorithm::unhex(hex.begin(), hex.end(), std::back_inserter(out));
    return out;
}

// Inner HelloWorld runtime from AuthPrecompiledTest::helloBin (after 0xf3fe marker).
constexpr std::string_view kHelloWorldRuntimeHex =
    "608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4c"
    "e63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064"
    "010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302"
    "840111640100000000831117156100a257600080fd5b91908080601f01602080910402602001604051908101604052"
    "8093929190818152602001838380828437600081840152601f19601f82011690508083019250505050505050919291"
    "9290505050610179565b005b6100fe610193565b604051808060200182810382528381815181526020019150805190"
    "6020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190"
    "601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390"
    "f35b806000908051906020019061018f929190610235565b5050565b60606000805460018160011615610100020316"
    "6002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561"
    "010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b82019190"
    "6000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090"
    "565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061"
    "027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a35782"
    "51825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b8082111561"
    "02d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d20"
    "5b50ba5d4dba42f506f9ea3628eb3d0299c9c28d5664736f6c634300060a0033";
}  // namespace

BOOST_AUTO_TEST_SUITE(EvmExecutionPathTest)

BOOST_AUTO_TEST_CASE(baselineAnalyze_helloWorldRuntime_doesNotThrow)
{
    auto code = hexToBytes(kHelloWorldRuntimeHex);
    BOOST_REQUIRE_NO_THROW(evmone::baseline::analyze(evmone::bytes_view{code.data(), code.size()}));
}

BOOST_AUTO_TEST_CASE(vmFactory_cacheKeyDistinguishesRevision)
{
    VMFactory factory{4};
    auto code = hexToBytes(kHelloWorldRuntimeHex);
    Keccak256 hasher;
    auto codeHash = hasher.hash(bytesConstRef(code.data(), code.size()));

    auto analysisLondon = std::make_shared<evmoneCodeAnalysis>(
        evmone::baseline::analyze(evmone::bytes_view{code.data(), code.size()}));
    factory.put({codeHash, EVMC_LONDON}, analysisLondon);

    BOOST_CHECK(factory.get({codeHash, EVMC_LONDON}) != nullptr);
    BOOST_CHECK(factory.get({codeHash, EVMC_SHANGHAI}) == nullptr);
}

BOOST_AUTO_TEST_CASE(vmFactory_createCallPath_buildsInstances)
{
    VMFactory factory;
    auto code = hexToBytes(kHelloWorldRuntimeHex);
    Keccak256 hasher;
    auto codeHash = hasher.hash(bytesConstRef(code.data(), code.size()));

    bytes_view codeView(code.data(), code.size());
    auto createVm =
        factory.create(VMKind::evmone, EVMC_SHANGHAI, crypto::HashType{}, codeView, true);
    auto callVm = factory.create(VMKind::evmone, EVMC_SHANGHAI, codeHash, codeView, false);

    // Smoke: both paths must be constructible (execute needs a full HostContext).
    boost::ignore_unused(createVm, callVm);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
