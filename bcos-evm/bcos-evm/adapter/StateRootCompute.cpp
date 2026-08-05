// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.cpp
#include <bcos-evm/adapter/StateRootCompute.h>

// TODO(eth-utils-removal): 去除本 include,mpt_hash 调用改为自研 MPT 建根实现。
#include <bcos-evm/eth/utils/mpt_hash.hpp>

namespace bcos::evm
{
evmone::hash256 stateRootOf(const evmone::test::TestState& state)
{
    return evmone::state::mpt_hash(state);
}

evmone::hash256 accountStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Aligned with evmone mpt_hash.cpp:13-24 (private helper, not exported) and with
    // bcos-evm/opstack/OpBlockSeal.cpp::opStorageRoot (same construction, logic duplicated
    // rather than called through — see the StateRootCompute.h doc comment on this function for
    // the layering/upstream-diff-golden rationale): secure-trie key, trimmed value; a defensive
    // continue on zero values (semantically equivalent to "called after removal", matching
    // opStorageRoot's own defensive stance rather than upstream's debug-only assert).
    evmone::state::MPT trie;
    for (const auto& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        trie.insert(evmone::keccak256(evmc::bytes_view(key)),
            evmone::rlp::encode(evmone::rlp::trim(evmc::bytes_view(value))));
    }
    return trie.hash();
}
}  // namespace bcos::evm
