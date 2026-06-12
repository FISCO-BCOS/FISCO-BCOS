/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE path: modexp (0x05) rejects EIP-7823 oversize input on Osaka+.
 *  @file Modexp7823TeTest.cpp
 */

#include "../bcos-transaction-executor/precompiled/PrecompiledImpl.h"
#include "bcos-executor/src/vm/ModexpGas.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

namespace
{
bytes modexpHeaderBaseLen1025()
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    return input;
}

ledger::Features osakaFeatures()
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    features.set(ledger::Features::Flag::feature_evm_osaka);
    features.set(ledger::Features::Flag::bugfix_v1_error_handling);
    return features;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Modexp7823Te)

BOOST_AUTO_TEST_CASE(callBuiltinPrecompiled_rejects_oversize_osaka)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    executor::PrecompiledContract const contract(executor::PrecompiledRegistrar::pricer("modexp"),
        executor::PrecompiledRegistrar::executor("modexp"));

    auto const input = modexpHeaderBaseLen1025();
    evmc_address modexpAddr{};
    modexpAddr.bytes[19] = 0x05;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.code_address = modexpAddr;
    message.gas = 1'000'000;
    message.input_data = input.data();
    message.input_size = input.size();

    auto const features = osakaFeatures();
    auto const result =
        executor_v1::callBuiltinPrecompiled(contract, message, features, EVMC_OSAKA);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_FAILURE);
    BOOST_CHECK_EQUAL(result.gas_left, 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
