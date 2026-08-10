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
// 禁协程上下文调用(默认规则):每次读操作由 task::syncWait 驱动单层协程(Wait.h:42-121)。
// 默认禁止在已处于协程上下文(即外层已有一个 syncWait 尚未返回)时再次调用本桥的读
// 方法——嵌套 syncWait 是已知的栈陷阱,本桥的正确性保证以下述条件式许可为界,超出
// 该许可范围的嵌套不受保证(design §4.1)。
//
// 条件式许可(op-validator-loop design §4.4,取代终审 I-3"仅一层、仅一个调用点"的
// 表述——该表述已不敷合流执行链的实际嵌套深度,改写为以下三条):
// 1) 嵌套拓扑声明:本条款许可的嵌套链路为 engine newPayload → executeOpBlock →
//    桥读方法(syncWait)→(stateRoot 段)visitAccounts(syncWait)→ 惰性 code getter
//    (AccountView::code(),§6 惰性 code getter,内部经 get_account_code 再次 syncWait
//    驱动 fetchCode)——多层嵌套,不再限定"仅一层、仅一个调用点";
// 2) 安全前提:a) 桥对接的 storage2 后端全部在 co_await 处线程内同步完成(内存
//    MultiLayerStorage;RocksDB 为线程内阻塞读),内层任务从不真正让出线程、外层协程
//    从不跨线程恢复,嵌套 syncWait 因而退化为纯栈递归,而非真正的并发调度;
//    b) engine 执行段整体被 `x_state` 锁串行(单线程执行,同一时刻至多一条嵌套链路
//    在跑,不存在并发观察窗口);
// 3) 失效判据:任何后端引入跨线程/事件循环的真正异步完成语义,本许可立即失效,必须
//    重新设计——该判据同时覆盖 handleNewPayload"持锁跨 co_await"现存 TODO,两者共享
//    同一安全前提(op-validator-loop design §1/§4.4)。
//
// 唯一写者不变式:桥实例存续期内(一块一实例,不提供 reset()),底层 storage2 的
// 唯一写入路径是本桥自身的 applyDiff(Task 4 落地);越过桥直接写底层存储,会使三张
// 读缓存(账户/槽/code)静默失真而不自知,属使用错误,桥不做检测(design §4.2)。
//
// 毒旗错误通道(rev. 终审批 9 F-1:**读写两路都走这条通道**,只是形态不同——读方法 noexcept
// 吞异常+置旗;applyDiff 置旗后**照样上抛**,strict tripwire 语义不变。理由见 applyDiff 的注释:
// 毒旗在 OpSchedulerImpl 里是 -32603/INVALID 的分类判据,而写回路径的失败**全是本地故障**):
// 三个读方法均 noexcept,内部 catch 全部异常,首次触发时置位
// poisoned() 并记录 firstError()(只记第一条,后续错误不覆盖),同时返回“安全值”
// (nullopt / 空字节 / 全零 bytes32)。消费方契约:块执行结束后必须检查
// poisoned(),一旦置位就让整块失败——绝不能把底层存储错误静默降级为“账户不存在”
// (design §4.3)。nonce 字段额外做“转宽类型 → 显式 > UINT64_MAX 检查 → 才窄化”的
// 处理,不依赖 convert_to 对越界的隐式截断行为(本仓 costOfPrecompiled 一支
// convert_to<int64_t>() 越界静默截断的前科,MEMORY costofprecompiled-int64-overflow)。
//
// 存在性判据:SYS_TABLES 中以账户表路径("/apps/<hex(addr)>")为键的标记行。表名推导与主线
// MPT 的 `bcos::ledger::mpt::accountTableName`/`parseAccountTable`(`Classify.h:83-117`)同源,
// **对所有地址一视同仁**——包括 c_systemTxsAddress 的 8 个地址,它们在以太坊侧就是普通地址
// (终审批 A · A-2,详见本文件 accountTableName 的注释;此前它们命中即毒旗,导致一笔合法交易
// 就让节点在该块上永久卡死)。FISCO 自己的 `/sys/<hex>` 控制面对 OP 执行世界不可见。
//
// ── 外部依赖:feature_raw_address 必须为 off(终审批 A · A-3)────────────────────────────
// 本桥的表名是硬编码 hex 路径。`feature_raw_address=on` 时账户表改用 20 字节**二进制**名,
// 桥的所有读会静默返回"账户不存在"——即在一个空世界上执行整个块,静默分叉。
// 这条前提的守护**不在本桥内**,而是上游 `scheduler_v1::validateMPTFlagMatrix`
// (`transaction-scheduler/bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h:107-138`):
// 它在 `libinitializer/LedgerInitializer.cpp:48-49` 于**启动时**拒绝
// `feature_raw_address && (feature_mpt_state_root || feature_l2_ethereum_compat)`,而 OP 模式
// 恒带 `feature_l2_ethereum_compat`(`Features.h:116`;`NodeConfig.cpp:299-316` 令它与
// `[alloc.*]` 互为充要条件),故启动路径被完整覆盖。
// **未覆盖的那一半**:上游的运行时守卫 `rejectRawAddressWithMPT` 唯一调用点在
// `BaselineScheduler::coExecuteBlock`,而 OP 路径走 `OpSchedulerImpl::executeOpBlock`,不经过
// 它;因此**中途**由系统配置交易打开 `feature_raw_address`,在下次重启前不会被任何守卫拦住。
// 今天该缺口不可达(`OpSchedulerImpl` 尚无生产组合根),但 **OP 组合根接线时必须补上
// `rejectRawAddressWithMPT` 的等价物**。启动这一半由 `Storage2LedgerTest` 的
// `RawAddressWithL2CompatIsRejectedAtStartup` 钉住:上游哪天放宽那个组合,红的是我们这条。
//
// has_storage 判据:账户表 range seek 探测是否存在至少一个 32 字节原始键(区别于
// ACCOUNT_TABLE_FIELDS 中的已知短字段名),冷读时探测一次并入账户缓存(design §4.4)。
//
// 零值槽语义(终审批 9):**槽值为 0 等价于该槽不存在**——以太坊 trie 会删掉零值槽,本仓
// accountStorageRoot/opStorageRoot 也是 is_zero → continue。该语义在本桥的**所有**判据上统一:
// probeHasStorage 不因零值行判 has_storage=true;fetchAllStorage 跳过零值行(既不 throw 也不进
// 建根 map);fetchStorage/get_storage 对"行不存在"与"行存在但值为零"返回同一个全零 bytes32。
// 理由:该语义必须对生产账本成立——生产写路径(transaction-executor HostContext::set、
// bcos-ledger 创世 alloc 导入)对零值照写不删,真实链账户表**必然**含零值槽行。反过来,
// "桥的写回从不留下零值槽行"(契约②)这条 E-b 不变量的守护在写路径:applyModifiedEntry 的
// 删槽后置回读,而不再在读路径上拦(在读路拦会让节点每块 -32603)。

#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Overloaded.h>
#include <boost/algorithm/hex.hpp>
#include <algorithm>
#include <array>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

        try
        {
            auto fetched = task::syncWait(fetchAccount(accountTableName(addr)));
            m_accountCache.emplace(addr, fetched);
            return fetched;
        }
        // ── 四级 catch 阶梯(终审批 9 fix2 F2-1)──────────────────────────────────────
        // 读路四个方法(get_account / get_account_code / get_storage / visitAccounts)与
        // applyDiff 共用同一条阶梯,理由与形态见本文件 applyDiff 的注释。此处只记**为什么读路
        // 也必须四级**:本仓 typed-catch 的判别式是**异常类型族**——`std::runtime_error` 子树
        // 的 typed catch 不生效(逃逸到 `catch (...)`),`std::logic_error` 子树正常生效。
        // 读路的 throw 两族都有(`fetchAllStorage` 的 "unknown key in account table" 是
        // runtime_error;`fetchStorage`/`fetchAllStorage` 的值长度校验是 length_error),
        // 两级阶梯下**前者今天就在丢消息**:`firstError()` 退化成 "unknown exception",
        // 运维拿到的 -32603 理由为空内容。而读路是毒旗的**主要**来源。
        // 消息保真由 `Storage2LedgerTest` 的 `ReadPathRuntimeErrorKeepsOriginalMessage`
        // 见证(与 (d2) 的 expectOpStorageErrorWithMessage 对称的读路那一半)。
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::logic_error& e)
        {
            poison(e.what());
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

        try
        {
            auto code = task::syncWait(fetchCode(accountTableName(addr)));
            m_codeCache.emplace(addr, code);
            return code;
        }
        // 四级阶梯,理由见 get_account 同名注释(F2-1)。
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::logic_error& e)
        {
            poison(e.what());
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

        try
        {
            auto value = task::syncWait(fetchStorage(accountTableName(addr), key));
            m_storageCache.emplace(cacheKey, value);
            return value;
        }
        // 四级阶梯,理由见 get_account 同名注释(F2-1)。
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::logic_error& e)
        {
            poison(e.what());
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

    /// 写回(design §5):**单一 strict 形态**——deleted_accounts 项在底层不存在即 tripwire
    /// (std::runtime_error),不提供 raw 版。序列化格式不自造:balance/nonce/code/create 逐字段
    /// 委托 `ledger::account::EVMAccount`(syncWait 驱动,与读桥同源);槽零值删除与账户删除走
    /// storage2 `removeOne`/`range` 扫删(EVMAccount 无删除 API)。**每个** modified entry 无条件
    /// ensure-exists,不因空 entry 跳过(EIP-161 touch-only / 完全空账户播种前置)。写穿:每步
    /// 同步更新三张读缓存;删除账户 ⇒ 三表按地址全量失效/置负(含全部已缓存槽与 code,design
    /// §4.2——CREATE2 同址重生场景,漏清即静默脏读)。不是 noexcept:strict tripwire 允许上抛。
    ///
    /// **写回失败也走毒旗通道(终审批 9 F-1)**:本方法保留上抛(strict tripwire 语义不变),但
    /// 在上抛之前**先置毒旗**。理由是错误分类,不是错误处理风格——
    /// `OpSchedulerImpl::executeOpBlock` 的分类逻辑是"`poisoned()` 为真 → `OpStorageError`
    /// (-32603),否则 → `OpConsensusError`(INVALID)"。写回路径的每一种失败(deleted 项
    /// ghost-delete、系统地址路由、契约②零值槽写回泄漏、写穿重读时的 nonce 越界/字段长度违规、
    /// 以及底层存储自身的失败)守护的都是"**本节点自己写回或自己的存储有问题**",即**本地故障**;
    /// 而 `applyDiff` 的输入来自 evmone 自己算出的 StateDiff,不是来自 payload——畸形 payload 早在
    /// 解码/processOpBlock 阶段就被拒了,走不到这里。因此这里没有任何一种失败该被答成 INVALID:
    /// 用 INVALID 回答一个合法 payload,等于本节点投票反对规范链并永久分叉(既有用例
    /// `StorageLayoutFaultIsInternalErrorNotInvalid` 防的正是这类错误)。
    /// 形态刻意是**整体 try/catch 包裹**而非逐个 throw 点改写:它把"写回路径的任何失败都是本地
    /// 故障"表达成一条不变量,新增的 throw 点自动继承,不会再漏一个;`catch (...)` 兜底不是冗余
    /// ——本仓已知 `-fno-rtti` 的 typed-catch 旁路(见 `OpSchedulerImpl.h` 同名注释),跨库抛出的
    /// `std::exception` 可能不匹配 typed catch,兜底保证**毒旗一定置位**(代价仅是丢掉消息文本)。
    /// `seeding`(终审批 D-6):true 时豁免"新建 EIP-161 空账户"守卫。seedFromTestState 经
    /// 同一条 applyDiff 落账 pre 中的完全空账户(EIP-161 touch-delete 向量前置,KEEP 契约——
    /// 三后端同根要求 pre 空账户也落表),那是创世快照而非块执行,不在此判;executeOpBlock 的
    /// 执行路径与所有直接调用方恒走默认 false(守卫开)。参数按单次调用作用域生效,不落桥实例
    /// 状态——同一块内先播种、后执行,执行段的 applyDiff 仍带守卫。
    void applyDiff(const evmone::state::StateDiff& diff, bool seeding = false)
    {
        try
        {
            for (const auto& entry : diff.modified_accounts)
                task::syncWait(applyModifiedEntry(entry, seeding));
            for (const auto& addr : diff.deleted_accounts)
                task::syncWait(applyDeletedEntry(addr));
        }
        // catch 阶梯的顺序不是风格问题,是本仓 `-fno-rtti` typed-catch 旁路的**实测**结果
        // (终审批 9 F-1 实测两轮,fix 轮复核者独立重做过,勿凭直觉重排或删级):
        //   * 实验一:只把 `catch (const std::runtime_error&)` 里的 poison 抑制掉 → 本文件全部
        //     四条写路见证(桥层三条 + OpSchedulerImplTest 的分类层一条,该测试在源分支未移植到此)**同时翻红** ⇒ 这些
        //     throw 全部落在**第一级**。
        //   * 实验二:整条 `catch (const std::runtime_error&)` 删掉,并给余下各级的 poison 打上
        //     可区分前缀 → 同样四条见证的 firstError() 全部是 **`[VIA-ELLIPSIS]`**,一条
        //     `[VIA-EXCEPTION]` 都没有。
        //   * 实验三(re-review INJ-R2,**推翻了本注释此前的机制归因**):三处 catch 全剥回
        //     两级、同一 TU 同一二进制、四探针对照 ⇒ `runtime_error` 在 `applyDiff` **和**
        //     `visitAccounts` 上都逃逸到 `catch (...)`;`length_error` 在 `get_storage`
        //     **和** `visitAccounts` 上都正常命中 `catch (const std::exception&)`。
        //
        // **判别式是异常的类型族,不是协程边界**:`std::runtime_error` 子树的 typed catch
        // 不生效,`std::logic_error` 子树正常生效。(此前这里写过"直抛 vs 穿协程"的对照,
        // 已被实验三证伪 —— `visitAccounts:327` 同样是 `syncWait(协程)`,穿的是同一种边界。)
        //
        // **机制未定,以实测为准。** 本注释一度归因为"非唯一的那份 typeinfo 是 `std::exception`
        // 的(来自 `-fno-rtti` 的 libevmone.a 隐藏副本)",该归因与实验三矛盾:若成立,
        // `length_error` 也该逃逸,而实际没有。真正的成因尚未定位,不要基于任何一种猜测去
        // 重排或删级 —— 只依据上面的类型族判别式。
        //
        // 因此先按具体基类捕获(runtime_error / logic_error 覆盖本文件全部 throw:
        // runtime_error 系含 overflow_error,logic_error 系含 length_error);`std::exception`
        // 这一级**必须保留**——按实验三它正是 `logic_error` 族**唯一正确命中**的那一级,
        // 也是将来新增的、不属于上面两族的标准异常的落点;`catch (...)` 兜底保证毒旗一定置位。
        // **四级都置毒旗**——分类的正确性只依赖毒旗置位,不依赖消息;消息只影响 -32603 的
        // 可诊断性(读路的消息保真见 get_account 的四级阶梯注释与 (z10))。
        catch (const std::runtime_error& e)
        {
            poison(e.what());
            throw;
        }
        catch (const std::logic_error& e)
        {
            poison(e.what());
            throw;
        }
        catch (const std::exception& e)
        {
            poison(e.what());
            throw;
        }
        catch (...)
        {
            poison(
                "Storage2Ledger::applyDiff: unknown exception on the write-back path (not derived "
                "from std::runtime_error/std::logic_error, or typed catch bypassed by the known "
                "-fno-rtti RTTI issue; message unavailable)");
            throw;
        }
    }

    /// AccountVisitor payload (design §6): nonce/balance/codeHash + a lazily-evaluated code
    /// getter (state-root computation never calls it — avoids an unconditional SYS_CODE_BINARY
    /// read per account) + the account's already-materialized, tombstone-filtered,
    /// poison-checked live storage slot map (see fetchAllStorage). Field names mirror
    /// MemoryLedger::AccountView exactly so bcos::evm::stateRootOf<Ledger> works unmodified
    /// against either backend.
    struct AccountView
    {
        const evmc::address& addr;
        uint64_t nonce;
        const intx::uint256& balance;
        evmc::bytes32 codeHash;
        const std::map<evmc::bytes32, evmc::bytes32>& storage;

        [[nodiscard]] evmc::bytes code() const noexcept { return m_bridge->get_account_code(addr); }

        const Storage2Ledger* m_bridge;
    };

    /// Traverses every live account under the /apps/ namespace (design §6). noexcept +
    /// poison-flag contract — the same shape as get_account/get_account_code/get_storage:
    /// storage2 errors (including layout-invariant violations — an unknown key in an account
    /// table, or a slot value whose length is not 32 bytes; a stored *zero-valued* slot is NOT
    /// one of them any more, see fetchAllStorage / final batch 9) are caught here, poison() is
    /// called, and the traversal stops early returning false. The visitor is invoked
    /// synchronously per live account and must itself return bool: false aborts the traversal
    /// early without poisoning (a plain "visitor is done" signal, distinct from a
    /// poisoned/failed traversal). Consumer contract (design §6 "遍历产物全部作废"): after this
    /// returns, check poisoned() before trusting anything the visitor produced.
    template <class Visitor>
    bool visitAccounts(Visitor&& visitor) const noexcept
    {
        try
        {
            return task::syncWait(visitAccountsImpl(visitor));
        }
        // 四级阶梯,理由见 get_account 同名注释(F2-1)。这一条是四个读方法里**最要紧**的:
        // re-review 实测的丢消息实例正是它——`fetchAllStorage` 的 "unknown key in account
        // table '/apps/…'"(runtime_error)在两级阶梯下逃逸到 catch(...),firstError() 退化
        // 成 "unknown exception";而 visitAccounts 是 stateRootOf 的必经路径,它的毒旗消息
        // 就是运维排查 -32603 时唯一的线索。
        catch (const std::runtime_error& e)
        {
            poison(e.what());
        }
        catch (const std::logic_error& e)
        {
            poison(e.what());
        }
        catch (const std::exception& e)
        {
            poison(e.what());
        }
        catch (...)
        {
            poison("Storage2Ledger::visitAccounts: unknown exception");
        }
        return false;
    }

private:
    task::Task<void> applyModifiedEntry(const evmone::state::StateDiff::Entry& entry, bool seeding)
    {
        const std::string tableName = accountTableName(entry.addr);

        // **表名只推导一次,读写共用同一个字符串**(终审批 A · A-2)。此处刻意用 EVMAccount 的
        // `FromTableName` 直构而不是 `EVMAccount(storage, addr, false)`:后者的构造函数自己会把
        // c_systemTxsAddress 的 8 个地址路由进 `/sys/`(EVMAccount.h:239-245),而本桥的读路
        // (以及 removeOne / existsOne / range / 写穿重读)一律走 `/apps/`——两者一旦分歧就是
        // **读 /apps/、写 /sys/ 的脑裂**,比原先那条毒旗更坏。
        //
        // 这不只是"避开系统地址"这一个 case:写路此前存在**两套独立的表名推导**(桥的
        // accountTableName 与 EVMAccount 构造函数内部那套),它们的一致性完全靠桥**拒绝**处理
        // 二者会分歧的那 8 个地址来维持。删掉拒绝就必须删掉第二套推导,而不是让两套继续并存
        // ——两处独立实现同一规则正是本仓 yParity 事故的成因。
        bcos::ledger::account::EVMAccount<Storage> account(
            m_storage.get(), bcos::ledger::account::FromTableName{}, tableName);

        // 账户 ensure-exists(design §5,rev.2 补):无条件确保 SYS_TABLES 标记行存在,不得优化为
        // "无字段可写则跳过"——pre 中完全空账户(EIP-161 touch-delete 向量前置)正依赖此落账;
        // evmone build_diff 把只读 touched 账户也放进 modified,对其重写同值 nonce/balance 无害。
        const bool createdNew = !co_await account.exists();

        // D-6 守卫(终审批 D):在账本上**新建**一个 EIP-161 空账户(nonce=0 ∧ balance=0 ∧ 无码)
        // 是协议违规路径——EIP-161 空账户按"不存在"处理,不应落表。今天靠"所有创建路径都 bump
        // nonce / 写 balance / 写 code"侥幸不触发(V2 补 `OpHost.cpp:62` 后穷举完整);此守卫把
        // "未来新增一条不 bump nonce 的创建路径"固定为立刻翻红(-32603 via 毒旗),而不是与既有
        // 路径的一致性再次靠运气维持。只查 createdNew:已存在的账户被改成空值是 delete 语义
        // (deleted_accounts)的本职,不在此判;build_diff 对 touch-only 已存在账户重写同值
        // nonce/balance 也走这里,但那些账户 createdNew=false。判据与 evmone `Account::is_empty()`
        // (EIP-161)同义,不含 storage——storage 不算非空。
        //
        // 为什么执行路径不会误伤(build_diff 已兜底):evmone `State::build_diff` 把带
        // `erase_if_empty` 的触摸空账户路由去 deleted_accounts / 跳过(`state.cpp:214-219`),即
        // 零值 CALL 的 `touch()`、authorization list 的
        // `get_or_insert(addr, {.erase_if_empty = true})` 都不会作为"新建空账户"走进 modified
        // ——能走到这里的一定是默认 `get_or_insert`(erase_if_empty=false)且最终为空的创建,
        // 正是本守卫要钉的路径。
        //
        // `seeding` 豁免:seedFromTestState 经同一条 applyDiff 落账 pre 中的完全空账户(EIP-161
        // touch-delete 向量前置,KEEP 契约——三后端同根要求 pre 空账户也落表),那是创世快照
        // 而非块执行,守卫放行;executeOpBlock 的执行路径恒走默认 false(守卫开)。
        if (!seeding && createdNew && entry.nonce == 0 && entry.balance == 0 &&
            (!entry.code.has_value() || entry.code->empty()))
        {
            throw std::runtime_error(
                "Storage2Ledger::applyDiff: EIP-161-empty account would be created in the ledger "
                "by a diff entry that never bumped nonce (address table '" +
                tableName + "', §6.4 D-6)");
        }
        if (createdNew)
            co_await account.create();

        co_await account.setBalance(bcos::u256(intx::to_string(entry.balance)));
        co_await account.setNonce(std::to_string(entry.nonce));

        // 契约③:code 仅 has_value() 时覆写;codeHash 由本桥自行 keccak(code)(StateDiff 无
        // code_hash 字段);不写 ABI 内容(setCode 的 abi 形参传空串——往返测试显式豁免 abi 字段,
        // design §5/§7)。
        if (entry.code.has_value())
        {
            const auto codeHash = evmone::keccak256(*entry.code);
            bcos::h256 codeHashValue(
                reinterpret_cast<const bcos::byte*>(codeHash.bytes), sizeof(codeHash.bytes));
            co_await account.setCode(
                bcos::bytes(entry.code->begin(), entry.code->end()), std::string{}, codeHashValue);
        }

        // 契约②:槽值为 0 = 删槽(storage2::removeOne,不写零值,EVMAccount 无删除 API);
        // 非零走 EVMAccount::setStorage(与读桥 fetchStorage 同一键空间)。
        for (const auto& [key, value] : entry.modified_storage)
        {
            if (evmc::is_zero(value))
            {
                std::string_view keyView(
                    reinterpret_cast<const char*>(key.bytes), sizeof(key.bytes));
                co_await storage2::removeOne(
                    m_storage.get(), executor_v1::StateKeyView{tableName, keyView});

                // 契约②的不变量守护(终审批 9,方案 A:守护搬到写回路径)。原先这条守护长在读路
                // ——fetchAllStorage 见到零值槽行就 throw+毒旗——但读路同时必须服务**不遵守本契约**
                // 的生产账本(HostContext.h:288 与 Ledger.cpp:1844 的 setStorage 对零值照写不删,
                // 真实链账户表必然含零值槽行),在那里检查等于让节点每块 -32603。不变量的主体是
                // **写**,守护随之搬回写路径:检查只针对本次 diff 自己写零的这些键,与账本里既有的
                // 零值行无关(后者按以太坊语义由读路跳过)。
                // 形态刻意不是 setStorage 前的 assert(!is_zero(value)):上面的 if/else 结构令这种
                // 断言永不可达、也永不可测(六步自验无从翻红),它防呆而不守护。改为**删槽后置回读**
                // ——校验的是结果而非意图:removeOne 未生效、或被底层降级成一次写入,都会当场响。
                // 判据为什么是 existsOne 而不是本文件的 liveContent():removeOne 在
                // LOGICAL_DELETION 语义的存储(生产 MultiLayerStorage)上写的是 DELETED_TYPE
                // **墓碑**而非物理擦除,判据若把墓碑当"存在",这条守护就会在每一次正常的零值写入上
                // 误报。已实测(测试 (z7) 两段:range() 仍能扫到该墓碑行,而 existsOne 对同一键返回
                // false):existsOne/readOne 在层间解析里已经做完了 liveContent() 对 range() 原始变体
                // 手工做的那件事(墓碑 → 不存在),它们的返回值里没有变体可判,两者同源不重复。
                if (co_await storage2::existsOne(
                        m_storage.get(), executor_v1::StateKeyView{tableName, keyView}))
                    throw std::runtime_error(
                        "Storage2Ledger::applyDiff: zero-valued slot write left the row alive in "
                        "account table '" +
                        tableName +
                        "' (contract ② write-back leak: the bridge's write-back must never leave a "
                        "zero-valued storage slot row behind — zero means the slot is deleted)");
            }
            else
            {
                co_await account.setStorage(key, value);
            }
        }

        // 写穿(design §4.2):账户/code 缓存经权威重读同步刷新——直接复用读路径的
        // fetchAccount/fetchCode,避免读写两处分叉出不一致的字段默认值/has_storage 计算逻辑;
        // 槽缓存按本轮写入的确切值直接更新(契约②:零值缓存为全零 bytes32,与读路径对已删槽的
        // 归一化返回值一致)。
        m_accountCache.insert_or_assign(entry.addr, co_await fetchAccount(tableName));
        m_codeCache.insert_or_assign(entry.addr, co_await fetchCode(tableName));
        for (const auto& [key, value] : entry.modified_storage)
        {
            m_storageCache.insert_or_assign(
                std::make_pair(entry.addr, key), evmc::is_zero(value) ? evmc::bytes32{} : value);
        }
    }

    task::Task<void> applyDeletedEntry(const evmc::address& addr)
    {
        const std::string tableName = accountTableName(addr);

        // strict 单形态(design §5):tripwire 内置——底层不存在即使用错误,不提供 raw 版
        // (与 EVMAccount::exists() 同一判据,直接复用 accountTableName 已解出的 tableName)。
        if (!co_await storage2::existsOne(
                m_storage.get(), executor_v1::StateKeyView(bcos::ledger::SYS_TABLES, tableName)))
            throw std::runtime_error(
                "Storage2Ledger::applyDiff: deleted_accounts entry not found in ledger (ghost "
                "delete, strict tripwire)");

        // 契约①:range 扫删账户表全部字段行 + 存量槽行——先收集键、range 结束后再逐个删除,
        // 避免边扫边删的迭代器失效风险;SYS_CODE_BINARY/SYS_CONTRACT_ABI 行永不触碰(内容寻址表,
        // 键空间在 tableName 之外,天然不落入本次 range 扫描,design §5 表格)。
        std::vector<std::string> fieldKeys;
        {
            auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
                executor_v1::StateKeyView{tableName, std::string_view{}});
            while (auto item = co_await iterator.next())
            {
                const auto& key = std::get<0>(*item);
                executor_v1::StateKeyView view(key);
                auto [table, fieldKey] = view.get();
                if (table != tableName)
                    break;
                fieldKeys.emplace_back(fieldKey);
            }
        }
        for (const auto& fieldKey : fieldKeys)
            co_await storage2::removeOne(
                m_storage.get(), executor_v1::StateKeyView{tableName, fieldKey});

        co_await storage2::removeOne(
            m_storage.get(), executor_v1::StateKeyView(bcos::ledger::SYS_TABLES, tableName));

        // 写穿(design §4.2):删除账户 ⇒ 三张缓存表对该地址全量失效/置负,含全部已缓存槽与
        // code——CREATE2 同址重生场景漏清即静默脏读。账户/code 直接写负缓存(与桥"nullopt 也
        // 缓存"的既有负缓存设计一致,不留给下次读触发额外 syncWait);槽缓存逐一擦除而非置零,
        // 让重生后未被本轮显式写入的槽自然落回冷读路径。
        m_accountCache.insert_or_assign(addr, std::nullopt);
        m_codeCache.insert_or_assign(addr, evmc::bytes{});
        std::erase_if(
            m_storageCache, [&addr](const auto& item) { return item.first.first == addr; });
    }

    /// Value-variant discrimination (design §6, Critical): storage2 logical deletion means
    /// range() merges do NOT filter tombstones — a raw range item's value is the full
    /// `storage2::StorageValueType<Value>` variant (NOT_EXISTS_TYPE/DELETED_TYPE/Value), and the
    /// traversal must skip the two tombstone alternatives itself, or a block-internal delete
    /// would resurrect into the state root. Returns the live content view, or nullopt for a
    /// tombstone (either alternative) — the caller never needs to distinguish "never existed"
    /// from "logically deleted", both mean "not part of this traversal".
    template <class RawValue>
    static std::optional<std::string_view> liveContent(const RawValue& rawValue)
    {
        return std::visit(
            bcos::overloaded{[](const storage2::NOT_EXISTS_TYPE&)
                                 -> std::optional<std::string_view> { return std::nullopt; },
                [](const storage2::DELETED_TYPE&) -> std::optional<std::string_view> {
                    return std::nullopt;
                },
                [](const auto& entry) -> std::optional<std::string_view> { return entry.get(); }},
            rawValue);
    }

    /// Whether fieldKey is one of the ACCOUNT_TABLE_FIELDS full set (design §6:
    /// CODE_HASH/CODE/BALANCE/ABI/NONCE/ALIVE/FROZEN/SHARD) — these rows are already read by
    /// fetchAccount (or, for CODE, intentionally never read — see the file header comment on why
    /// this bridge doesn't revive the legacy CODE field) and must not be misclassified as a
    /// 32-byte storage slot key during the account-table range scan.
    static bool isKnownAccountField(std::string_view fieldKey)
    {
        using Fields = bcos::ledger::ACCOUNT_TABLE_FIELDS;
        return fieldKey == Fields::CODE_HASH || fieldKey == Fields::CODE ||
               fieldKey == Fields::BALANCE || fieldKey == Fields::ABI ||
               fieldKey == Fields::NONCE || fieldKey == Fields::ALIVE ||
               fieldKey == Fields::FROZEN || fieldKey == Fields::SHARD;
    }

    /// 零值槽判据(终审批 9):以太坊语义下"槽值为 0 ≡ 该槽不存在",所以一行内容恰为 32 字节
    /// 全零的槽行,在**所有**判据上都必须等同于"该行不在"(fetchAllStorage 跳过、probeHasStorage
    /// 不因它判 has_storage=true)。
    /// 长度不为 32 的内容**不**在此判真:它不是合法槽值,把它降级成"零/不存在"就是终审 M-1 已
    /// 修过的那类静默降级。此处保守地判它"有内容"(probeHasStorage 因而给出 has_storage=true,
    /// EIP-7610 方向上偏保守 = 拒绝在其上 CREATE),真正的长度校验与 throw+毒旗留给权威读路
    /// (fetchStorage / fetchAllStorage)。
    static bool isZeroSlotValue(std::string_view content) noexcept
    {
        return content.size() == sizeof(evmc_bytes32::bytes) &&
               std::all_of(content.begin(), content.end(), [](char byte) { return byte == '\0'; });
    }

    /// Decodes the address embedded in a "/apps/<hex(addr)>" SYS_TABLES key, or nullopt when the
    /// table is **not** an account table at all (终审批 A · A-1).
    ///
    /// **判据不自造,直接复用主线 MPT 的分类器** `bcos::ledger::mpt::parseAccountTable`
    /// (`bcos-ledger/bcos-ledger/mpt/Classify.h:95-117`:前缀 `/apps/` + 后缀恰 40 字符 +
    /// 可解析为 hex,三条全过才算账户表,自带 no-throw 契约)。为什么必须是复用而不是照抄:
    ///   * `/apps/` 名字空间下**必然**存在非账户表——`/apps/<name>_accessAuth` 授权表
    ///     (`ContractAuthMgrPrecompiled.h:99-108` + `bcos-executor/src/Common.h:87`
    ///     `CONTRACT_SUFFIX = "_accessAuth"`)、BFS 链接表 `/apps/<contractName>/<version>`
    ///     (`BFSPrecompiled.cpp:663`)。本函数**曾经**对这些表逐行 unhex 并 throw,被
    ///     `visitAccounts` 接住置毒旗 ⇒ 生产账本上每个 OP 块 -32603 ⇒ op-node 反复重投 ⇒
    ///     节点在该块上永久卡死(活性故障,不自愈)。
    ///   * 主线 MPT 建根与本桥建根必须对"哪些 `/apps/` 表算账户"给出同一答案,否则同一本账本上
    ///     MPT root 与 OP stateRoot 结构性分歧。两处独立实现同一规则正是本仓 yParity 事故的成因;
    ///     共用同一个函数令这一类分歧不可能发生。
    ///
    /// 依赖形态:`Classify.h` 是 header-only(全 inline,只依赖 bcos-utilities),不需要链接
    /// `ledger` 目标,只需 `bcos-ledger/` 在 include path 上——`bcos-evm/test/CMakeLists.txt`
    /// 的 in-tree 门控内早已两者兼备(:128-131 include dir,:136 link)。
    static std::optional<evmc::address> addressFromTableName(std::string_view tableKey)
    {
        auto parsed = bcos::ledger::mpt::parseAccountTable(tableKey);
        if (!parsed.has_value())
            return std::nullopt;
        evmc::address addr{};
        static_assert(sizeof(addr.bytes) == bcos::Address::SIZE,
            "evmc::address and bcos::Address must both be 20 bytes for this memcpy");
        std::memcpy(addr.bytes, parsed->data(), sizeof(addr.bytes));
        return addr;
    }

    /// Full account-table scan for visitAccounts (design §6): classifies every live row under
    /// tableName as one of ACCOUNT_TABLE_FIELDS (skipped — read separately by fetchAccount) / a
    /// 32-byte raw storage slot key (collected into the returned map) / anything else (a
    /// storage2 layout invariant violation the bridge cannot interpret — throws, caught and
    /// poisoned by the public visitAccounts entry point). Two kinds of row are skipped before
    /// they can reach the returned map, and the two filters are cumulative, not alternatives:
    /// tombstoned rows (design §6 Critical, so a logically-deleted slot cannot resurrect) and
    /// zero-valued slot rows (final batch 9: zero ≡ the slot does not exist, matching what
    /// accountStorageRoot/opStorageRoot already do when building the trie).
    task::Task<std::map<evmc::bytes32, evmc::bytes32>> fetchAllStorage(std::string tableName) const
    {
        std::map<evmc::bytes32, evmc::bytes32> storage;
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            const auto& rawValue = std::get<1>(*item);
            executor_v1::StateKeyView keyView(key);
            auto [table, fieldKey] = keyView.get();
            if (table != tableName)
                break;

            if (isKnownAccountField(fieldKey))
                continue;

            auto content = liveContent(rawValue);
            if (!content.has_value())
                continue;  // 墓碑跳过(design §6 Critical)

            if (fieldKey.size() != kStorageSlotKeySize)
                throw std::runtime_error(
                    "Storage2Ledger::fetchAllStorage: unknown key in account table '" + tableName +
                    "' (neither a known ACCOUNT_TABLE_FIELDS name nor a 32-byte storage slot "
                    "key)");

            evmc::bytes32 slotKey{};
            std::memcpy(slotKey.bytes, fieldKey.data(), fieldKey.size());

            if (content->size() != sizeof(evmc_bytes32::bytes))
                throw std::length_error(
                    "Storage2Ledger::visitAccounts: storage slot value size mismatch in account "
                    "table '" +
                    tableName + "'");

            evmc::bytes32 slotValue{};
            std::memcpy(slotValue.bytes, content->data(), content->size());

            // 零值槽跳过(终审批 9):以太坊语义下**槽值为 0 等价于该槽不存在**(geth 的 trie 会
            // 删掉零值槽;本仓 accountStorageRoot/opStorageRoot 同样 is_zero → continue,零值槽本
            // 就不进 trie)。此处**曾经**是 throw+毒旗,理由是"applyDiff 的契约②从不写零值槽,
            // 真实值为零而仍落一行只能是写回路径有漏"——该理由只在桥自写的 E-b 世界成立,而这条
            // 读路同时要服务生产账本(HostContext.h:288 / Ledger.cpp:1844 的 setStorage 对零值照写
            // 不删,真实链账户表必然含零值槽行,创世 alloc 里一个 "0x00…00" 就是一行),留在读路
            // 的后果是 stateRootOf → visitAccounts → 这里每个 OP 块都毒旗 → 节点每块 -32603。
            // 写回泄漏的守护没有消失,它搬去了它真正的归属地:applyModifiedEntry 的删槽后置回读。
            if (evmc::is_zero(slotValue))
                continue;

            storage.emplace(slotKey, slotValue);
        }
        co_return storage;
    }

    /// visitAccounts implementation (design §6): range-scans SYS_TABLES for the /apps/ prefix
    /// (`/sys/` is a different prefix and is never scanned; a `c_systemTxsAddress` member that
    /// has an `/apps/<40hex>` table is an ordinary account here and is collected unconditionally
    /// — 终审批 A · A-2, see accountTableName for why that is the *required* behaviour and not a
    /// missing guard), skips non-account tables under /apps/ (A-1), skips tombstoned marker rows
    /// (Critical, same
    /// discrimination as fetchAllStorage), and for each surviving candidate delegates to
    /// fetchAccount/fetchAllStorage (which independently re-verify liveness through
    /// existsOne/readOne) before invoking the visitor.
    template <class Visitor>
    task::Task<bool> visitAccountsImpl(Visitor& visitor) const
    {
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{
                bcos::ledger::SYS_TABLES, bcos::ledger::SYS_DIRECTORY::USER_APPS});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            const auto& rawValue = std::get<1>(*item);
            executor_v1::StateKeyView keyView(key);
            auto [table, tableKey] = keyView.get();
            // 离开 /apps/ 前缀区间即停止(design §6:/sys/ 不扫)。
            if (table != bcos::ledger::SYS_TABLES ||
                !tableKey.starts_with(bcos::ledger::SYS_DIRECTORY::USER_APPS))
                break;

            // 值变体判别(design §6 Critical):跳过 NOT_EXISTS_TYPE/DELETED_TYPE 墓碑行,否则
            // 块内刚删除的账户还魂进 stateRoot。
            if (!liveContent(rawValue).has_value())
                continue;

            // 非账户表跳过(终审批 A · A-1):`/apps/` 名字空间里混着授权表 `_accessAuth` 与 BFS
            // 链接表 `<name>/<version>`,它们不是账户,不进 stateRoot。判据见
            // addressFromTableName 的注释(复用主线 `mpt::parseAccountTable`)。
            // **continue 而非 break**:这些表名与账户表名在 `/apps/` 前缀区间内交错排序,遇到一张
            // 就停会把它后面的真账户整片丢出 stateRoot。区间结束的判据只有上面那条前缀检查。
            const auto parsedAddr = addressFromTableName(tableKey);
            if (!parsedAddr.has_value())
                continue;

            const std::string tableName{tableKey};
            const auto addr = *parsedAddr;

            auto account = co_await fetchAccountForVisit(addr, tableName);
            if (!account.has_value())
                continue;  // 双重防御:与上面的墓碑判别同一判据(existsOne),理应不可达

            const auto storage = co_await fetchAllStorage(tableName);

            const AccountView accountView{.addr = addr,
                .nonce = account->nonce,
                .balance = account->balance,
                .codeHash = account->code_hash,
                .storage = storage,
                .m_bridge = this};
            if (!visitor(accountView))
                co_return false;
        }
        co_return true;
    }

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

    /// 账户表路径:**无条件** "/apps/" + hex_lower(addr)(feature_raw_address=off 前提,见文件头
    /// 的外部依赖小节)。判据与主线 MPT 的 `bcos::ledger::mpt::accountTableName`
    /// (`Classify.h:83-90`)逐字同形,与 A-1 复用的 `parseAccountTable` 严格互逆。
    ///
    /// **本函数曾经对 `c_systemTxsAddress` 集合返回 false,由调用方毒旗(终审批 A · A-2 已删)。**
    /// 那 8 个地址(0x1000 SYS_CONFIG / 0x1003 CONSENSUS / 0x1005 AUTH_MANAGER / 0x100b
    /// WORKING_SEALER_MGR / 0x1010 SHARDING / 0x10001 AUTH_COMMITTEE / 0x10003 ACCOUNT_MGR /
    /// 0x10004 ACCOUNT,`PrecompiledTypeDef.h:143-149`)在以太坊侧就是**普通地址**,而毒旗的
    /// 触发条件是"被 EVM 触及"而非"被写入状态"——BALANCE / EXTCODESIZE / CALL、乃至仅在
    /// access list 里提到,都会走到读路;evmone 的 `build_diff` 还把只读 touched 账户一并放进
    /// `modified_accounts`,写路守卫同样会被触发。于是一笔完全合法的交易就让节点 -32603,
    /// op-node 反复重投,节点在该块上永久卡死。
    ///
    /// 现语义(三个答案必须一致,且已一致):
    ///   * **读到什么** —— `/apps/<40hex>` 里的真实内容;表不存在即 `nullopt` = 以太坊语义的
    ///     空账户。与 op-geth 对同一地址的行为逐字一致。
    ///   * **写到哪** —— 同一张 `/apps/<40hex>`(见 applyModifiedEntry 用 `FromTableName` 直构
    ///     EVMAccount 的注释:必须绕开 EVMAccount 自己的 `/sys/` 路由,否则读写脑裂)。
    ///   * **进不进 stateRoot** —— 进,当且仅当 `/apps/<40hex>` 表存在,与任何其它账户同一条
    ///     规则。`visitAccounts` 因此**不需要**补守卫:它无条件收录,正是本语义所要求的。
    ///
    /// 为什么不能改成"系统地址恒为空账户":有人向 0x…1000 转账时余额会凭空消失,把一个响亮的
    /// -32603 换成不可发现的根分歧。而"进不进 stateRoot"的答案还必须与主线 MPT 一致——主线的
    /// `Classify.h` 正向/反向**都没有系统地址分支**,`/sys/` 因不带 `/apps/` 前缀而从不进 MPT。
    /// 也就是说主线 MPT 在同一本账本上已经把 `/apps/…1000` 当普通账户提交了;桥取任何别的语义,
    /// OP stateRoot 就会与主线 MPT root 分歧。
    ///
    /// FISCO 自己的系统合约控制面仍在 `/sys/<40hex>`,对 OP 执行世界完全不可见——两个键空间
    /// 互不重叠,不存在覆写风险(精编译地址也无法被部署占用)。
    static std::string accountTableName(const evmc::address& addr)
    {
        std::array<char, sizeof(addr.bytes) * 2> hex{};  // NOLINT
        boost::algorithm::hex_lower(
            std::string_view(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes)),
            hex.data());
        std::string_view hexView(hex.data(), hex.size());

        std::string tableName;
        tableName.reserve(bcos::ledger::SYS_DIRECTORY::USER_APPS.size() + hexView.size());
        tableName.append(bcos::ledger::SYS_DIRECTORY::USER_APPS);
        tableName.append(hexView);
        return tableName;
    }

    /// `computeHasStorage=false` skips the `probeHasStorage` range scan (final review B4-4). The
    /// KEEP contract for `has_storage` (present-but-empty is NOT nullopt — the bridge's core
    /// invariant) is untouched: this changes only WHETHER the field is computed, never what a
    /// computed value means. An Account produced with `false` therefore carries a `has_storage`
    /// that must not be published: only `fetchAccountForVisit` uses it, and it deliberately does
    /// not put such an Account into `m_accountCache`, so `get_account` can never observe one.
    task::Task<std::optional<Account>> fetchAccount(
        std::string tableName, bool computeHasStorage = true) const
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

        if (computeHasStorage)
        {
            account.has_storage = co_await probeHasStorage(tableName);
        }
        co_return account;
    }

    /// Account lookup for `visitAccountsImpl` (final review B4-4). Two savings over calling
    /// `fetchAccount` directly, both of which were pure waste:
    ///   * a cache HIT reuses the entry `get_account`/`applyModifiedEntry` already populated —
    ///     the cache is write-through (`applyModifiedEntry` refreshes it from the authoritative
    ///     re-read, `applyDeletedEntry` invalidates), so it is authoritative here too, and
    ///     bypassing it just re-read the same rows;
    ///   * a cache MISS skips `probeHasStorage`, because `has_storage` is not part of the
    ///     `AccountView` the visitor receives and is not read anywhere on the `stateRootOf` path.
    ///     That probe is a full range scan per account, i.e. it doubled the scans this path does.
    /// A miss result is deliberately NOT cached: it lacks `has_storage`, and `get_account` must
    /// never be served an Account with an uncomputed field.
    task::Task<std::optional<Account>> fetchAccountForVisit(
        const evmc::address& addr, const std::string& tableName) const
    {
        if (auto it = m_accountCache.find(addr); it != m_accountCache.end())
        {
            co_return it->second;
        }
        co_return co_await fetchAccount(tableName, /*computeHasStorage=*/false);
    }

    /// has_storage 判据(design §4.4):账户表 range seek 探测首个存活(非墓碑)、且值**非零**的
    /// 32 字节原始键(区别于 ACCOUNT_TABLE_FIELDS 的已知短字段名)。两层过滤都是修过的缺陷,
    /// **叠加**而非替代:
    ///   * 终审 I-1(墓碑层):range 扫描不判值变体会把逻辑删除的墓碑行(storage2::DELETED_TYPE)
    ///     当成活槽——"删至最后一个槽"后 has_storage 翻不回 false。用 liveContent() 过滤
    ///     (design §6 Critical),与 fetchAllStorage/visitAccountsImpl 同源。
    ///   * 终审批 9(零值层):一行**存活但值为零**的槽行,按以太坊语义等于该槽不存在,不得
    ///     判 has_storage=true。误真的后果不是性能而是共识:has_storage → state.cpp:259
    ///     `.has_initial_storage` → host.cpp:91 `is_create_collision`,同一个 CREATE2 会在
    ///     op-geth 成功而在本桥 INVALID(EIP-7610 误判)。而生产账本必然含零值槽行
    ///     (HostContext.h:288 / Ledger.cpp:1844 对零值照写不删)。
    /// 注意 KEEP 契约不受影响:"零值槽 = 槽不存在"与"账户存在但字段全默认 = 账户存在"是两件事,
    /// 后者仍由 fetchAccount 的 existsOne 判据保证返回 Account 而非 nullopt。
    task::Task<bool> probeHasStorage(std::string_view tableName) const
    {
        auto iterator = co_await storage2::range(m_storage.get(), storage2::RANGE_SEEK,
            executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            const auto& key = std::get<0>(*item);
            const auto& rawValue = std::get<1>(*item);
            executor_v1::StateKeyView view(key);
            auto [table, fieldKey] = view.get();
            if (table != tableName)
                co_return false;
            if (fieldKey.size() == kStorageSlotKeySize)
            {
                if (auto content = liveContent(rawValue);
                    content.has_value() && !isZeroSlotValue(*content))
                    co_return true;
            }
            // 已知字段名(codeHash/code/balance/abi/nonce/alive/frozen/shard)、已墓碑化的槽
            // (逻辑删除,liveContent 判负)、或值为全零的槽行(零值 ≡ 不存在)——继续找下一行,
            // 不能提前判定 has_storage=true。
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
            // 终审 M-1:槽值长度 != 32 字节此前静默返回全零槽,与"值不存在"混同,把存储层布局
            // 违规悄悄降级成了合法的零值。改为与 fetchAllStorage 一致的校验——throw,由
            // get_storage 的读路径 catch 并置毒旗(design §4.3),不让调用方把损坏数据当零值用。
            if (view.size() != sizeof(evmc_bytes32::bytes))
                throw std::length_error(
                    "Storage2Ledger::fetchStorage: storage slot value size mismatch in account "
                    "table '" +
                    tableName + "'");
            evmc::bytes32 value{};
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
