/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-protocol/TransactionStatus.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::protocol;

namespace
{
struct Expectation
{
    TransactionStatus status;
    const char* label;
};

constexpr Expectation kExpectations[] = {
    {TransactionStatus::None, "None"},
    {TransactionStatus::OutOfGasLimit, "OutOfGasLimit"},
    {TransactionStatus::NotEnoughCash, "NotEnoughCash"},
    {TransactionStatus::BadInstruction, "BadInstruction"},
    {TransactionStatus::BadJumpDestination, "BadJumpDestination"},
    {TransactionStatus::OutOfGas, "OutOfGas"},
    {TransactionStatus::OutOfStack, "OutOfStack"},
    {TransactionStatus::StackUnderflow, "StackUnderflow"},
    {TransactionStatus::PrecompiledError, "PrecompiledError"},
    {TransactionStatus::RevertInstruction, "RevertInstruction"},
    {TransactionStatus::ContractAddressAlreadyUsed, "ContractAddressAlreadyUsed"},
    {TransactionStatus::PermissionDenied, "PermissionDenied"},
    {TransactionStatus::CallAddressError, "CallAddressError"},
    {TransactionStatus::GasOverflow, "GasOverflow"},
    {TransactionStatus::ContractFrozen, "ContractFrozen"},
    {TransactionStatus::AccountFrozen, "AccountFrozen"},
    {TransactionStatus::AccountAbolished, "AccountAbolished"},
    {TransactionStatus::ContractAbolished, "ContractAbolished"},
    {TransactionStatus::WASMValidationFailure, "WASMValidationFailure"},
    {TransactionStatus::WASMArgumentOutOfRange, "WASMArgumentOutOfRange"},
    {TransactionStatus::WASMUnreachableInstruction, "WASMUnreachableInstruction"},
    {TransactionStatus::WASMTrap, "WASMTrap"},
    {TransactionStatus::NonceCheckFail, "NonceCheckFail"},
    {TransactionStatus::BlockLimitCheckFail, "BlockLimitCheckFail"},
    {TransactionStatus::TxPoolIsFull, "TxPoolIsFull"},
    // Source intentionally maps Malformed to the legacy "MalformTx" label.
    {TransactionStatus::Malformed, "MalformTx"},
    {TransactionStatus::AlreadyInTxPool, "AlreadyInTxPool"},
    {TransactionStatus::TxAlreadyInChain, "TxAlreadyInChain"},
    {TransactionStatus::InvalidChainId, "InvalidChainId"},
    {TransactionStatus::InvalidGroupId, "InvalidGroupId"},
    {TransactionStatus::InvalidSignature, "InvalidSignature"},
    {TransactionStatus::RequestNotBelongToTheGroup, "RequestNotBelongToTheGroup"},
    {TransactionStatus::TransactionPoolTimeout, "TransactionPoolTimeout"},
    {TransactionStatus::AlreadyInTxPoolAndAccept, "AlreadyInTxPoolAndAccept"},
    {TransactionStatus::OverFlowValue, "OverFlowValue"},
    {TransactionStatus::MaxInitCodeSizeExceeded, "MaxInitCodeSizeExceeded"},
    {TransactionStatus::SenderNoEOA, "SenderNoEOA"},
    {TransactionStatus::InsufficientFunds, "InsufficientFunds"},
    // #5520 added the code and its operator<< case but no expectation here.
    {TransactionStatus::BlobTxNotAllowed, "BlobTxNotAllowed"},
    {TransactionStatus::TxTypeNotSupported, "TxTypeNotSupported"},
    {TransactionStatus::TipGreaterThanFeeCap, "TipGreaterThanFeeCap"},
    {TransactionStatus::CreateSetCodeTx, "CreateSetCodeTx"},
    {TransactionStatus::EmptyAuthorizationList, "EmptyAuthorizationList"},
    {TransactionStatus::NonceHasMaxValue, "NonceHasMaxValue"},
    {TransactionStatus::FeeCapLessThanBaseFee, "FeeCapLessThanBaseFee"},
    {TransactionStatus::MaxGasLimitExceeded, "MaxGasLimitExceeded"},
};
}  // namespace

BOOST_AUTO_TEST_SUITE(TransactionStatusTest)

BOOST_AUTO_TEST_CASE(toStringCoversAllNamedCases)
{
    for (auto const& expect : kExpectations)
    {
        BOOST_CHECK_EQUAL(toString(expect.status), expect.label);
    }
}

BOOST_AUTO_TEST_CASE(unknownAndDefaultFallToUnknown)
{
    BOOST_CHECK_EQUAL(toString(TransactionStatus::Unknown), "Unknown");
    // Cast a value not in the enum — exercises the `default` branch.
    auto unmapped = static_cast<TransactionStatus>(99999);
    BOOST_CHECK_EQUAL(toString(unmapped), "Unknown");
}

BOOST_AUTO_TEST_SUITE_END()
