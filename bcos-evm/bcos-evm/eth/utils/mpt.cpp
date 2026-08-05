// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "mpt.hpp"
#include "mpt_hash.hpp"
#include "rlp.hpp"
#include <algorithm>
#include <cassert>

namespace evmone::state
{
namespace
{
/// The MPT node kind.
enum class Kind : bool
{
    leaf,
    branch
};

/// The collection of nibbles (4-bit values) representing a path in a MPT.
///
/// TODO(c++26): This is an instance of std::inplace_vector.
class Path
{
    static constexpr size_t MAX_SIZE = 64;

    size_t m_size = 0;  // TODO: Can be converted to uint8_t.
    uint8_t m_nibbles[MAX_SIZE]{};

public:
    Path() = default;

    /// Constructs a path from a pair of iterators.
    Path(const uint8_t* first, const uint8_t* last) noexcept
      : m_size(static_cast<size_t>(last - first))
    {
        assert(m_size <= std::size(m_nibbles));
        std::copy(first, last, m_nibbles);
    }

    /// Constructs a path from bytes - each byte will produce 2 nibbles in the path.
    explicit Path(bytes_view key) noexcept : m_size{2 * key.size()}
    {
        assert(m_size <= std::size(m_nibbles) && "a keys must not be longer than 32 bytes");
        size_t i = 0;
        for (const auto b : key)
        {
            m_nibbles[i++] = b >> 4;
            m_nibbles[i++] = b & 0x0f;
        }
    }

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] const uint8_t* begin() const noexcept { return m_nibbles; }
    [[nodiscard]] const uint8_t* end() const noexcept { return m_nibbles + m_size; }

    [[nodiscard]] bytes encode(Kind kind) const
    {
        if (kind == Kind::branch && m_size == 0)
            return {};

        const auto kind_prefix = kind == Kind::leaf ? 0x20 : 0x00;
        const auto has_odd_size = m_size % 2 != 0;
        const auto nibble_prefix = has_odd_size ? (0x10 | m_nibbles[0]) : 0x00;

        bytes encoded{static_cast<uint8_t>(kind_prefix | nibble_prefix)};
        for (auto i = size_t{has_odd_size}; i < m_size; i += 2)
            encoded.push_back(static_cast<uint8_t>((m_nibbles[i] << 4) | m_nibbles[i + 1]));
        return rlp::encode(encoded);
    }
};
}  // namespace

/// The MPT Node.
class MPTNode
{
    Kind m_kind = Kind::leaf;
    Path m_path;
    bytes m_value;
    std::unique_ptr<MPTNode> m_children[16];

    /// Creates a branch node out of two children and an optional extended path.
    MPTNode(const Path& path, size_t idx1, MPTNode&& child1, size_t idx2, MPTNode&& child2) noexcept
      : m_kind{Kind::branch}, m_path{path}
    {
        assert(idx1 != idx2);
        assert(idx1 < std::size(m_children));
        assert(idx2 < std::size(m_children));

        m_children[idx1] = std::make_unique<MPTNode>(std::move(child1));
        m_children[idx2] = std::make_unique<MPTNode>(std::move(child2));
    }

public:
    /// Creates new leaf node.
    MPTNode(const Path& path, bytes&& value) noexcept : m_path{path}, m_value{std::move(value)} {}

    void insert(const Path& path, bytes&& value);

    [[nodiscard]] bytes encode() const;
};

void MPTNode::insert(const Path& path, bytes&& value)  // NOLINT(misc-no-recursion)
{
    // The insertion is all about branch nodes. In happy case we will find an empty slot
    // in an existing branch node. Otherwise, we need to create new branch node
    // (possibly with an extended path) and transform existing nodes around it.

    // Let's consider the following branch node with extended path "ab".
    //
    //     |
    //     |a ↙③
    //     |b
    //     |
    // [a|b|c|d]
    //  |     ②
    //  ①
    //
    // If the insert path prefix matches the "ab" we insert to one of the children:
    // - e.g. for "aba" insert into existing child ①,
    // - e.g. for "abd" create new leaf node ②.
    // If the insert path prefix doesn't match "ab" we split the extended path by
    // a new branch node of the "this" branch node and a new leaf node.
    // E.g. for "acd" insert new branch node "a" at ③ with:
    // - at "b" : the "this" branch node with empty extended path "",
    // - at "c" : the new leaf node with path "d".

    const auto [this_idx_it, insert_idx_it] = std::ranges::mismatch(m_path, path);

    // insert_idx_it is always valid if requirements are fulfilled:
    // - if m_path is not shorter than path they must have mismatched nibbles,
    //   given the requirement of key uniqueness and not being a prefix if existing key,
    // - if m_path is shorter and matches the path prefix
    //   then insert_idx_it points at path[m_path.size()].
    assert(insert_idx_it != path.end() && "a key must not be a prefix of another key");
    const Path insert_tail{insert_idx_it + 1, path.end()};

    if (m_kind == Kind::branch && this_idx_it == m_path.end())  // Paths match: go into the child.
    {
        if (auto& child = m_children[*insert_idx_it]; child)
            child->insert(insert_tail, std::move(value));  // ①
        else
            child = std::make_unique<MPTNode>(insert_tail, std::move(value));  // ②
    }
    else  // ③: Shorten path of this node and insert it to the new branch node.
    {
        const auto this_idx = *this_idx_it;
        const Path extended_path{m_path.begin(), this_idx_it};
        const Path this_node_tail{this_idx_it + 1, m_path.end()};
        auto this_node = std::move(*this);  // invalidates this_idx_it
        this_node.m_path = this_node_tail;
        *this = MPTNode(extended_path, this_idx, std::move(this_node), *insert_idx_it,
            MPTNode{insert_tail, std::move(value)});
    }
}


bytes MPTNode::encode() const  // NOLINT(misc-no-recursion)
{
    static constexpr uint8_t EMPTY = 0x80;  // encoded empty child

    static constexpr auto shorten = [](bytes&& b) {
        return (b.size() < 32) ? std::move(b) : rlp::encode(keccak256(b));
    };

    bytes encoded;  // the encoded content of the node without its path
    switch (m_kind)
    {
    case Kind::leaf:
        encoded = rlp::encode(m_value);
        break;
    case Kind::branch:
        for (const auto& child : m_children)
            encoded += child ? shorten(child->encode()) : bytes{EMPTY};
        encoded += EMPTY;  // end indicator

        if (!m_path.empty())  // extended node
            encoded = shorten(rlp::internal::wrap_list(encoded));
        break;
    }
    return rlp::internal::wrap_list(m_path.encode(m_kind) + encoded);
}


MPT::MPT() noexcept = default;
MPT::~MPT() noexcept = default;

void MPT::insert(bytes_view key, bytes&& value)
{
    const Path path{key};

    if (m_root == nullptr)
        m_root = std::make_unique<MPTNode>(path, std::move(value));
    else
        m_root->insert(path, std::move(value));
}

[[nodiscard]] hash256 MPT::hash() const
{
    if (m_root == nullptr)
        return EMPTY_MPT_HASH;
    return keccak256(m_root->encode());
}

}  // namespace evmone::state
