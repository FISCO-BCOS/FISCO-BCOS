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
    "608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b578063"
    "6d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b8101908080359060"
    "20019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001"
    "918460018302840111640100000000831117156100a257600080fd5b91908080601f0160208091040260200160"
    "405190810160405280939291908181526020018383808284378600081840152601f19601f820116905080830192"
    "505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283"
    "818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050"
    "610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260"
    "200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b"
    "606060008054600181600116156101000203166002900480601f0160208091040260200160405190810160405280"
    "9291908181526020018280546001816001161561010002031660029004801561022b5780601f1061020057610100"
    "80835404028352916020019161022b565b820191906000526020600020905b8154815290600101906020018083"
    "1161020e57829003601f168201915b5050505050905090565b8280546001816001161561010002031660029004"
    "90600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b"
    "828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b"
    "5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102"
    "bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d205b50ba5d4dba42f506f9ea3628"
    "eb3d0299c9c28d5664736f6c634300060a0033";
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
