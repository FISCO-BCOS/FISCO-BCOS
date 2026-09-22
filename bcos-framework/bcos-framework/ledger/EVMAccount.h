#pragma once
#include <algorithm>
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/AccountTableName.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <range/v3/algorithm/copy.hpp>

namespace bcos::ledger::account
{

DERIVE_BCOS_EXCEPTION(NonceNotInitialized);

/// Tag for the table-name constructor below. A distinct type so it can never be
/// confused with the AddressTableMode the address-taking constructors carry.
struct FromTableName
{
    explicit FromTableName() = default;
};

// AddressTableMode (the node-local account-table encoding) and the node-wide
// nodeAddressTableMode() singleton live in ledger/AccountTableName.h; callers pass
// nodeAddressTableMode() to the mode-taking constructors below.

/// Exactly 40 lowercase hex chars — the canonical address form (Address::hex(),
/// boost::algorithm::hex_lower output). Uppercase is NOT accepted: it is a different
/// string, not another encoding of the same address.
constexpr bool isLowerHexAddress(std::string_view address) noexcept
{
    return address.size() == HEX_ADDRESS_SIZE &&
           std::all_of(address.begin(), address.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

namespace detail
{
/// Compile-time nibble decode; caller guarantees lowercase hex (isLowerHexAddress).
consteval char lowerHexNibble(char c)
{
    return static_cast<char>(c <= '9' ? c - '0' : c - 'a' + 10);
}

/// Compile-time unhex of one canonical 40-char lowercase-hex address.
/// boost::algorithm::unhex is not constexpr, hence the hand-rolled decoder.
consteval std::array<char, ADDRESS_SIZE> unhexLowerHexAddress(std::string_view hex)
{
    std::array<char, ADDRESS_SIZE> result{};
    for (size_t i = 0; i < ADDRESS_SIZE; ++i)
    {
        result[i] = static_cast<char>(lowerHexNibble(hex[i * 2]) << 4 | lowerHexNibble(hex[i * 2 + 1]));
    }
    return result;
}

/// The c_systemTxsAddress members that are hex addresses, decoded to raw bytes at
/// COMPILE TIME. Derived from c_systemTxsAddress itself — never maintained as a
/// separate list: isLowerHexAddress filters out the name members ("/sys/...", which
/// can never equal a hex_lower output anyway), and decoding preserves lexicographic
/// order (lowercase hex chars sort like their nibble values), so the sorted input
/// decodes to a sorted array and binary_search is valid. The evmc_address overload
/// below binary-searches this set on the raw 20 bytes, so the Binary-mode
/// non-system path does no hex work at all.
constexpr auto c_systemTxsBinaryAddress = [] {
    constexpr size_t count = [] {
        size_t n = 0;
        for (std::string_view address : precompiled::c_systemTxsAddress)
        {
            if (isLowerHexAddress(address))
            {
                ++n;
            }
        }
        return n;
    }();
    std::array<std::array<char, ADDRESS_SIZE>, count> result{};
    size_t i = 0;
    for (std::string_view address : precompiled::c_systemTxsAddress)
    {
        if (isLowerHexAddress(address))
        {
            result[i++] = unhexLowerHexAddress(address);
        }
    }
    return result;
}();
// The 8 hex-address members; the 3 name members are filtered out.
static_assert(c_systemTxsBinaryAddress.size() == 8);
// Sortedness must hold under the SAME ordering the runtime binary_search uses:
// string_view comparison (char_traits::compare, unsigned/memcmp order). A plain
// is_sorted on the char arrays would compare signed chars, which orders bytes >= 0x80
// differently — fine for today's 8 low-byte addresses, silently wrong if a high-byte
// system address is ever added.
static_assert(std::ranges::is_sorted(c_systemTxsBinaryAddress, std::ranges::less{},
    [](const auto& entry) { return std::string_view{entry.data(), entry.size()}; }));
// Spot-check the decoder: 0x...1000 has byte[18] == 0x10.
static_assert(
    unhexLowerHexAddress(precompiled::SYS_CONFIG_ADDRESS)[ADDRESS_SIZE - 2] == '\x10');
}  // namespace detail

/// THE one address → account-table-name routing rule (the encoding contract itself is
/// documented in AccountTableName.h): the 8 c_systemTxsAddress members always route to
/// "/sys/<hex>", every other address routes to "/apps/<hex>" in Hex mode and to
/// "/s/<20 raw bytes>" in any other mode. The EVMAccount constructors below and every
/// caller that needs the table name without an account object (Ledger's state reads, the
/// web3 RPC endpoints, the v1 precompiled call sites) share this single derivation — never
/// re-derive the name locally.
///
/// The function is TOTAL: the Binary branch is taken only for a canonical 40-char
/// lowercase-hex address; every other input — uppercase, non-hex, odd or short length,
/// user garbage from precompiled call params (ShardingPrecompiled) or RPC/P2P ingress —
/// falls through to the verbatim "/apps/<input>" form, exactly what the Hex branch
/// returns. Both encodings therefore map every input to the SAME logical (typically
/// empty) table: a malformed input reads an empty table on both sides of a
/// mixed-encoding network instead of throwing on the Binary side (different receipts,
/// different receipt root — a fork) or tripping an assert.
///
/// The evmc_address overload below mirrors this same rule on raw bytes: its system-tx
/// membership test runs against detail::c_systemTxsBinaryAddress, decoded from
/// c_systemTxsAddress at compile time — one rule, two representations of the system
/// set, the binary one derived by the compiler so the two can never drift apart.
/// @param address the address as a hex string (no 0x prefix)
/// @param mode this node's account-table encoding (see AddressTableMode)
inline std::string accountTableName(std::string_view address, AddressTableMode mode)
{
    std::string tableName;
    if (precompiled::contains(bcos::precompiled::c_systemTxsAddress, address))
    {
        // System-tx addresses always route to /sys/ with the hex name; those tables
        // are not part of the account-table migration regardless of mode.
        tableName.reserve(ledger::SYS_DIRECTORY::SYS_APPS.size() + address.size());
        tableName.append(ledger::SYS_DIRECTORY::SYS_APPS);
        tableName.append(address);
        return tableName;
    }
    if (mode != AddressTableMode::Hex && isLowerHexAddress(address))
    {
        tableName.reserve(BINARY_TABLE_PREFIX.size() + ADDRESS_SIZE);
        tableName.append(BINARY_TABLE_PREFIX);
        boost::algorithm::unhex(address.begin(), address.end(), std::back_inserter(tableName));
        return tableName;
    }
    tableName.reserve(ledger::SYS_DIRECTORY::USER_APPS.size() + address.size());
    tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
    tableName.append(address);
    return tableName;
}

/// Raw-address overload. The Binary-mode non-system path — the overwhelming hot path
/// (every executor account access) — does NO hex work at all: the system-tx membership
/// test runs a string_view over the raw 20 bytes against detail::c_systemTxsBinaryAddress
/// (no copy of the address), and the "/s/" name is just prefix + raw bytes. Only a
/// system-tx hit (which needs "/sys/<hex>") or Hex mode pays for hex_lower and routes
/// through the string_view overload above. Equivalence with that overload holds because
/// its hex_lower output is canonical by construction (isLowerHexAddress always passes,
/// unhex is the exact inverse of the encode) and the name members of c_systemTxsAddress
/// can never equal a 40-char hex string.
inline std::string accountTableName(const evmc_address& address, AddressTableMode mode)
{
    const std::string_view rawAddress = concepts::bytebuffer::toView(address.bytes);
    if (mode != AddressTableMode::Hex &&
        !std::ranges::binary_search(detail::c_systemTxsBinaryAddress, rawAddress,
            std::ranges::less{}, [](const auto& entry) {
                return std::string_view{entry.data(), entry.size()};
            }))
    {
        std::string tableName;
        tableName.reserve(BINARY_TABLE_PREFIX.size() + ADDRESS_SIZE);
        tableName.append(BINARY_TABLE_PREFIX);
        tableName.append(rawAddress);
        return tableName;
    }
    std::array<char, sizeof(address.bytes) * 2> hexAddress;  // NOLINT
    boost::algorithm::hex_lower(rawAddress, hexAddress.data());
    return accountTableName(std::string_view(hexAddress.data(), hexAddress.size()), mode);
}

/// Convenience overloads for callers in the node process, where the mode is the
/// process-global published once at boot (AccountTableName.h).
inline std::string accountTableName(std::string_view address)
{
    return accountTableName(address, nodeAddressTableMode());
}

inline std::string accountTableName(const evmc_address& address)
{
    return accountTableName(address, nodeAddressTableMode());
}

/// Re-encode a table name derived through a Hex-era rule (the executive
/// getContractTableName, "/apps/" + address) into this node's physical layout. Binary
/// mode maps "/apps/<40 lowercase hex>" to "/s/<20 raw bytes>"; every other name —
/// "/sys/<hex>" from the executive rule's 35-leading-zero routing, non-canonical
/// inputs — passes through unchanged, the same treatment canonicalTableNameForHash
/// gives it. Hex mode returns the input byte-for-byte. Sites that historically derived
/// their name from such a rule must keep deriving it from that rule and re-encode ONLY
/// the physical layout through this helper, so Hex and Binary nodes write the same
/// logical row (deriving the Binary name through the shared rule instead splits e.g.
/// address(0) — "/sys/<hex>" by the executive rule — onto "/s/<zeros>", canonical
/// "/apps/<hex>": different logical rows, different XOR root, a mixed-mode fork).
inline std::string toNodeLayout(std::string hexRuleName)
{
    if (nodeAddressTableMode() == AddressTableMode::Binary)
    {
        if (auto bin = hexToBinaryAccountTableName(hexRuleName); !bin.empty())
        {
            return bin;
        }
    }
    return hexRuleName;
}

/// Table name for the v1-precompiled call sites that historically built their key as
/// getContractTableName("/apps/", address) = "/apps/" + <verbatim input> and therefore NEVER
/// routed system addresses to /sys/ (ShardingPrecompiled's shard rows, the AccountManager /
/// ContractAuthMgr / BFSPrecompiled access probes). Both layouts must resolve to the same
/// logical row: Hex keeps the base string byte-for-byte, and Binary only re-encodes it
/// physically (/apps/<hex> -> /s/<20 raw bytes>) — no /sys/ routing in either mode.
/// Routing Binary through the shared rule instead would send the 8 c_systemTxsAddress
/// members to /sys/ while Hex keeps them under /apps/, splitting the row across a
/// mixed-mode network — /sys/ names are not normalized by canonicalTableNameForHash.
/// Callers pass a plain 40-char lowercase hex address.
inline std::string legacyAppsAccountTableName(std::string_view address)
{
    std::string hexName(ledger::SYS_DIRECTORY::USER_APPS);
    hexName.append(address);
    return toNodeLayout(std::move(hexName));
}

template <class Storage>
class EVMAccount
{
    // All interface Need block version >= 3.1
private:
    std::reference_wrapper<Storage> m_storage;
    std::string m_tableName;

public:
    task::Task<bool> exists()
    {
        co_return co_await storage2::existsOne(
            m_storage.get(), executor_v1::StateKeyView(SYS_TABLES, m_tableName));
    }

    /// Ethereum-style existence (EIP-161 Spurious Dragon+): an account with nonce 0, balance 0
    /// and no code is empty and counts as non-existent. "No code" is either a missing CODE_HASH
    /// row (zero hash) or a row holding @p emptyCodeHash, the chain hasher's hash of "" (an
    /// EIP-7702 delegation cleared back to an EOA is stored that way).
    /// @param knownCodeHash the CODE_HASH row if the caller already read it, to avoid a second
    ///        read on the EXTCODEHASH path.
    task::Task<bool> existsEthereum(
        const h256& emptyCodeHash, std::optional<h256> knownCodeHash = std::nullopt)
    {
        auto ch = knownCodeHash ? *knownCodeHash : co_await codeHash();
        if (ch != h256{} && ch != emptyCodeHash)
            co_return true;  // has code: live regardless of nonce / balance

        if (!co_await exists())
            co_return false;

        auto nonceVal = co_await nonce();
        if (nonceVal.has_value() && u256(nonceVal.value()) != 0)
            co_return true;

        auto bal = co_await balance();
        if (bal != 0)
            co_return true;

        co_return false;  // empty account → not exist in Ethereum sense
    }

    task::Task<void> create()
    {
        co_await storage2::writeOne(m_storage.get(), executor_v1::StateKey(SYS_TABLES, m_tableName),
            storage::Entry{std::string_view{"value"}});
    }

    task::Task<std::optional<storage::Entry>> code()
    {
        // 先通过code hash从s_code_binary找代码
        // Start by using the code hash to find the code from the s_code_binary
        if (auto codeHashEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            if (auto codeEntry = co_await storage2::readOne(m_storage.get(),
                    executor_v1::StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry->get()}))
            {
                co_return codeEntry;
            }
        }

        // 在s_code_binary里没找到，可能是老版本部署的合约或internal
        // precompiled，代码在合约表的code字段里
        // Can't find it in the s_code_binary, it may be a contract deployed in the old version or
        // internal precompiled, and the code is in the code field of the contract table
        if (auto codeEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::CODE}))
        {
            co_return codeEntry;
        }
        co_return {};
    }

    task::Task<void> setCode(bytes code, std::string abi, const crypto::HashType& codeHash)
    {
        storage::Entry codeHashEntry(concepts::bytebuffer::toView(codeHash));
        if (!co_await storage2::existsOne(m_storage.get(),
                executor_v1::StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry.get()}))
        {
            co_await storage2::writeOne(m_storage.get(),
                executor_v1::StateKey{ledger::SYS_CODE_BINARY, codeHashEntry.get()},
                storage::Entry{std::move(code)});
        }

        if (auto codeABI = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{ledger::SYS_CONTRACT_ABI, codeHashEntry.get()});
            !codeABI || codeABI->size() == 0)
        {
            co_await storage2::writeOne(m_storage.get(),
                executor_v1::StateKey{ledger::SYS_CONTRACT_ABI, codeHashEntry.get()},
                storage::Entry{std::move(abi)});
        }

        co_await storage2::writeOne(m_storage.get(),
            executor_v1::StateKey{m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH},
            std::move(codeHashEntry));
    }

    task::Task<h256> codeHash()
    {
        if (auto codeHashEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            auto view = codeHashEntry->get();
            h256 codeHash((const bcos::byte*)view.data(), view.size());
            co_return codeHash;
        }
        co_return {};
    }

    task::Task<std::optional<storage::Entry>> abi()
    {
        // 先通过code hash从s_contract_abi找代码
        // Start by using the code hash to find the code from the s_contract_abi
        if (auto codeHashEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            if (auto abiEntry = co_await storage2::readOne(m_storage.get(),
                    executor_v1::StateKeyView{ledger::SYS_CONTRACT_ABI, codeHashEntry->get()}))
            {
                co_return abiEntry;
            }
        }

        // 在s_code_binary里没找到，可能是老版本部署的合约或internal
        // precompiled，代码在合约表的code字段里
        // I can't find it in the s_code_binary, it may be a contract deployed in the old version or
        // internal precompiled, and the code is in the code field of the contract table
        if (auto abiEntry = co_await storage2::readOne(
                m_storage.get(), executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::ABI}))
        {
            co_return abiEntry;
        }
        co_return {};
    }

    task::Task<u256> balance()
    {
        if (auto balanceEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::BALANCE}))
        {
            auto view = balanceEntry->get();
            auto balance = boost::lexical_cast<u256>(view);
            co_return balance;
        }
        co_return {};
    }

    task::Task<void> setBalance(const u256& balance)
    {
        storage::Entry balanceEntry(balance.str({}, {}));
        co_await storage2::writeOne(m_storage.get(),
            executor_v1::StateKey{m_tableName, ACCOUNT_TABLE_FIELDS::BALANCE},
            std::move(balanceEntry));
    }

    task::Task<std::optional<std::string>> nonce()
    {
        if (auto entry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, ACCOUNT_TABLE_FIELDS::NONCE}))
        {
            auto view = entry->get();
            co_return std::string(view);
        }
        co_return {};
    }

    task::Task<void> setNonce(std::string nonce)
    {
        storage::Entry nonceEntry(std::move(nonce));
        co_await storage2::writeOne(m_storage.get(),
            executor_v1::StateKey{m_tableName, ACCOUNT_TABLE_FIELDS::NONCE}, std::move(nonceEntry));
    }

    task::Task<void> increaseNonce()
    {
        if (auto currentNonce = co_await nonce())
        {
            const auto newNonce = u256(currentNonce.value()) + 1;
            co_await setNonce(newNonce.convert_to<std::string>());
        }
        else
        {
            BOOST_THROW_EXCEPTION(NonceNotInitialized{});
        }
    }

    task::Task<evmc_bytes32> storage(const evmc_bytes32& key)
    {
        if (auto valueEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{m_tableName, concepts::bytebuffer::toView(key.bytes)}))
        {
            auto field = valueEntry->get();
            evmc_bytes32 value;
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
            co_return value;
        }
        else
        {
            co_return {};
        }
    }

    // Tag-forwarding storage read: passes all tags through to the underlying
    // readOneRaw call. Callers compose the exact set of tags they need
    // (e.g. BYPASS_READ_SET | BYPASS_MULTILAYER for metadata reads that
    // must skip both conflict tracking and layer resolution).
    task::Task<evmc_bytes32> storage(const evmc_bytes32& key, auto... tags)
    {
        auto rawValue = co_await m_storage.get().readOneRaw(
            executor_v1::StateKey{m_tableName, concepts::bytebuffer::toView(key.bytes)}, tags...);
        evmc_bytes32 value{};
        if (auto* entry = std::get_if<storage::Entry>(std::addressof(rawValue)))
        {
            auto field = entry->get();
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
        }
        co_return value;
    }

    task::Task<void> setStorage(const evmc_bytes32& key, const evmc_bytes32& value)
    {
        storage::Entry valueEntry(concepts::bytebuffer::toView(value.bytes));

        co_await storage2::writeOne(m_storage.get(),
            executor_v1::StateKey{m_tableName, concepts::bytebuffer::toView(key.bytes)},
            std::move(valueEntry));
    }

    task::Task<std::optional<bcos::storage::Entry>> storageEntry(const std::string_view& key)
    {
        co_return co_await storage2::readOne(
            m_storage.get(), executor_v1::StateKeyView{m_tableName, key});
    }

    task::Task<std::string_view> path() { co_return m_tableName; }

    EVMAccount(const EVMAccount&) = delete;
    EVMAccount(EVMAccount&&) noexcept = default;
    EVMAccount& operator=(const EVMAccount&) = delete;
    EVMAccount& operator=(EVMAccount&&) noexcept = default;
    /// Construct directly from the account's table name, bypassing address→table-name routing
    /// entirely. Every method of this class reads nothing but `m_tableName`, so this is the
    /// primitive the two
    /// address-taking constructors below are sugar for; it adds no new semantics and changes
    /// nothing for existing callers.
    ///
    /// It exists for callers that must derive the table name themselves and need the *write*
    /// side pinned to the exact same string as their own reads. The address-taking constructors
    /// route the `c_systemTxsAddress` members to `/sys/` (see below); a caller that reads those
    /// addresses out of `/apps/` — as the Ethereum-compatible state view must, since in Ethereum
    /// they are ordinary accounts — would otherwise read one table and write another, a silent
    /// read/write split-brain. Handing over one already-computed table name removes the second,
    /// independent derivation rather than trying to keep two of them in agreement.
    EVMAccount(Storage& storage, FromTableName /*tag*/, std::string tableName)
      : m_storage(storage), m_tableName(std::move(tableName))
    {}

    EVMAccount(Storage& storage, const evmc_address& address, AddressTableMode mode)
      : m_storage(storage), m_tableName(accountTableName(address, mode))
    {}

    /**
     * @brief Construct a new EVMAccount object
     * @param storage storage instance
     * @param address address of the account, hex string, should not contain 0x prefix
     * @param mode how the account table name is derived (see AddressTableMode)
     */
    EVMAccount(Storage& storage, std::string_view address, AddressTableMode mode)
      : m_storage(storage), m_tableName(accountTableName(address, mode))
    {}

    EVMAccount(Storage& storage, const bcos::Address& address, AddressTableMode mode)
      : EVMAccount(
            storage,
            [](const bcos::Address& address) {
                evmc_address evmcAddress;
                ::ranges::copy(address, std::span{evmcAddress.bytes}.data());
                return evmcAddress;
            }(address),
            mode)
    {}
    ~EVMAccount() noexcept = default;

    std::string_view address() const { return m_tableName; }
};

template <class Storage>
inline std::ostream& operator<<(std::ostream& stream, EVMAccount<Storage> const& account)
{
    stream << account.address();
    return stream;
}
}  // namespace bcos::ledger::account
