// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.cpp
#include <bcos-evm/adapter/StateRootCompute.h>

#include <bcos-ledger/mpt/HashBuilder.h>
#include <cstring>

namespace bcos::evm
{
evmone::hash256 accountStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Secure-trie over one account's live slot map: key = keccak256(slot), value = rlp(trimmed
    // value). Same construction as the retired evmone mpt_hash.cpp:13-24 and
    // bcos-evm/opstack/OpBlockSeal.cpp::opStorageRoot (logic duplicated rather than called
    // through — see the StateRootCompute.h doc comment). Defensive continue on zero values.
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        bcos::bytes leaf;
        bcos::codec::rlp::encode(
            leaf, trimmedBigEndian(bcos::bytesConstRef{value.bytes, sizeof(value.bytes)}));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    evmone::hash256 root{};
    std::memcpy(root.bytes, result.root.data(), sizeof(root.bytes));
    return root;
}
}  // namespace bcos::evm
