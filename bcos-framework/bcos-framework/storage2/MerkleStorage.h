#pragma once

#include "Storage.h"
#include "bcos-storage/bcos-storage/RocksDBStorage2.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace bcos::storage2::merkle_storage
{

enum class MPTNodeType : uint8_t
{
    branch = 0,
    extension = 1,
    leaf = 2,
};

using MPTNibble = uint8_t;
using MPTPath = std::vector<MPTNibble>;
using MPTNodeHash = crypto::HashType;

struct MPTAccountValue
{
    std::optional<storage::Entry> codeHash;
    std::optional<storage::Entry> code;
    std::optional<storage::Entry> balance;
    std::optional<storage::Entry> abi;
    std::optional<storage::Entry> nonce;
    std::optional<storage::Entry> alive;
    std::optional<storage::Entry> frozen;
    std::optional<storage::Entry> shard;
    std::optional<MPTNodeHash> storageRoot;
};

struct MPTNode
{
    constexpr static uint8_t VERSION = 1;

    MPTNodeType type = MPTNodeType::branch;
    MPTPath path;
    std::array<std::optional<MPTNodeHash>, 16> children;
    std::optional<MPTNodeHash> childHash;
    MPTAccountValue value;
    std::optional<storage::Entry> slotValue;
};

template <class TrieNodeStorage>
class MPT
{
public:
    using NodeStorage = TrieNodeStorage;

    MPT(NodeStorage& nodeStorage, crypto::Hash::Ptr hashImpl)
      : m_nodeStorage(nodeStorage), m_hashImpl(std::move(hashImpl))
    {}

    MPT(const MPT&) = delete;
    MPT& operator=(const MPT&) = delete;
    MPT(MPT&&) noexcept = default;
    MPT& operator=(MPT&&) noexcept = default;
    ~MPT() noexcept = default;

    task::Task<std::optional<MPTNode>> read(MPTNodeHash rootHash, std::string key)
    {
        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNodeHash>> write(
        MPTNodeHash rootHash, std::string key, std::string fieldKey, storage::Entry value)
    {
        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNodeHash>> clear(
        MPTNodeHash rootHash, std::string key, std::string fieldKey)
    {
        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNodeHash>> writeSlot(
        MPTNodeHash slotRootHash, std::string slotKey, storage::Entry slotValue)
    {
        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNodeHash>> clearSlot(
        MPTNodeHash slotRootHash, std::string slotKey)
    {
        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNodeHash>> writeStorageRoot(
        MPTNodeHash rootHash, std::string key, MPTNodeHash storageRoot)
    {
        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNode>> getNode(const MPTNodeHash& nodeHash)
    {
        co_return std::nullopt;
    }

    task::Task<MPTNodeHash> putNode(const MPTNode& node)
    {
        co_return MPTNodeHash{};
    }

private:
    template <class UpdateFn>
    task::Task<std::optional<MPTNodeHash>> writeTrieNode(
        MPTNodeHash rootHash, std::string key, UpdateFn&& updateFn)
    {
        co_return std::nullopt;
    }
    std::reference_wrapper<NodeStorage> m_nodeStorage;
    crypto::Hash::Ptr m_hashImpl;
};

template <class BackendStorage, class TrieNodeStorage>
class MerkleStorage
{
public:
    using Backend = std::remove_reference_t<BackendStorage>;
    using NodeStorage = std::remove_reference_t<TrieNodeStorage>;
    using Key = std::remove_cvref_t<typename Backend::Key>;
    using Value = std::remove_cvref_t<typename Backend::Value>;
    using Trie = MPT<NodeStorage>;

    explicit MerkleStorage(BackendStorage& backendStorage) : m_backendStorage(backendStorage) {}
    MerkleStorage(
        BackendStorage& backendStorage, TrieNodeStorage& nodeStorage, crypto::Hash::Ptr hashImpl)
      : m_backendStorage(backendStorage),
        m_trieNodeStorage(std::ref(nodeStorage)),
        m_mpt(std::in_place, m_trieNodeStorage->get(), std::move(hashImpl))
    {}

    MerkleStorage(const MerkleStorage&) = delete;
    MerkleStorage& operator=(const MerkleStorage&) = delete;
    MerkleStorage(MerkleStorage&&) noexcept = default;
    MerkleStorage& operator=(MerkleStorage&&) noexcept = default;
    ~MerkleStorage() noexcept = default;

    auto readOne(const auto& key) -> task::Task<std::optional<Value>>
    {
        co_return std::nullopt;
    }

    auto readOne(const auto& key, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<Value>>
    {
        co_return std::nullopt;
    }

    auto existsOne(const auto& key) -> task::Task<bool>
    {
        co_return false;
    }

    auto readSome(::ranges::input_range auto keys) -> task::Task<std::vector<std::optional<Value>>>
    {
        co_return {};
    }

    auto writeOne(auto key, auto value) -> task::Task<void>
    {
        co_return;
    }

    auto writeOne(auto key, auto value, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

    auto writeSome(::ranges::input_range auto keyValues) -> task::Task<void>
    {
        co_return;
    }

    auto writeSome(::ranges::input_range auto keyValues, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

    auto removeOne(auto key) -> task::Task<void>
    {
        co_return;
    }

    auto removeOne(auto key, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        co_return;
    }

    auto removeOne(auto key, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

    auto removeOne(auto key, const MPTNodeHash& stateRoot, DIRECT_TYPE /*unused*/)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

    auto removeSome(::ranges::input_range auto keys) -> task::Task<void>
    {
        co_return;
    }

    auto removeSome(::ranges::input_range auto keys, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        co_return;
    }

    auto removeSome(::ranges::input_range auto keys, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

    auto removeSome(
        ::ranges::input_range auto keys, const MPTNodeHash& stateRoot, DIRECT_TYPE /*unused*/)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

private:
    task::Task<void> removeOneImpl(auto key, bool direct)
    {
        co_return;
    }

    auto removeOneWithStateRoot(auto key, const MPTNodeHash& stateRoot, bool direct)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return std::nullopt;
    }

    std::reference_wrapper<Backend> m_backendStorage;
    std::optional<std::reference_wrapper<NodeStorage>> m_trieNodeStorage;
    std::optional<Trie> m_mpt;
};

}  // namespace bcos::storage2::merkle_storage
