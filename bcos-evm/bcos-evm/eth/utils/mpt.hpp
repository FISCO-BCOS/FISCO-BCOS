// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <bcos-evm/eth/state/hash_utils.hpp>
#include <memory>

namespace evmone::state
{
/// Insert-only Merkle Patricia Trie implementation for getting the root hash
/// out of (key, value) pairs.
///
/// Limitations:
/// 1. A key must not be longer than 32 bytes. Protected by debug assert.
/// 2. A key must not be a prefix of another key. Protected by debug assert.
///    This comes from the spec (Yellow Paper Appendix D) - a branch node cannot store a value.
/// 3. A key must be unique. Protected by debug assert.
///    I.e. inserted values cannot be updated by inserting with the same key again.
/// 4. Inserted values cannot be erased. There is no method for this.
class MPT
{
    std::unique_ptr<class MPTNode> m_root;

public:
    MPT() noexcept;
    ~MPT() noexcept;

    void insert(bytes_view key, bytes&& value);

    [[nodiscard]] hash256 hash() const;
};

}  // namespace evmone::state
