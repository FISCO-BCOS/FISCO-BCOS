// bcos-evm/test/opstack/OpEnvelopeToTarsTest.cpp
//
// D8 (whole-branch review, Important #2): the opEnvelopeToTars sender path
// `web3Tx.sender()` -> `fromHex` -> `tarsTx.sender` has no prior production consumer and is now on
// the block-commit hot path (registerOpBlock). This test drives the helper directly with a
// known-signed envelope and pins:
//   (a) tarsTx.sender == the 20 raw bytes of the recovered address (read side does
//       toHex(tx.sender()) then EIP-55 checksum, so a double-encoded hex string would corrupt it);
//   (b) tarsTx.extraTransactionHash == keccak(envelope) (D4).
//
// Envelope provenance: the envelope is produced in-test by constructing a Web3Transaction, signing
// `hashForSign()` with the repo's established fixed test private key
// 0xbcec428d...67dd (already used by RpcValidatorTest.cpp / CallValidatorTest.cpp), and calling
// Web3Transaction::encode() (full EIP-2718 envelope). The expected sender is derived INDEPENDENTLY
// from the public key (keccak(pubkey)[12:]) -- NOT via the same recover path `sender()` uses -- so
// a broken ecrecover or a broken 0x-stripping/fromHex step is actually caught.
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
/// The repo's established fixed test private key (also used by RpcValidatorTest.cpp /
/// CallValidatorTest.cpp). Its address is derived independently from the public key (see
/// assertOpEnvelopeSender), not via the recovery path under test.
constexpr char const* c_testSec =
    "bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd";

bcos::crypto::CryptoSuite::Ptr makeSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
        std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
}

/// Sign `tx` with `keyPair` (fills signatureR/S/V) and return the recovered raw 20-byte address.
bcos::Address signAndFill(bcos::rpc::Web3Transaction& tx, bcos::crypto::Secp256k1KeyPair& keyPair)
{
    auto suite = makeSuite();
    auto sig = suite->signatureImpl()->sign(keyPair, tx.hashForSign());
    BOOST_REQUIRE_EQUAL(sig->size(), crypto::SECP256K1_SIGNATURE_LEN);  // r(32) || s(32) ||
                                                                        // recid(1)
    tx.signatureR.assign(sig->begin(), sig->begin() + 32);
    tx.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    tx.signatureV = static_cast<uint8_t>((*sig)[64]);
    // Independent expected address: keccak(pubkey)[12:] -- NOT the recover path under test.
    return keyPair.address(suite->hashImpl());
}

/// Drive the D8 path end-to-end for one transaction kind: encode a signed Web3Transaction to a
/// full EIP-2718 envelope, run it through opEnvelopeToTars (which re-decodes the envelope and
/// re-derives the sender from the signature), and pin sender + tx hash.
void assertOpEnvelopeSender(TransactionType type)
{
    auto suite = makeSuite();
    h256 fixedSec(c_testSec);
    auto sec = std::make_shared<bcos::crypto::KeyImpl>(fixedSec.asBytes());
    bcos::crypto::Secp256k1KeyPair keyPair(sec);
    auto expectedAddress = keyPair.address(suite->hashImpl());

    bcos::rpc::Web3Transaction web3Tx;
    web3Tx.type = type;
    web3Tx.chainId = 0x2105;
    web3Tx.nonce = 0;
    web3Tx.gasLimit = 21000;
    web3Tx.to.emplace(bcos::Address("0x1234567890123456789012345678901234567890"));
    web3Tx.value = bcos::u256(1000);
    web3Tx.maxPriorityFeePerGas = bcos::u256(5);  // EIP-1559 priority fee / legacy gasPrice
    if (type == TransactionType::EIP1559)
    {
        web3Tx.maxFeePerGas = bcos::u256(10);
    }

    // signAndFill signs web3Tx in place; the D8 path inside opEnvelopeToTars re-derives the sender
    // from the signature. (F1-1: removed a self-comparing sanity check — recoveredAddress and
    // expectedAddress were the same expression, so it was trivially true and caught nothing.)
    signAndFill(web3Tx, keyPair);

    auto envelope = web3Tx.encode();
    auto txHash = bcos::crypto::keccak256Hash(bcos::ref(envelope));
    auto tarsTx = bcos::engine::detail::opEnvelopeToTars(envelope, txHash);
    BOOST_REQUIRE(tarsTx.has_value());

    // tarsTx byte containers are std::vector<tars::Char> (signed char); comparing them against
    // byte/uint8_t with std::equal's default operator== would mis-compare bytes >= 0x80, so use an
    // explicit unsigned comparison.
    auto sameByte = [](tars::Char c, bcos::byte b) { return static_cast<uint8_t>(c) == b; };

    // (a) D8: tarsTx.sender must be the raw 20 bytes, NOT a "0x"-prefixed hex string.
    BOOST_CHECK_EQUAL(tarsTx->sender.size(), 20u);
    BOOST_CHECK(std::equal(
        tarsTx->sender.begin(), tarsTx->sender.end(), expectedAddress.begin(), sameByte));
    // (b) D4: extraTransactionHash == keccak(envelope) == the canonical tx hash.
    BOOST_CHECK_EQUAL(tarsTx->extraTransactionHash.size(), txHash.size());
    BOOST_CHECK(std::equal(tarsTx->extraTransactionHash.begin(), tarsTx->extraTransactionHash.end(),
        txHash.begin(), sameByte));
    BOOST_CHECK(web3Tx.txHash() == txHash);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEnvelopeToTarsSuite)

BOOST_AUTO_TEST_CASE(Eip1559SenderPath)
{
    assertOpEnvelopeSender(TransactionType::EIP1559);
}

BOOST_AUTO_TEST_CASE(LegacySenderPath)
{
    assertOpEnvelopeSender(TransactionType::Legacy);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
