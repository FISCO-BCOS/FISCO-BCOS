// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file BlobSidecarRowTest.cpp
// @brief Round trip of the committed-block blob sidecar row (BlobSidecarRow.h) for both
// sidecar versions, and the malformed shapes the decoder refuses.
#include "bcos-rlp-protocol/BlobSidecarRow.h"
#include "bcos-rlp-protocol/Web3BlobTxWrapper.h"
#include <bcos-codec/rlp/Exceptions.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using bcos::rpc::decodeBlobSidecarRow;
using bcos::rpc::encodeBlobSidecarRow;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BlobSidecarRowTest)

namespace
{
engine::BlobsBundleV1 makeBundle(std::size_t blobCount, std::size_t proofsPerBlob)
{
    engine::BlobsBundleV1 bundle;
    for (std::size_t i = 0; i < blobCount; ++i)
    {
        bundle.blobs.emplace_back(rpc::BLOB_TX_BLOB_SIZE, byte{static_cast<uint8_t>(i + 1)});
        bundle.commitments.emplace_back(
            rpc::BLOB_TX_COMMITMENT_SIZE, byte{static_cast<uint8_t>(0xc0 + i)});
        for (std::size_t j = 0; j < proofsPerBlob; ++j)
        {
            bundle.proofs.emplace_back(
                rpc::BLOB_TX_PROOF_SIZE, byte{static_cast<uint8_t>(0x10 * i + (j % 16))});
        }
    }
    return bundle;
}
}  // namespace

BOOST_AUTO_TEST_CASE(roundTripBothSidecarVersions)
{
    for (auto const proofsPerBlob : {std::size_t{1}, engine::CELL_PROOFS_PER_BLOB})
    {
        auto const bundle = makeBundle(3, proofsPerBlob);
        auto const row = encodeBlobSidecarRow(bundle);
        auto const items = decodeBlobSidecarRow(bcos::bytesConstRef(row.data(), row.size()));
        BOOST_REQUIRE_EQUAL(items.size(), 3);
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            BOOST_CHECK_EQUAL(items[i].version, proofsPerBlob == 1 ? 0 : 1);
            BOOST_CHECK(items[i].commitment == bundle.commitments[i]);
            BOOST_CHECK(items[i].blob == bundle.blobs[i]);
            BOOST_REQUIRE_EQUAL(items[i].proofs.size(), proofsPerBlob);
            BOOST_CHECK(std::equal(items[i].proofs.begin(), items[i].proofs.end(),
                bundle.proofs.begin() + i * proofsPerBlob));
        }
    }
}

BOOST_AUTO_TEST_CASE(refusesMalformedRows)
{
    // A bundle whose proofs are not a whole number per blob cannot be encoded.
    auto odd = makeBundle(2, 1);
    odd.proofs.pop_back();
    BOOST_CHECK_THROW(encodeBlobSidecarRow(odd), bcos::Exception);
    BOOST_CHECK_THROW(encodeBlobSidecarRow(engine::BlobsBundleV1{}), bcos::Exception);

    // A proofs count that is neither 1 nor CELL_PROOFS_PER_BLOB decodes as malformed.
    auto const two = encodeBlobSidecarRow(makeBundle(1, 2));
    BOOST_CHECK_THROW(decodeBlobSidecarRow(bcos::bytesConstRef(two.data(), two.size())),
        codec::rlp::RlpDecodeException);
    // Trailing bytes after the row's list, and a string where the list should be.
    auto trailing = encodeBlobSidecarRow(makeBundle(1, 1));
    trailing.push_back(byte{0x80});
    BOOST_CHECK_THROW(decodeBlobSidecarRow(bcos::bytesConstRef(trailing.data(), trailing.size())),
        codec::rlp::RlpDecodeException);
    bytes const notAList{byte{0x83}, byte{0x01}, byte{0x02}, byte{0x03}};
    BOOST_CHECK_THROW(decodeBlobSidecarRow(bcos::bytesConstRef(notAList.data(), notAList.size())),
        codec::rlp::RlpDecodeException);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
