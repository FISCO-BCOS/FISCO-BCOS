#pragma once

// OpTestReceiptFactory — test-side receipt factory. opTransition and
// runDeposit now inject a bcos::protocol::TransactionReceiptFactory::Ptr (they produce FISCO
// receipts directly); the tests build the real bcostars implementation over a Keccak256 suite,
// the same construction bcos-tars-protocol's own TransactionReceiptImplTest uses.

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace bcos::evm::opstack::testutil
{
/// One shared receipt factory for the whole suite (cheap: the factory is stateless beyond the
/// crypto suite; the receipt objects it creates are independent).
inline bcos::protocol::TransactionReceiptFactory::Ptr makeOpTestReceiptFactory()
{
    auto suite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
}

/// intx::uint256 → bcos::u256, full-width big-endian conversion (same pattern as the production
/// helper in OpTransition.cpp; duplicated here so tests can compare opStackMeta fields against
/// the intx values the fee code computes).
inline bcos::u256 bcosU256FromIntx(intx::uint256 const& val)
{
    auto be = intx::be::store<evmc::uint256be>(val);
    return bcos::fromBigEndian<bcos::u256>(
        bcos::bytesConstRef{reinterpret_cast<bcos::byte const*>(be.bytes), sizeof(be.bytes)});
}

/// One shared factory instance across test TUs (inline variable: one per program, stateless).
inline const bcos::protocol::TransactionReceiptFactory::Ptr kOpTestReceiptFactory =
    makeOpTestReceiptFactory();
}  // namespace bcos::evm::opstack::testutil
