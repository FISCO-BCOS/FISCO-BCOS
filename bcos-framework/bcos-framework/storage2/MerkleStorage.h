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

namespace bcos::storage2
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

struct MPTNodeHashResolver
{
    std::string encode(const MPTNodeHash& nodeHash) const
    {
        return std::string(
            reinterpret_cast<const char*>(nodeHash.data()), nodeHash.size());
    }

    MPTNodeHash decode(std::string_view encoded) const
    {
        if (encoded.size() != MPTNodeHash::SIZE)
        {
            throw std::runtime_error("Invalid raw MPT node hash length");
        }

        MPTNodeHash nodeHash;
        std::memcpy(nodeHash.data(), encoded.data(), MPTNodeHash::SIZE);
        return nodeHash;
    }
};

struct MPTNodeValueResolver
{
    std::string encode(const storage::Entry& entry) const { return std::string(entry.get()); }

    storage::Entry decode(std::string_view encoded) const
    {
        return storage::Entry(std::string(encoded));
    }
};

using TrieNodeRocksDBStorage = bcos::storage2::rocksdb::RocksDBStorage2<MPTNodeHash,
    storage::Entry, MPTNodeHashResolver, MPTNodeValueResolver>;

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

    bool empty() const noexcept
    {
        return !codeHash && !code && !balance && !abi && !nonce && !alive && !frozen && !shard &&
               !storageRoot;
    }
};

struct MPTNode
{
    constexpr static uint8_t VERSION = 1;

    MPTNodeType type = MPTNodeType::branch;
    // `path` is meaningful for extension and leaf nodes. Branch nodes should keep it empty.
    MPTPath path;
    std::array<std::optional<MPTNodeHash>, 16> children;
    std::optional<MPTNodeHash> childHash;
    MPTAccountValue value;
    std::optional<storage::Entry> slotValue;

    storage::Entry flatten() const
    {
        std::string buffer;
        buffer.reserve(estimateEncodedSize());

        appendByte(buffer, VERSION);
        appendByte(buffer, static_cast<uint8_t>(type));
        appendPath(buffer, path);

        uint16_t childrenMask = 0;
        for (size_t index = 0; index < children.size(); ++index)
        {
            if (children[index].has_value())
            {
                childrenMask |= static_cast<uint16_t>(1U << index);
            }
        }
        appendUint16(buffer, childrenMask);
        for (auto const& child : children)
        {
            if (child)
            {
                appendHash(buffer, *child);
            }
        }

        appendOptionalHash(buffer, childHash);
        appendAccountValue(buffer, value);
        appendOptionalEntry(buffer, slotValue);

        return storage::Entry(std::move(buffer));
    }

    MPTNodeHash hash(const crypto::Hash& hashImpl) const
    {
        auto encoded = flatten();
        return hashImpl.hash(encoded.get());
    }

    static MPTNode restore(const storage::Entry& entry)
    {
        return restore(entry.get());
    }

    static MPTNode restore(std::string_view encoded)
    {
        Reader reader(encoded);

        auto version = reader.readByte();
        if (version != VERSION)
        {
            throw std::runtime_error("Unsupported MPT node encoding version");
        }

        MPTNode node;
        node.type = static_cast<MPTNodeType>(reader.readByte());
        node.path = reader.readPath();

        auto childrenMask = reader.readUint16();
        for (size_t index = 0; index < node.children.size(); ++index)
        {
            if ((childrenMask & static_cast<uint16_t>(1U << index)) != 0)
            {
                node.children[index] = reader.readHash();
            }
        }

        node.childHash = reader.readOptionalHash();
        node.value = reader.readAccountValue();
        node.slotValue = reader.readOptionalEntry();

        reader.ensureFullyConsumed();
        return node;
    }

private:
    struct Reader
    {
        explicit Reader(std::string_view data) : m_data(data) {}

        uint8_t readByte()
        {
            ensureAvailable(1);
            return static_cast<uint8_t>(m_data[m_offset++]);
        }

        uint16_t readUint16()
        {
            ensureAvailable(sizeof(uint16_t));
            uint16_t value = 0;
            value |= static_cast<uint16_t>(static_cast<uint8_t>(m_data[m_offset])) << 8U;
            value |= static_cast<uint16_t>(static_cast<uint8_t>(m_data[m_offset + 1]));
            m_offset += sizeof(uint16_t);
            return value;
        }

        uint32_t readUint32()
        {
            ensureAvailable(sizeof(uint32_t));
            uint32_t value = 0;
            value |= static_cast<uint32_t>(static_cast<uint8_t>(m_data[m_offset])) << 24U;
            value |= static_cast<uint32_t>(static_cast<uint8_t>(m_data[m_offset + 1])) << 16U;
            value |= static_cast<uint32_t>(static_cast<uint8_t>(m_data[m_offset + 2])) << 8U;
            value |= static_cast<uint32_t>(static_cast<uint8_t>(m_data[m_offset + 3]));
            m_offset += sizeof(uint32_t);
            return value;
        }

        std::string_view readBytes()
        {
            auto size = readUint32();
            ensureAvailable(size);
            auto view = m_data.substr(m_offset, size);
            m_offset += size;
            return view;
        }

        MPTPath readPath()
        {
            auto bytes = readBytes();
            return MPTPath(bytes.begin(), bytes.end());
        }

        MPTNodeHash readHash()
        {
            ensureAvailable(MPTNodeHash::SIZE);
            MPTNodeHash hash;
            std::memcpy(hash.data(), m_data.data() + m_offset, MPTNodeHash::SIZE);
            m_offset += MPTNodeHash::SIZE;
            return hash;
        }

        std::optional<MPTNodeHash> readOptionalHash()
        {
            if (readByte() == 0)
            {
                return std::nullopt;
            }
            return readHash();
        }

        std::optional<storage::Entry> readOptionalEntry()
        {
            if (readByte() == 0)
            {
                return std::nullopt;
            }
            return storage::Entry(std::string(readBytes()));
        }

        MPTAccountValue readAccountValue()
        {
            MPTAccountValue value;
            value.codeHash = readOptionalEntry();
            value.code = readOptionalEntry();
            value.balance = readOptionalEntry();
            value.abi = readOptionalEntry();
            value.nonce = readOptionalEntry();
            value.alive = readOptionalEntry();
            value.frozen = readOptionalEntry();
            value.shard = readOptionalEntry();
            value.storageRoot = readOptionalHash();
            return value;
        }

        void ensureFullyConsumed() const
        {
            if (m_offset != m_data.size())
            {
                throw std::runtime_error("Unexpected trailing bytes in encoded MPT node");
            }
        }

    private:
        void ensureAvailable(size_t size) const
        {
            if (size > (m_data.size() - m_offset))
            {
                throw std::runtime_error("Invalid encoded MPT node");
            }
        }

        std::string_view m_data;
        size_t m_offset = 0;
    };

    size_t estimateEncodedSize() const
    {
        size_t size = 0;
        size += 1;  // version
        size += 1;  // type
        size += sizeof(uint32_t) + path.size();
        size += sizeof(uint16_t);  // child mask
        for (auto const& child : children)
        {
            if (child)
            {
                size += MPTNodeHash::SIZE;
            }
        }
        size += 1 + (childHash ? MPTNodeHash::SIZE : 0);
        size += accountValueEncodedSize(value);
        size += optionalEntryEncodedSize(slotValue);
        return size;
    }

    static void appendByte(std::string& buffer, uint8_t value)
    {
        buffer.push_back(static_cast<char>(value));
    }

    static void appendUint16(std::string& buffer, uint16_t value)
    {
        buffer.push_back(static_cast<char>((value >> 8U) & 0xFFU));
        buffer.push_back(static_cast<char>(value & 0xFFU));
    }

    static void appendUint32(std::string& buffer, uint32_t value)
    {
        buffer.push_back(static_cast<char>((value >> 24U) & 0xFFU));
        buffer.push_back(static_cast<char>((value >> 16U) & 0xFFU));
        buffer.push_back(static_cast<char>((value >> 8U) & 0xFFU));
        buffer.push_back(static_cast<char>(value & 0xFFU));
    }

    static void appendBytes(std::string& buffer, std::string_view bytes)
    {
        if (bytes.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::runtime_error("MPT node field is too large to encode");
        }

        appendUint32(buffer, static_cast<uint32_t>(bytes.size()));
        if (!bytes.empty())
        {
            buffer.append(bytes.data(), bytes.size());
        }
    }

    static void appendPath(std::string& buffer, const MPTPath& path)
    {
        if (path.empty())
        {
            appendBytes(buffer, {});
            return;
        }

        appendBytes(buffer, std::string_view(reinterpret_cast<const char*>(path.data()), path.size()));
    }

    static void appendHash(std::string& buffer, const MPTNodeHash& hash)
    {
        buffer.append(reinterpret_cast<const char*>(hash.data()), hash.size());
    }

    static void appendOptionalHash(std::string& buffer, const std::optional<MPTNodeHash>& hash)
    {
        appendByte(buffer, hash ? 1 : 0);
        if (hash)
        {
            appendHash(buffer, *hash);
        }
    }

    static void appendOptionalEntry(
        std::string& buffer, const std::optional<storage::Entry>& maybeValue)
    {
        appendByte(buffer, maybeValue ? 1 : 0);
        if (maybeValue)
        {
            appendBytes(buffer, maybeValue->get());
        }
    }

    static size_t optionalEntryEncodedSize(const std::optional<storage::Entry>& maybeValue)
    {
        size_t size = 1;
        if (maybeValue)
        {
            size += sizeof(uint32_t) + maybeValue->size();
        }
        return size;
    }

    static size_t optionalHashEncodedSize(const std::optional<MPTNodeHash>& hash)
    {
        return 1 + (hash ? MPTNodeHash::SIZE : 0);
    }

    static size_t accountValueEncodedSize(const MPTAccountValue& value)
    {
        size_t size = 0;
        size += optionalEntryEncodedSize(value.codeHash);
        size += optionalEntryEncodedSize(value.code);
        size += optionalEntryEncodedSize(value.balance);
        size += optionalEntryEncodedSize(value.abi);
        size += optionalEntryEncodedSize(value.nonce);
        size += optionalEntryEncodedSize(value.alive);
        size += optionalEntryEncodedSize(value.frozen);
        size += optionalEntryEncodedSize(value.shard);
        size += optionalHashEncodedSize(value.storageRoot);
        return size;
    }

    static void appendAccountValue(std::string& buffer, const MPTAccountValue& value)
    {
        appendOptionalEntry(buffer, value.codeHash);
        appendOptionalEntry(buffer, value.code);
        appendOptionalEntry(buffer, value.balance);
        appendOptionalEntry(buffer, value.abi);
        appendOptionalEntry(buffer, value.nonce);
        appendOptionalEntry(buffer, value.alive);
        appendOptionalEntry(buffer, value.frozen);
        appendOptionalEntry(buffer, value.shard);
        appendOptionalHash(buffer, value.storageRoot);
    }
};

// MPT skeleton is kept in the same header as MerkleStorage for now,
// so the account-state interception layer and trie abstraction evolve together.
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
        std::string_view remainingKey(key);
        auto nowNode = co_await getNode(rootHash);
        while (nowNode)
        {
            if (nowNode->type == MPTNodeType::leaf)
            {
                if (!pathEquals(remainingKey, nowNode->path))
                {
                    co_return std::nullopt;
                }
                co_return nowNode;
            }

            if (nowNode->type == MPTNodeType::branch)
            {
                if (remainingKey.empty())
                {
                    co_return std::nullopt;
                }

                auto firstKeyChar = remainingKey.front();
                auto childIndex = hexCharToNibble(firstKeyChar);

                if (auto const& childHash = nowNode->children[static_cast<size_t>(childIndex)];
                    childHash)
                {
                    nowNode = co_await getNode(*childHash);
                    remainingKey.remove_prefix(1);
                    continue;
                }
                co_return std::nullopt;
            }

            if (nowNode->type == MPTNodeType::extension)
            {
                if (!pathStartsWith(remainingKey, nowNode->path))
                {
                    co_return std::nullopt;
                }

                if (!nowNode->childHash)
                {
                    co_return std::nullopt;
                }

                remainingKey.remove_prefix(nowNode->path.size());
                nowNode = co_await getNode(*nowNode->childHash);
                continue;
            }

            throw std::runtime_error("Invalid MPT node type");
        }

        co_return std::nullopt;
    }

    task::Task<std::optional<MPTNodeHash>> write(
        MPTNodeHash rootHash, std::string key, std::string fieldKey, storage::Entry value)
    {
        co_return co_await writeTrieNode(rootHash, std::move(key),
            [fieldKey = std::move(fieldKey), value = std::move(value)](MPTNode& node) -> bool {
                return setAccountField(node.value, fieldKey, value);
            });
    }

    task::Task<std::optional<MPTNodeHash>> clear(
        MPTNodeHash rootHash, std::string key, std::string fieldKey)
    {
        co_return co_await writeTrieNode(rootHash, std::move(key),
            [fieldKey = std::move(fieldKey)](MPTNode& node) -> bool {
                return clearAccountField(node.value, fieldKey);
            });
    }

    task::Task<std::optional<MPTNodeHash>> writeSlot(
        MPTNodeHash slotRootHash, std::string slotKey, storage::Entry slotValue)
    {
        co_return co_await writeTrieNode(slotRootHash, std::move(slotKey),
            [slotValue = std::move(slotValue)](MPTNode& node) mutable -> bool {
                node.slotValue = std::move(slotValue);
                return true;
            });
    }

    task::Task<std::optional<MPTNodeHash>> clearSlot(
        MPTNodeHash slotRootHash, std::string slotKey)
    {
        co_return co_await writeTrieNode(slotRootHash, std::move(slotKey), [](MPTNode& node) {
            if (!node.slotValue)
            {
                return false;
            }
            node.slotValue.reset();
            return true;
        });
    }

    task::Task<std::optional<MPTNodeHash>> writeStorageRoot(
        MPTNodeHash rootHash, std::string key, MPTNodeHash storageRoot)
    {
        co_return co_await writeTrieNode(rootHash, std::move(key),
            [storageRoot = std::move(storageRoot)](MPTNode& node) mutable -> bool {
                node.value.storageRoot = std::move(storageRoot);
                return true;
            });
    }

    task::Task<void> remove(std::string_view key)
    {
        // TODO: Delete the leaf mapped by `key`.
        // TODO: Compact branch / extension nodes after deletion.
        // TODO: Refresh `m_root` and persist modified nodes.
        (void)key;
        co_return;
    }

    task::Task<std::optional<MPTNode>> getNode(const MPTNodeHash& nodeHash)
    {
        static_assert(std::same_as<std::remove_cvref_t<typename NodeStorage::Key>, MPTNodeHash>,
            "MPT node storage key type must be MPTNodeHash");
        static_assert(
            std::same_as<std::remove_cvref_t<typename NodeStorage::Value>, storage::Entry>,
            "MPT node storage value type must be storage::Entry");

        if (auto encodedNode = co_await ::bcos::storage2::readOne(m_nodeStorage.get(), nodeHash))
        {
            co_return MPTNode::restore(*encodedNode);
        }
        co_return std::nullopt;
    }

    MPTNodeHash calcNodeHash(const MPTNode& node) const
    {
        if (!m_hashImpl)
        {
            throw std::runtime_error("Hash implementation is not configured for MPT");
        }

        return node.hash(*m_hashImpl);
    }

    task::Task<MPTNodeHash> putNode(const MPTNode& node)
    {
        static_assert(std::same_as<std::remove_cvref_t<typename NodeStorage::Key>, MPTNodeHash>,
            "MPT node storage key type must be MPTNodeHash");
        static_assert(
            std::same_as<std::remove_cvref_t<typename NodeStorage::Value>, storage::Entry>,
            "MPT node storage value type must be storage::Entry");

        auto nodeHash = calcNodeHash(node);
        auto encodedNode = node.flatten();

        co_await ::bcos::storage2::writeOne(m_nodeStorage.get(), nodeHash, std::move(encodedNode));
        co_return nodeHash;
    }

    task::Task<void> setRoot(MPTNodeHash root)
    {
        m_root = std::move(root);
        // TODO: Persist root hash if the caller wants a durable root record.
        co_return;
    }

    task::Task<std::optional<MPTNodeHash>> root() const
    {
        co_return m_root;
    }

    static bool isSlotKey(std::string_view key) noexcept
    {
        constexpr static size_t slotKeyLength = 32;
        return key.size() == slotKeyLength;
    }

private:
    template <class UpdateFn>
    task::Task<std::optional<MPTNodeHash>> writeTrieNode(
        MPTNodeHash rootHash, std::string key, UpdateFn&& updateFn)
    {
        auto nowNode = co_await getNode(rootHash);
        if (!nowNode)
        {
            nowNode.emplace();
            nowNode->type = MPTNodeType::leaf;
            nowNode->path = toMPTPath(key);
            if (!std::invoke(updateFn, *nowNode))
            {
                co_return std::nullopt;
            }
            co_return co_await putNode(*nowNode);
        }

        if (nowNode->type == MPTNodeType::leaf)
        {
            if (!pathEquals(key, nowNode->path))
            {
                auto prefixLength = commonPrefixLength(key, nowNode->path);
                auto keyPath = toMPTPath(key);

                auto oldLeafNode = *nowNode;
                oldLeafNode.path = MPTPath(
                    nowNode->path.begin() + static_cast<std::ptrdiff_t>(prefixLength + 1),
                    nowNode->path.end());

                MPTNode newLeafNode;
                newLeafNode.type = MPTNodeType::leaf;
                newLeafNode.path = MPTPath(
                    keyPath.begin() + static_cast<std::ptrdiff_t>(prefixLength + 1),
                    keyPath.end());
                if (!std::invoke(updateFn, newLeafNode))
                {
                    co_return std::nullopt;
                }

                auto oldLeafHash = co_await putNode(oldLeafNode);
                auto newLeafHash = co_await putNode(newLeafNode);

                MPTNode branchNode;
                branchNode.type = MPTNodeType::branch;
                auto oldBranchNibble = nowNode->path[prefixLength];
                auto newBranchNibble = keyPath[prefixLength];
                branchNode.children[oldBranchNibble] = oldLeafHash;
                branchNode.children[newBranchNibble] = newLeafHash;

                auto branchHash = co_await putNode(branchNode);
                if (prefixLength == 0)
                {
                    co_return branchHash;
                }

                MPTNode extensionNode;
                extensionNode.type = MPTNodeType::extension;
                extensionNode.path = MPTPath(
                    nowNode->path.begin(),
                    nowNode->path.begin() + static_cast<std::ptrdiff_t>(prefixLength));
                extensionNode.childHash = branchHash;
                co_return co_await putNode(extensionNode);
            }

            if (!std::invoke(updateFn, *nowNode))
            {
                co_return std::nullopt;
            }
            co_return co_await putNode(*nowNode);
        }
        else if (nowNode->type == MPTNodeType::branch)
        {
            if (key.empty())
            {
                throw std::runtime_error("Trie write reached branch node with empty key");
            }

            auto childIndex = static_cast<size_t>(hexCharToNibble(key.front()));
            auto remainingKey = key.substr(1);

            if (auto& childHash = nowNode->children[childIndex]; childHash)
            {
                if (auto updatedChildHash = co_await writeTrieNode(
                        *childHash, std::string(remainingKey), std::ref(updateFn)))
                {
                    childHash = *updatedChildHash;
                    co_return co_await putNode(*nowNode);
                }
                co_return std::nullopt;
            }

            MPTNode newLeafNode;
            newLeafNode.type = MPTNodeType::leaf;
            newLeafNode.path = toMPTPath(remainingKey);
            if (!std::invoke(updateFn, newLeafNode))
            {
                co_return std::nullopt;
            }

            auto newLeafHash = co_await putNode(newLeafNode);
            nowNode->children[childIndex] = newLeafHash;
            co_return co_await putNode(*nowNode);
        }
        else if (nowNode->type == MPTNodeType::extension)
        {
            if (!pathStartsWith(key, nowNode->path))
            {
                auto prefixLength = commonPrefixLength(key, nowNode->path);
                auto keyPath = toMPTPath(key);

                if (!nowNode->childHash)
                {
                    throw std::runtime_error("Extension node is missing child hash");
                }

                std::optional<MPTNodeHash> oldChildForBranch;
                MPTNibble oldBranchNibble = 0;
                MPTNibble newBranchNibble = 0;

                if (prefixLength == 0)
                {
                    oldBranchNibble = nowNode->path.front();
                    newBranchNibble = keyPath.front();

                    if (nowNode->path.size() == 1)
                    {
                        oldChildForBranch = *nowNode->childHash;
                    }
                    else
                    {
                        MPTNode oldExtensionNode;
                        oldExtensionNode.type = MPTNodeType::extension;
                        oldExtensionNode.path =
                            MPTPath(nowNode->path.begin() + 1, nowNode->path.end());
                        oldExtensionNode.childHash = *nowNode->childHash;
                        oldChildForBranch = co_await putNode(oldExtensionNode);
                    }
                }
                else
                {
                    oldBranchNibble = nowNode->path[prefixLength];
                    newBranchNibble = keyPath[prefixLength];

                    auto oldSuffixBegin =
                        nowNode->path.begin() + static_cast<std::ptrdiff_t>(prefixLength + 1);
                    if (oldSuffixBegin == nowNode->path.end())
                    {
                        oldChildForBranch = *nowNode->childHash;
                    }
                    else
                    {
                        MPTNode oldExtensionNode;
                        oldExtensionNode.type = MPTNodeType::extension;
                        oldExtensionNode.path = MPTPath(oldSuffixBegin, nowNode->path.end());
                        oldExtensionNode.childHash = *nowNode->childHash;
                        oldChildForBranch = co_await putNode(oldExtensionNode);
                    }
                }

                MPTNode newLeafNode;
                newLeafNode.type = MPTNodeType::leaf;
                newLeafNode.path = MPTPath(
                    keyPath.begin() + static_cast<std::ptrdiff_t>(prefixLength + 1),
                    keyPath.end());
                if (!std::invoke(updateFn, newLeafNode))
                {
                    co_return std::nullopt;
                }
                auto newLeafHash = co_await putNode(newLeafNode);

                MPTNode branchNode;
                branchNode.type = MPTNodeType::branch;
                branchNode.children[oldBranchNibble] = *oldChildForBranch;
                branchNode.children[newBranchNibble] = newLeafHash;
                auto branchHash = co_await putNode(branchNode);

                if (prefixLength == 0)
                {
                    co_return branchHash;
                }

                nowNode->path = MPTPath(
                    nowNode->path.begin(),
                    nowNode->path.begin() + static_cast<std::ptrdiff_t>(prefixLength));
                nowNode->childHash = branchHash;
                co_return co_await putNode(*nowNode);
            }

            if (!nowNode->childHash)
            {
                throw std::runtime_error("Extension node is missing child hash");
            }

            auto remainingKey = key.substr(nowNode->path.size());
            if (auto updatedChildHash = co_await writeTrieNode(
                    *nowNode->childHash, std::string(remainingKey), std::ref(updateFn)))
            {
                nowNode->childHash = *updatedChildHash;
                co_return co_await putNode(*nowNode);
            }
            co_return std::nullopt;
        }

        throw std::runtime_error("Trie write resolved to unsupported MPT node type");
    }

    static auto toMPTPath(std::string_view hexKey) -> MPTPath
    {
        MPTPath path;
        path.reserve(hexKey.size());
        for (char c : hexKey)
        {
            path.push_back(static_cast<MPTNibble>(hexCharToNibble(c)));
        }
        return path;
    }

    static bool pathStartsWith(std::string_view key, const MPTPath& path) noexcept
    {
        if (key.size() < path.size())
        {
            return false;
        }

        for (size_t index = 0; index < path.size(); ++index)
        {
            if (static_cast<uint8_t>(hexCharToNibble(key[index])) != path[index])
            {
                return false;
            }
        }
        return true;
    }

    static bool pathEquals(std::string_view key, const MPTPath& path) noexcept
    {
        return key.size() == path.size() && pathStartsWith(key, path);
    }

    static size_t commonPrefixLength(std::string_view key, const MPTPath& path) noexcept
    {
        auto prefixLength = std::min(key.size(), path.size());
        size_t index = 0;
        for (; index < prefixLength; ++index)
        {
            if (static_cast<uint8_t>(hexCharToNibble(key[index])) != path[index])
            {
                break;
            }
        }
        return index;
    }

    static int hexCharToNibble(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F')
        {
            return 10 + (c - 'A');
        }
        throw std::runtime_error("Invalid hex character in MPT path");
    }

    static bool setAccountField(
        MPTAccountValue& accountValue, std::string_view field, const storage::Entry& value)
    {
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH)
        {
            accountValue.codeHash = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE)
        {
            accountValue.code = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::BALANCE)
        {
            accountValue.balance = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ABI)
        {
            accountValue.abi = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::NONCE)
        {
            accountValue.nonce = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ALIVE)
        {
            accountValue.alive = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::FROZEN)
        {
            accountValue.frozen = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::SHARD)
        {
            accountValue.shard = value;
            return true;
        }
        return false;
    }

    static bool clearAccountField(MPTAccountValue& accountValue, std::string_view field)
    {
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH)
        {
            if (!accountValue.codeHash)
            {
                return false;
            }
            accountValue.codeHash.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE)
        {
            if (!accountValue.code)
            {
                return false;
            }
            accountValue.code.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::BALANCE)
        {
            if (!accountValue.balance)
            {
                return false;
            }
            accountValue.balance.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ABI)
        {
            if (!accountValue.abi)
            {
                return false;
            }
            accountValue.abi.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::NONCE)
        {
            if (!accountValue.nonce)
            {
                return false;
            }
            accountValue.nonce.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ALIVE)
        {
            if (!accountValue.alive)
            {
                return false;
            }
            accountValue.alive.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::FROZEN)
        {
            if (!accountValue.frozen)
            {
                return false;
            }
            accountValue.frozen.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::SHARD)
        {
            if (!accountValue.shard)
            {
                return false;
            }
            accountValue.shard.reset();
            return true;
        }
        return false;
    }

    std::reference_wrapper<NodeStorage> m_nodeStorage;
    crypto::Hash::Ptr m_hashImpl;
    std::optional<MPTNodeHash> m_root;
};

// A thin wrapper over an existing storage backend.
//
// New boundary after the architecture adjustment:
// - request routing / filtering happens before entering MerkleStorage
// - any request arriving here is assumed to be CA/EOA-related account-state access
//
// Current stage:
// - keep the existing backend behavior unchanged
// - reserve the read/write/remove interception points for future MPT integration
//
// Future stage:
// - translate logical state keys (`table:key`) into trie lookup/update paths
// - read/write account state through MPT instead of directly touching flat KV entries
// - keep trie nodes in a dedicated node store instead of mixing them with state entries
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
        co_return co_await ::bcos::storage2::readOne(m_backendStorage.get(), key);
    }

    auto readOne(const auto& key, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<Value>>
    {
        if (auto stateKey = toStateKeyView(key); stateKey && m_mpt)
        {
            auto hexStateKey = toHexStateKey(*stateKey);
            auto& table = hexStateKey.table;
            auto& tableKey = hexStateKey.key;

            if (auto mptNode = co_await m_mpt->read(stateRoot, table))
            {
                if (isSlotKey(stateKey->m_key))
                {
                    if (auto slotRoot = mptNode->value.storageRoot)
                    {
                        if (auto slotNode = co_await m_mpt->read(*slotRoot, tableKey))
                        {
                            if (slotNode->slotValue)
                            {
                                co_return slotNode->slotValue;
                            }
                        }
                    }
                }
                else if (auto accountFieldValue = getAccountField(mptNode->value, stateKey->m_key))
                {
                    co_return accountFieldValue;
                }
            }
        }

        co_return std::nullopt;
    }

    auto existsOne(const auto& key) -> task::Task<bool>
    {
        co_return (co_await readOne(key)).has_value();
    }

    auto readSome(::ranges::input_range auto keys) -> task::Task<std::vector<std::optional<Value>>>
    {
        std::vector<std::optional<Value>> values;
        if constexpr (::ranges::sized_range<decltype(keys)>)
        {
            values.reserve(::ranges::size(keys));
        }

        for (auto&& key : keys)
        {
            values.emplace_back(co_await readOne(key));
        }

        co_return values;
    }

    auto writeOne(auto key, auto value) -> task::Task<void>
    {
        co_await ::bcos::storage2::writeOne(
            m_backendStorage.get(), std::move(key), std::move(value));
        co_return;
    }

    auto writeOne(auto key, auto value, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        std::optional<MPTNodeHash> updatedRoot;

        if (auto stateKey = toStateKeyView(key); stateKey && m_mpt)
        {
            auto hexStateKey = toHexStateKey(*stateKey);
            auto& table = hexStateKey.table;
            auto& tableKey = hexStateKey.key;

            if constexpr (std::same_as<Value, storage::Entry> &&
                          std::same_as<std::remove_cvref_t<decltype(value)>, storage::Entry>)
            {
                if (isSlotKey(stateKey->m_key))
                {
                    if (auto accountNode = co_await m_mpt->read(stateRoot, table))
                    {
                        if (auto slotRoot = accountNode->value.storageRoot)
                        {
                            if (auto updatedSlotRoot =
                                    co_await m_mpt->writeSlot(*slotRoot, tableKey, value))
                            {
                                updatedRoot =
                                    co_await m_mpt->writeStorageRoot(stateRoot, table, *updatedSlotRoot);
                            }
                        }
                    }
                }
                else
                {
                    updatedRoot = co_await m_mpt->write(
                        stateRoot, table, std::string(stateKey->m_key), value);
                }
            }
        }

        co_await ::bcos::storage2::writeOne(
            m_backendStorage.get(), std::move(key), std::move(value));
        co_return updatedRoot;
    }

    auto writeSome(::ranges::input_range auto keyValues) -> task::Task<void>
    {
        for (auto&& [key, value] : keyValues)
        {
            co_await writeOne(std::forward<decltype(key)>(key), std::forward<decltype(value)>(value));
        }
        co_return;
    }

    auto writeSome(::ranges::input_range auto keyValues, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        std::optional<MPTNodeHash> currentRoot = stateRoot;
        for (auto&& [key, value] : keyValues)
        {
            auto updatedRoot =
                co_await writeOne(std::forward<decltype(key)>(key),
                    std::forward<decltype(value)>(value), *currentRoot);
            if (!updatedRoot)
            {
                co_return std::nullopt;
            }
            currentRoot = std::move(updatedRoot);
        }
        co_return currentRoot;
    }

    auto removeOne(auto key) -> task::Task<void>
    {
        co_await removeOneImpl(std::move(key), false);
    }

    auto removeOne(auto key, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        co_await removeOneImpl(std::move(key), true);
    }

    auto removeOne(auto key, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return co_await removeOneWithStateRoot(std::move(key), stateRoot, false);
    }

    auto removeOne(auto key, const MPTNodeHash& stateRoot, DIRECT_TYPE /*unused*/)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        co_return co_await removeOneWithStateRoot(std::move(key), stateRoot, true);
    }

    auto removeSome(::ranges::input_range auto keys) -> task::Task<void>
    {
        for (auto&& key : keys)
        {
            co_await removeOne(std::forward<decltype(key)>(key));
        }
        co_return;
    }

    auto removeSome(::ranges::input_range auto keys, DIRECT_TYPE /*unused*/) -> task::Task<void>
    {
        for (auto&& key : keys)
        {
            co_await removeOne(std::forward<decltype(key)>(key), DIRECT);
        }
        co_return;
    }

    auto removeSome(::ranges::input_range auto keys, const MPTNodeHash& stateRoot)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        std::optional<MPTNodeHash> currentRoot = stateRoot;
        for (auto&& key : keys)
        {
            auto updatedRoot =
                co_await removeOne(std::forward<decltype(key)>(key), *currentRoot);
            if (!updatedRoot)
            {
                co_return std::nullopt;
            }
            currentRoot = std::move(updatedRoot);
        }
        co_return currentRoot;
    }

    auto removeSome(
        ::ranges::input_range auto keys, const MPTNodeHash& stateRoot, DIRECT_TYPE /*unused*/)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        std::optional<MPTNodeHash> currentRoot = stateRoot;
        for (auto&& key : keys)
        {
            auto updatedRoot =
                co_await removeOne(std::forward<decltype(key)>(key), *currentRoot, DIRECT);
            if (!updatedRoot)
            {
                co_return std::nullopt;
            }
            currentRoot = std::move(updatedRoot);
        }
        co_return currentRoot;
    }

private:
    task::Task<void> removeOneImpl(auto key, bool direct)
    {
        if (auto stateKey = toStateKeyView(key); stateKey && m_mpt)
        {
            if (auto currentRoot = co_await m_mpt->root(); currentRoot)
            {
                auto updatedRoot = co_await removeOneWithStateRoot(key, *currentRoot, direct);
                if (updatedRoot)
                {
                    co_await m_mpt->setRoot(*updatedRoot);
                }
                co_return;
            }
        }

        if (direct)
        {
            co_await ::bcos::storage2::removeOne(m_backendStorage.get(), std::move(key), DIRECT);
        }
        else
        {
            co_await ::bcos::storage2::removeOne(m_backendStorage.get(), std::move(key));
        }
        co_return;
    }

    auto removeOneWithStateRoot(auto key, const MPTNodeHash& stateRoot, bool direct)
        -> task::Task<std::optional<MPTNodeHash>>
    {
        std::optional<MPTNodeHash> updatedRoot;

        if (auto stateKey = toStateKeyView(key); stateKey && m_mpt)
        {
            auto hexStateKey = toHexStateKey(*stateKey);
            auto& table = hexStateKey.table;
            auto& tableKey = hexStateKey.key;

            if (isSlotKey(stateKey->m_key))
            {
                if (auto accountNode = co_await m_mpt->read(stateRoot, table))
                {
                    if (auto slotRoot = accountNode->value.storageRoot)
                    {
                        if (auto updatedSlotRoot = co_await m_mpt->clearSlot(*slotRoot, tableKey))
                        {
                            updatedRoot =
                                co_await m_mpt->writeStorageRoot(stateRoot, table, *updatedSlotRoot);
                        }
                    }
                }
            }
            else
            {
                updatedRoot =
                    co_await m_mpt->clear(stateRoot, table, std::string(stateKey->m_key));
            }
        }

        if (direct)
        {
            co_await ::bcos::storage2::removeOne(m_backendStorage.get(), std::move(key), DIRECT);
        }
        else
        {
            co_await ::bcos::storage2::removeOne(m_backendStorage.get(), std::move(key));
        }
        co_return updatedRoot;
    }

    std::reference_wrapper<Backend> m_backendStorage;
    std::optional<std::reference_wrapper<NodeStorage>> m_trieNodeStorage;
    std::optional<Trie> m_mpt;

    auto readBackendField(std::string_view table, std::string_view field)
        -> task::Task<std::optional<storage::Entry>>
    {
        co_return co_await ::bcos::storage2::readOne(
            m_backendStorage.get(), executor_v1::StateKeyView{table, field});
    }

    auto readAccountValueFromBackend(std::string_view table) -> task::Task<MPTAccountValue>
    {
        MPTAccountValue accountValue;
        accountValue.codeHash =
            co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH);
        accountValue.code = co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::CODE);
        accountValue.balance =
            co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        accountValue.abi = co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::ABI);
        accountValue.nonce = co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::NONCE);
        accountValue.alive = co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::ALIVE);
        accountValue.frozen =
            co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::FROZEN);
        accountValue.shard = co_await readBackendField(table, ledger::ACCOUNT_TABLE_FIELDS::SHARD);
        co_return accountValue;
    }

    static auto getAccountField(const MPTAccountValue& accountValue, std::string_view field)
        -> std::optional<storage::Entry>
    {
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH)
        {
            return accountValue.codeHash;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE)
        {
            return accountValue.code;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::BALANCE)
        {
            return accountValue.balance;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ABI)
        {
            return accountValue.abi;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::NONCE)
        {
            return accountValue.nonce;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ALIVE)
        {
            return accountValue.alive;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::FROZEN)
        {
            return accountValue.frozen;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::SHARD)
        {
            return accountValue.shard;
        }
        return std::nullopt;
    }

    static bool setAccountField(
        MPTAccountValue& accountValue, std::string_view field, const storage::Entry& value)
    {
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH)
        {
            accountValue.codeHash = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE)
        {
            accountValue.code = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::BALANCE)
        {
            accountValue.balance = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ABI)
        {
            accountValue.abi = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::NONCE)
        {
            accountValue.nonce = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ALIVE)
        {
            accountValue.alive = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::FROZEN)
        {
            accountValue.frozen = value;
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::SHARD)
        {
            accountValue.shard = value;
            return true;
        }
        return false;
    }

    static bool clearAccountField(MPTAccountValue& accountValue, std::string_view field)
    {
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH)
        {
            if (!accountValue.codeHash)
            {
                return false;
            }
            accountValue.codeHash.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::CODE)
        {
            if (!accountValue.code)
            {
                return false;
            }
            accountValue.code.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::BALANCE)
        {
            if (!accountValue.balance)
            {
                return false;
            }
            accountValue.balance.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ABI)
        {
            if (!accountValue.abi)
            {
                return false;
            }
            accountValue.abi.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::NONCE)
        {
            if (!accountValue.nonce)
            {
                return false;
            }
            accountValue.nonce.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::ALIVE)
        {
            if (!accountValue.alive)
            {
                return false;
            }
            accountValue.alive.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::FROZEN)
        {
            if (!accountValue.frozen)
            {
                return false;
            }
            accountValue.frozen.reset();
            return true;
        }
        if (field == ledger::ACCOUNT_TABLE_FIELDS::SHARD)
        {
            if (!accountValue.shard)
            {
                return false;
            }
            accountValue.shard.reset();
            return true;
        }
        return false;
    }

    static auto toStateKeyView(const auto& key) -> std::optional<executor_v1::StateKeyView>
    {
        using InputKey = std::remove_cvref_t<decltype(key)>;
        if constexpr (std::same_as<InputKey, executor_v1::StateKey>)
        {
            return executor_v1::StateKeyView(key);
        }
        else if constexpr (std::same_as<InputKey, executor_v1::StateKeyView>)
        {
            return key;
        }
        else
        {
            return std::nullopt;
        }
    }

    static auto toHexPathKey(std::string_view keyPart) -> std::string
    {
        return bcos::toHex(keyPart);
    }

    static bool isSlotKey(std::string_view key) noexcept { return Trie::isSlotKey(key); }

    struct HexStateKey
    {
        std::string table;
        std::string key;
    };

    static auto toHexStateKey(const executor_v1::StateKeyView& key) -> HexStateKey
    {
        return HexStateKey{toHexPathKey(key.m_table), toHexPathKey(key.m_key)};
    }

    static bool pathStartsWith(std::string_view key, const MPTPath& path) noexcept
    {
        if (key.size() < path.size())
        {
            return false;
        }

        for (size_t index = 0; index < path.size(); ++index)
        {
            if (static_cast<uint8_t>(hexCharToNibble(key[index])) != path[index])
            {
                return false;
            }
        }
        return true;
    }

    static bool pathEquals(std::string_view key, const MPTPath& path) noexcept
    {
        return key.size() == path.size() && pathStartsWith(key, path);
    }

    static int hexCharToNibble(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F')
        {
            return 10 + (c - 'A');
        }
        throw std::runtime_error("Invalid hex character in MPT path");
    }
};

}  // namespace bcos::storage2
