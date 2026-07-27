// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Storage2Ledger — 真账本读桥(design doc §4:
// docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md)。在 storage2 世界
// (MultiLayerStorage/StateKey/EVMAccount 键空间)之上实现 evmone::state::StateView 的
// 三个只读方法:查缓存 → syncWait 读 storage2 → 归一化 → 回填缓存。
//
// KEEP 契约(自 bcos-evm/adapter/StateViewAdapter.h 迁入,该文件已删除):
//   StateView 是同步 noexcept 接口,get_account_code 按值返回整段代码。"存在但空"
//   的账户必须返回 Account{nonce=0, balance=0, code_hash=keccak(空), has_storage=false}
//   而非 std::nullopt——账户存在性与"全部字段取默认值"是两回事,折叠为 nullopt 会
//   致 EIP-7610 create 碰撞判定失真(design §3/§4.4)。
//
// 单线程契约:桥实例仅供单线程串行调用。三个读方法都是 const,但通过 mutable 缓存/
// 毒旗成员回填状态,不带任何锁——并发共享同一实例即数据竞争。并行调度接入时这是
// 第一个需要重新设计的点(design §4.1/§10)。
//
// 禁协程上下文调用:每次读操作由 task::syncWait 驱动单层协程(Wait.h:42-121)。禁止
// 在已处于协程上下文(即外层已有一个 syncWait 尚未返回)时再次调用本桥的读方法——
// 嵌套 syncWait 是已知的栈陷阱,本桥不做该场景下的正确性保证(design §4.1)。
//
// 唯一写者不变式:桥实例存续期内(一块一实例,不提供 reset()),底层 storage2 的
// 唯一写入路径是本桥自身的 applyDiff(Task 4 落地);越过桥直接写底层存储,会使三张
// 读缓存(账户/槽/code)静默失真而不自知,属使用错误,桥不做检测(design §4.2)。
//
// 毒旗错误通道:三个读方法均 noexcept,内部 catch 全部异常,首次触发时置位
// poisoned() 并记录 firstError()(只记第一条,后续错误不覆盖),同时返回“安全值”
// (nullopt / 空字节 / 全零 bytes32)。消费方契约:块执行结束后必须检查
// poisoned(),一旦置位就让整块失败——绝不能把底层存储错误静默降级为“账户不存在”
// (design §4.3)。nonce 字段额外做“转宽类型 → 显式 > UINT64_MAX 检查 → 才窄化”的
// 处理,不依赖 convert_to 对越界的隐式截断行为(本仓 costOfPrecompiled 一支
// convert_to<int64_t>() 越界静默截断的前科,MEMORY costofprecompiled-int64-overflow)。
//
// 存在性判据:SYS_TABLES 中以账户表路径("/apps/<hex(addr)>",feature_raw_address=off
// 前提)为键的标记行,判据锚定 EVMAccount 的同一路径构造逻辑,但不复制其到 /sys/ 的
// 路由分支——遇到 c_systemTxsAddress 集合地址,桥直接毒旗,不猜测路由(design §4.4)。
//
// has_storage 判据:账户表 range seek 探测是否存在至少一个 32 字节原始键(区别于
// ACCOUNT_TABLE_FIELDS 中的已知短字段名),冷读时探测一次并入账户缓存(design §4.4)。

#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <boost/algorithm/hex.hpp>
#include <array>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bcos::evm::ledger
{

/// 账户表原始存储槽键的固定长度(32 字节),用于在 has_storage range seek 探测中把
/// 槽键(evmc::bytes32,固定 32 字节)同 ACCOUNT_TABLE_FIELDS 的已知短字段名区分开
/// (design §6:"已知字段名=字段,32 字节原始键=槽")。
inline constexpr std::size_t kStorageSlotKeySize = sizeof(evmc_bytes32::bytes);

template <class Storage>
class Storage2Ledger final : public evmone::state::StateView
{
public:
    /// 一块一实例,不提供 reset()(design §4.2)。
    explicit Storage2Ledger(Storage& storage) noexcept : m_storage(storage) {}

    Storage2Ledger(const Storage2Ledger&) = delete;
    Storage2Ledger(Storage2Ledger&&) = delete;
    Storage2Ledger& operator=(const Storage2Ledger&) = delete;
    Storage2Ledger& operator=(Storage2Ledger&&) = delete;
    ~Storage2Ledger() override = default;

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        if (auto it = m_accountCache.find(addr); it != m_accountCache.end())
            return it->second;

        std::string tableName;
        if (!accountTableName(addr, tableName))
        {
            poison(
                "Storage2Ledger::get_account: address routes to /sys/ (c_systemTxsAddress "
                "member); bridge refuses to guess the routing, see design §4.4");
            return std::nullopt;
        }

        try
        {
            auto fetched = task::syncWait(fetchAccount(tableName));
            m_accountCache.emplace(addr, fetched);
            return fetched;
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2Ledger::get_account: unknown exception");
        }
        return std::nullopt;
    }

    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        if (auto it = m_codeCache.find(addr); it != m_codeCache.end())
            return it->second;

        std::string tableName;
        if (!accountTableName(addr, tableName))
        {
            poison(
                "Storage2Ledger::get_account_code: address routes to /sys/ (c_systemTxsAddress "
                "member); bridge refuses to guess the routing, see design §4.4");
            return {};
        }

        try
        {
            auto code = task::syncWait(fetchCode(tableName));
            m_codeCache.emplace(addr, code);
            return code;
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2Ledger::get_account_code: unknown exception");
        }
        return {};
    }

    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override
    {
        auto cacheKey = std::make_pair(addr, key);
        if (auto it = m_storageCache.find(cacheKey); it != m_storageCache.end())
            return it->second;

        std::string tableName;
        if (!accountTableName(addr, tableName))
        {
            poison(
                "Storage2Ledger::get_storage: address routes to /sys/ (c_systemTxsAddress "
                "member); bridge refuses to guess the routing, see design §4.4");
            return {};
        }

        try
        {
            auto value = task::syncWait(fetchStorage(tableName, key));
            m_storageCache.emplace(cacheKey, value);
            return value;
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2Ledger::get_storage: unknown exception");
        }
        return {};
    }

    /// 消费方契约(design §4.3):块执行结束必须检查,置位即整块失败。
    [[nodiscard]] bool poisoned() const noexcept { return m_poisoned; }
    /// 只记第一条错误;实例隔离——毒旗随桥实例生命周期,新实例不受影响。
    [[nodiscard]] std::string_view firstError() const noexcept { return m_firstError; }

private:
    void poison(std::string_view reason) const noexcept
    {
        if (m_poisoned)
            return;
        m_poisoned = true;
        try
        {
            m_firstError.assign(reason);
        }
        catch (...)
        {
            // firstError 本身分配失败不应二次抛出;poisoned() 已置位,消费方仍能判定失败。
        }
    }

    /// 计算账户表路径("/apps/<hex(addr)>",feature_raw_address=off 前提),判据锚定
    /// EVMAccount 的同一构造逻辑。命中 c_systemTxsAddress 集合(会被 EVMAccount 路由到
    /// /sys/)时返回 false——桥不复制该路由分支,调用方须毒旗。
    static bool accountTableName(const evmc::address& addr, std::string& tableName)
    {
        std::array<char, sizeof(addr.bytes) * 2> hex{};  // NOLINT
        boost::algorithm::hex_lower(
            std::string_view(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes)),
            hex.data());
        std::string_view hexView(hex.data(), hex.size());
        if (bcos::precompiled::contains(bcos::precompiled::c_systemTxsAddress, hexView))
            return false;

        tableName.reserve(bcos::ledger::SYS_DIRECTORY::USER_APPS.size() + hexView.size());
        tableName.append(bcos::ledger::SYS_DIRECTORY::USER_APPS);
        tableName.append(hexView);
        return true;
    }

    task::Task<std::optional<Account>> fetchAccount(std::string tableName) const
    {
        if (!co_await storage2::existsOne(
                m_storage.get(), executor_v1::StateKeyView(bcos::ledger::SYS_TABLES, tableName)))
            co_return std::nullopt;

        Account account{};
        // 空账户归一化默认值:code_hash = keccak(空)(design §4.4);nonce/balance 默认为 0
        // 由 Account 的成员默认初始化提供,has_storage 探测在下方进行。
        account.code_hash = evmone::keccak256(evmc::bytes_view{});

        if (auto balanceEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE});
            balanceEntry && !balanceEntry->get().empty())
        {
            account.balance = intx::from_string<intx::uint256>(std::string(balanceEntry->get()));
        }

        if (auto nonceEntry = co_await storage2::readOne(m_storage.get(),
                executor_v1::StateKeyView{tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE});
            nonceEntry && !nonceEntry->get().empty())
        {
            // 十进制串 → 宽类型(intx::uint256)→ 显式 > UINT64_MAX 检查 → 才窄化。
            // 不直接解析/窄化到 uint64_t——避免依赖某个转换函数在越界时是否抛异常这个
            // 不确定行为(本仓 convert_to<int64_t>() 越界静默截断前科)。
            auto nonceValue = intx::from_string<intx::uint256>(std::string(nonceEntry->get()));
            static constexpr intx::uint256 maxUint64{std::numeric_limits<uint64_t>::max()};
            if (nonceValue > maxUint64)
                throw std::overflow_error(
                    "Storage2Ledger::fetchAccount: nonce exceeds uint64_t range (silent-"
                    "truncation guard, design §4.3)");
            account.nonce = static_cast<uint64_t>(nonceValue);
        }

        if (auto codeHashEntry = co_await storage2::readOne(
                m_storage.get(), executor_v1::StateKeyView{tableName,
                                     bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH});
            codeHashEntry && !codeHashEntry->get().empty())
        {
            auto view = codeHashEntry->get();
            if (view.size() != sizeof(account.code_hash.bytes))
                throw std::length_error(
                    "Storage2Ledger::fetchAccount: codeHash field size mismatch");
            std::memcpy(account.code_hash.bytes, view.data(), view.size());
        }

        account.has_storage = co_await probeHasStorage(tableName);
        co_return account;
    }

    /// has_storage 判据(design §4.4):账户表 range seek 探测首个 32 字节原始键
    /// (区别于 ACCOUNT_TABLE_FIELDS 的已知短字段名)。
    task::Task<bool> probeHasStorage(std::string_view tableName) const
    {
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            executor_v1::StateKeyView view(key);
            auto [table, fieldKey] = view.get();
            if (table != tableName)
                co_return false;
            if (fieldKey.size() == kStorageSlotKeySize)
                co_return true;
            // 已知字段名(codeHash/code/balance/abi/nonce/alive/frozen/shard)——继续找下一行。
        }
        co_return false;
    }

    /// get_account_code:经 CODE_HASH → SYS_CODE_BINARY 读(design §4.4 接口小节);本桥
    /// 只服务 storage2 新栈,不复刻 EVMAccount::code() 对遗留 CODE 字段的回退分支。
    task::Task<evmc::bytes> fetchCode(std::string tableName) const
    {
        auto codeHashEntry = co_await storage2::readOne(m_storage.get(),
            executor_v1::StateKeyView{tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH});
        if (!codeHashEntry || codeHashEntry->get().empty())
            co_return evmc::bytes{};

        auto codeEntry = co_await storage2::readOne(m_storage.get(),
            executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY, codeHashEntry->get()});
        if (!codeEntry)
            co_return evmc::bytes{};

        auto view = codeEntry->get();
        co_return evmc::bytes(view.begin(), view.end());
    }

    task::Task<evmc::bytes32> fetchStorage(std::string tableName, evmc::bytes32 key) const
    {
        std::string_view keyView(reinterpret_cast<const char*>(key.bytes), sizeof(key.bytes));
        if (auto entry = co_await storage2::readOne(
                m_storage.get(), executor_v1::StateKeyView{tableName, keyView}))
        {
            auto view = entry->get();
            evmc::bytes32 value{};
            if (view.size() == sizeof(value.bytes))
                std::memcpy(value.bytes, view.data(), view.size());
            co_return value;
        }
        co_return evmc::bytes32{};
    }

    std::reference_wrapper<Storage> m_storage;

    // 三张块级读缓存(design §4.2):nullopt/零值也缓存,消掉负查询浪费。写穿由
    // applyDiff(Task 4)维护;桥不提供 reset(),缓存随实例生命周期自然复位。
    mutable std::map<evmc::address, std::optional<Account>> m_accountCache;
    mutable std::map<evmc::address, evmc::bytes> m_codeCache;
    mutable std::map<std::pair<evmc::address, evmc::bytes32>, evmc::bytes32> m_storageCache;

    mutable bool m_poisoned{false};
    mutable std::string m_firstError;
};

}  // namespace bcos::evm::ledger
