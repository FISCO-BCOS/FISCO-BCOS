#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-ledger/mpt/NodeStorageCodec.h>
#include <bcos-ledger/mpt/TrieNode.h>
#include <boost/test/unit_test.hpp>
#include <limits>

namespace bcos::ledger::mpt::test
{
BOOST_AUTO_TEST_SUITE(NodeStorageCodecSuite)

BOOST_AUTO_TEST_CASE(RoundTripPreservesRawNodeRlp)
{
    ExtensionNode extension;
    extension.sharedNibbles = {1, 2, 3};
    extension.child = {0xa0};
    extension.child.insert(extension.child.end(), 32, 0x44);
    auto const raw = encodeRaw(TrieNode{extension});
    for (auto const& [refCount, deadline] : {std::pair<uint64_t, uint64_t>{1, 0}, {2, 100},
             {std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()}})
    {
        auto stored = encodeStoredNode(bcos::ref(raw), refCount, deadline);
        auto decoded = decodeStoredNode(bcos::ref(stored));
        BOOST_CHECK_EQUAL(decoded.refCount, refCount);
        BOOST_CHECK_EQUAL(decoded.deadline, deadline);
        BOOST_CHECK(decoded.rlp == raw);
        BOOST_CHECK(
            std::get<ExtensionNode>(decodeNode(bcos::ref(decoded.rlp))).child == extension.child);
    }
}

BOOST_AUTO_TEST_CASE(RejectsRawNodeAndMalformedValues)
{
    LeafNode leaf;
    leaf.keyNibbles = {1};
    leaf.value = {0x42};
    auto const raw = encodeRaw(TrieNode{leaf});
    BOOST_CHECK_THROW(decodeStoredNode(bcos::ref(raw)), bcos::storage2::MPTNodeStorageError);
    BOOST_CHECK_THROW(decodeStoredNode(bcos::bytesConstRef{}), bcos::storage2::MPTNodeStorageError);
    auto stored = encodeStoredNode(bcos::ref(raw), 1);
    stored.push_back(0x00);
    BOOST_CHECK_THROW(decodeStoredNode(bcos::ref(stored)), bcos::storage2::MPTNodeStorageError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ledger::mpt::test
