# 真账本 StateView 桥 + 双后端账本抽象 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 storage2 之上实现 `evmone::state::StateView` 真账本桥 + 自研内存账本,三腿回放(TestState/MemoryLedger/Storage2Ledger)33 向量全绿,解除 E-b park 的"真账本桥缺失"层。

**Architecture:** 方案 A:同步直读桥(`task::syncWait`)+ 块级读缓存(含负缓存)+ 毒旗错误通道;写回复用 `EVMAccount` 序列化(委托而非重实现);建根走 `visitAccounts` 遍历 + vendored MPT/rlp 原语;回放器抽取模板 harness 三腿实例化。

**Tech Stack:** C++20、GTest、storage2(`MemoryStorage`/`MultiLayerStorage`)、`bcos-framework::ledger::account::EVMAccount`、`bcos-task`(syncWait)、vendored evmone(StateView/StateDiff/MPT/rlp)。

**Spec:** `docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`(rev.2,42 项审查发现已折入——实现时以 spec 为唯一语义依据,本计划给结构与代码骨架)

## Global Constraints

- 命名空间 `bcos::evm::ledger`;目录 `bcos-evm/bcos-evm/ledger/`。
- `ports/` 零改动;`test/opstack/t8n/vectors/` 逐字节不动。
- 库目标纯净:gtest/nlohmann 不入库;`MemoryLedger.cpp` 入 `bcosevm::eth` 且不得引入 bcos-framework 依赖;`Storage2Ledger.h` 纯模板头。
- **条件编译边界(本计划新钉,Task 3 落地、Task 7 回填 spec)**:Storage2Ledger 相关测试与 E-b gate 仅在 `TARGET bcos-framework` 存在时编译(in-tree);standalone 构建交付 MemoryLedger + 其回放腿。
- E-b 前置(spec §2):`feature_raw_address=off`、向量无 `c_systemTxsAddress` 地址、单线程串行。
- 等价性口径:既有 TestState gate 行为不得变(33/33、比对计数不变);git 用 `rtk` 前缀;clang-format hook 归一化照 §4.6(a) 口径记录。
- 桥的全部语义细节(has_storage 判据、墓碑判别、缓存归属/唯一写者、毒旗点、SYS_CODE_BINARY 永不删、ensure-exists 无条件、零值槽毒旗适用域)以 spec rev.2 §4-§6 原文为准,本计划不复述即为引用。
- 环境变量:`export FB=/Users/octopus/octo/code/FISCO-BCOS`(worktree 内为 worktree 根)。

---

### Task 1: MemoryLedger + 单元测试

**Files:**
- Create: `bcos-evm/bcos-evm/ledger/MemoryLedger.h`、`MemoryLedger.cpp`
- Modify: `bcos-evm/CMakeLists.txt`(`bcos-evm-eth` 源列表 + `MemoryLedger.cpp`)
- Test: `bcos-evm/test/opstack/MemoryLedgerTest.cpp`(入 `bcos-evm-opstack-tests` 源列表)

**Interfaces:**
- Produces(后续任务依赖的精确签名):

```cpp
namespace bcos::evm::ledger {
struct LedgerAccount {
    uint64_t nonce{0};
    intx::uint256 balance;
    evmc::bytes code;
    std::map<evmc::bytes32, evmc::bytes32> storage;  // 不存零值(契约②)
};
class MemoryLedger final : public evmone::state::StateView {
public:
    std::optional<Account> get_account(const evmc::address&) const noexcept override;
    evmc::bytes get_account_code(const evmc::address&) const noexcept override;
    evmc::bytes32 get_storage(const evmc::address&, const evmc::bytes32&) const noexcept override;
    void applyDiff(const evmone::state::StateDiff&);  // strict 单形态,tripwire 内置(spec §5)
    bool poisoned() const noexcept { return false; }  // 抽象层统一查询(spec §6)
    // 访问者返回 false 即中止;visitAccounts noexcept 语义(内存后端天然不失败)
    // AccountView: {addr, nonce, balance, codeHash, codeGetter(惰性), storage(map 引用)}
    template <class Visitor> bool visitAccounts(Visitor&& v) const;
    std::map<evmc::address, LedgerAccount>& accounts();  // 测试/seed 直访
};
}
```

- [ ] **Step 1: 写失败测试**(`MemoryLedgerTest.cpp`,四组核心断言;`keccak256` 来自 `<bcos-evm/eth/state/hash_utils.hpp>`)

```cpp
#include <bcos-evm/ledger/MemoryLedger.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <gtest/gtest.h>
using namespace bcos::evm::ledger;
using namespace evmc::literals;

TEST(MemoryLedger, KeepContractEmptyAccountIsNotNullopt) {
    MemoryLedger l;
    l.accounts()[0x01_address];  // 存在但空
    auto acc = l.get_account(0x01_address);
    ASSERT_TRUE(acc.has_value());                       // KEEP:不折叠
    EXPECT_EQ(acc->nonce, 0u);
    EXPECT_EQ(acc->balance, intx::uint256{0});
    EXPECT_FALSE(acc->has_storage);
    EXPECT_FALSE(l.get_account(0x02_address).has_value());  // 不存在=nullopt
}
TEST(MemoryLedger, HasStorageIsDynamic) {
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.modified_accounts.push_back({0x01_address, 1, 1, std::nullopt,
        {{0x01_bytes32, 0x02_bytes32}}});
    l.applyDiff(d);
    EXPECT_TRUE(l.get_account(0x01_address)->has_storage);
    evmone::state::StateDiff d2;  // 契约②:写零=删槽 → has_storage 翻 false
    d2.modified_accounts.push_back({0x01_address, 1, 1, std::nullopt,
        {{0x01_bytes32, 0x00_bytes32}}});
    l.applyDiff(d2);
    EXPECT_FALSE(l.get_account(0x01_address)->has_storage);
}
TEST(MemoryLedger, ApplyDiffThreeContracts) {
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.modified_accounts.push_back({0x01_address, 5, 100,
        evmc::bytes{0x60, 0x00}, {{0x01_bytes32, 0x02_bytes32}}});
    l.applyDiff(d);
    EXPECT_EQ(l.get_account_code(0x01_address), (evmc::bytes{0x60, 0x00}));
    EXPECT_EQ(l.get_account(0x01_address)->code_hash,
        evmone::keccak256(evmc::bytes{0x60, 0x00}));
    evmone::state::StateDiff d3;  // 契约③:code 无值不覆写
    d3.modified_accounts.push_back({0x01_address, 6, 100, std::nullopt, {}});
    l.applyDiff(d3);
    EXPECT_EQ(l.get_account_code(0x01_address), (evmc::bytes{0x60, 0x00}));
    evmone::state::StateDiff d4;  // 契约①:删除
    d4.deleted_accounts.push_back(0x01_address);
    l.applyDiff(d4);
    EXPECT_FALSE(l.get_account(0x01_address).has_value());
}
TEST(MemoryLedger, StrictTripwireOnGhostDelete) {
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.deleted_accounts.push_back(0xdd_address);  // view 中不存在
    EXPECT_THROW(l.applyDiff(d), std::runtime_error);
}
TEST(MemoryLedger, EnsureExistsUnconditional) {  // spec §5:空 entry 也落账
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.modified_accounts.push_back({0x01_address, 0, 0, std::nullopt, {}});
    l.applyDiff(d);
    EXPECT_TRUE(l.get_account(0x01_address).has_value());
}
```

- [ ] **Step 2: 跑测试确认失败**——`cmake --build bcos-evm/build --target bcos-evm-opstack-tests -j8`,预期编译错误(头不存在)。
- [ ] **Step 3: 实现 `MemoryLedger.{h,cpp}`**。get_account:`find` 无→nullopt;有→`{nonce, balance, keccak256(code), !storage.empty()}`。applyDiff:deleted 先查后删(无则 throw,tripwire);每 modified entry `m_accounts[addr]`(无条件 ensure-exists)→ 赋 nonce/balance → code `has_value()` 才覆写 → 槽零值 `erase` 非零 `insert_or_assign`。visitAccounts 模板在头内:逐账户构造 AccountView(codeHash 现算,codeGetter 返回 `code` 引用),访问者返回 false 短路返回 false。
- [ ] **Step 4: CMake 接线 + 测试全绿**——`bcos-evm-eth` 源列表加 `bcos-evm/ledger/MemoryLedger.cpp`(FISCO 自有代码,不入告警抑制);测试源列表加 `MemoryLedgerTest.cpp`。跑 `ctest --test-dir bcos-evm/build -R BcosEvmOpstackTests --output-on-failure`,预期全 PASS 且既有 124 用例不回归。
- [ ] **Step 5: Commit**——`rtk git add bcos-evm && rtk git commit -m "feat(bcos-evm): MemoryLedger 自研内存账本(StateView+applyDiff+visitAccounts,KEEP/三契约/has_storage)"`

---

### Task 2: 回放器模板化 + MemoryLedger 回放腿

**Files:**
- Create: `bcos-evm/test/opstack/T8nReplayHarness.h`
- Modify: `bcos-evm/test/opstack/OpT8nReplayTest.cpp`(主体迁入 harness,行为不变)
- Test: 新增 `TEST(MemoryLedgerT8nReplay, Vectors)`(同文件或新文件 `MemoryLedgerT8nReplayTest.cpp`)

**Interfaces:**
- Consumes: Task 1 的 `MemoryLedger` 全部签名。
- Produces: `template <class Backend> void replayAllVectors(const char* legName)`;Backend 概念 =
  `{ using Ledger; static Ledger fromPre(const evmone::test::TestState& pre);
     static const evmone::state::StateView& view(Ledger&);
     static void apply(Ledger&, const evmone::state::StateDiff&);
     static evmone::hash256 stateRoot(const Ledger&);
     static void forEachPostAccount(const Ledger&, PostVisitor); }`。
  `TestStateBackend`(既有语义原样)与 `MemoryLedgerBackend` 两个实现。

- [ ] **Step 1: 抽取 harness**。把 `OpT8nReplayTest.cpp` 中 `TEST(OpT8nReplay, Vectors)` 的向量循环体(含 manifest/DIVERGENCES 装载、逐向量 JSON 解析、`processOpBlock`/`sealOpBlock` 调用、header/receipt/postState 比对、`RecordProperty` 汇总)整体移入 `T8nReplayHarness.h` 的 `replayAllVectors<Backend>`,四个后端耦合点(现 `:493 applyStateDiffStrict(ts,d)`、`:499 processOpBlock(ts,…)`、`:524/553 stateRootOf(ts)`、postState 遍历)改经 Backend 静态方法。**JSON loader 不动**:`pre` 仍先解析成 `evmone::test::TestState`,再交 `Backend::fromPre`(TestStateBackend 直接返回;其他后端由此播种——即 `seedFromTestState` 的雏形,本任务先为 MemoryLedger 写一个 map 拷贝版,Task 4 的 `LedgerSeed.h` 泛化它)。
- [ ] **Step 2: TestState 腿等价性验证(硬门)**。`bcos-evm/build/test/bcos-evm-opstack-tests --gtest_filter='OpT8nReplay.Vectors'`,预期:33/33、`known_diverges=0`、比对计数与重构前**逐字节相同**(重构前先跑一次记录基线值)。任何偏差=重构引入行为变化,修复后才能继续。
- [ ] **Step 3: MemoryLedger 腿**。`TEST(MemoryLedgerT8nReplay, Vectors) { replayAllVectors<MemoryLedgerBackend>("memory-ledger"); }`,其中 stateRoot 暂时经"导出为 TestState 再调既有 `stateRootOf`"过渡(Task 5 换正式遍历建根后删除过渡代码——本任务在过渡函数处留 `// Task 5 移除` 标记,Task 5 的 Files 列表含此清理)。
- [ ] **Step 4: 跑三套并 Commit**。`ctest -R BcosEvmOpstackTests` 全绿(既有 + MemoryLedger 腿 33/33)。`rtk git commit -m "test(bcos-evm): t8n 回放器模板化,MemoryLedger 33 向量腿全绿(TestState 腿等价性锁定)"`

---

### Task 3: Storage2Ledger 读桥 + 单元测试

**Files:**
- Create: `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`
- Create: `bcos-evm/test/opstack/support/CountingStorage.h`(读写计数装饰器)、`ThrowingStorage.h`(异常注入装饰器)
- Modify: `bcos-evm/CMakeLists.txt` + `bcos-evm/test/CMakeLists.txt`(条件编译块)
- Delete: `bcos-evm/bcos-evm/adapter/StateViewAdapter.h`(占位契约注释迁 `Storage2Ledger.h` 头注)
- Test: `bcos-evm/test/opstack/Storage2LedgerTest.cpp`

**Interfaces:**
- Consumes: `bcos-framework/ledger/EVMAccount.h`(`exists/create/balance/setBalance/nonce/setNonce/code/setCode/codeHash/storage/setStorage/path`,全协程)、`bcos-framework/storage2/Storage.h`(`readOne/writeOne/removeOne/existsOne/range`)、`bcos-task/Wait.h`(`task::syncWait`)、`LedgerTypeDef.h`(`SYS_TABLES/SYS_CODE_BINARY/ACCOUNT_TABLE_FIELDS/USER_APPS`)。
- Produces:

```cpp
namespace bcos::evm::ledger {
template <class Storage>
class Storage2Ledger final : public evmone::state::StateView {
public:
    explicit Storage2Ledger(Storage& storage);   // 一块一实例,无 reset(spec §4.2)
    std::optional<Account> get_account(const evmc::address&) const noexcept override;
    evmc::bytes get_account_code(const evmc::address&) const noexcept override;
    evmc::bytes32 get_storage(const evmc::address&, const evmc::bytes32&) const noexcept override;
    bool poisoned() const noexcept;
    std::string_view firstError() const noexcept;
    void applyDiff(const evmone::state::StateDiff&);          // Task 4
    template <class Visitor> bool visitAccounts(Visitor&&) const;  // Task 5
};
}
```

- [ ] **Step 1: CMake 条件编译块**。`bcos-evm/test/CMakeLists.txt` 追加:

```cmake
# Storage2Ledger 桥测试仅在 in-tree(bcos-framework 目标存在)时编译;
# standalone 构建交付 MemoryLedger 部分(plan Global Constraints,spec 回填见 Task 7)。
if(TARGET bcos-framework)
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/Storage2LedgerTest.cpp)
    target_link_libraries(bcos-evm-opstack-tests PRIVATE bcos-framework)
endif()
```

先确认根构建里 `bcos-framework` 在 `bcos-evm` 之前 `add_subdirectory`(`rg -n "add_subdirectory" CMakeLists.txt | head -20`);若目标名不同(如带命名空间),以实际为准并记入报告。

- [ ] **Step 2: 写失败测试**(核心六组;fixture 用单层 `MemoryStorage`,仿 `transaction-executor/tests/TestMemoryStorage.h:8-9` 的 `MemoryStorage<StateKey, StateValue, ORDERED>`):

```cpp
// (a) 存在性:EVMAccount::create() 后 has_value;未 create 的地址 nullopt
// (b) 空账户归一化:create() 但零字段 → Account{0,0,keccak(空),has_storage=false}
// (c) 往返读:EVMAccount 写 balance=7/nonce="5"/setCode → 桥读逐字段相等
// (d) has_storage:EVMAccount::setStorage 一槽 → true
// (e) 毒旗:Storage2Ledger<ThrowingStorage> 读 → 返回 nullopt/零值,poisoned()==true,
//     firstError() 非空;新实例 poisoned()==false(实例隔离)
// (f) nonce 溢出:EVMAccount::setNonce("18446744073709551616")(2^64)→ 桥读 poisoned()
// (g) 负缓存:Storage2Ledger<CountingStorage> 连读两次不存在地址,
//     第二次 storage 调用计数零增量
```

每组用 `task::syncWait` 驱动 EVMAccount 写入;账户构造 `EVMAccount(storage, addr, /*binaryAddress=*/false)`(E-b 前置 raw_address=off)。

- [ ] **Step 3: 实现读桥**。三方法模板:查缓存→`try { task::syncWait(...) } catch(...) { poison; return 安全值; }`→归一化→回填。get_account 流程:系统地址集(`c_systemTxsAddress`)命中→毒旗;`existsOne(StateKeyView(SYS_TABLES, "/apps/"+hex))` 无→缓存 nullopt;有→读 BALANCE/NONCE/CODE_HASH 字段 + has_storage 探测(账户表 range seek 首个 32 字节键)→nonce 字符串转 u256 显式 `> UINT64_MAX` 检查→毒旗。get_account_code:经 CODE_HASH→`SYS_CODE_BINARY` 读。get_storage:`readOne(StateKeyView(表, slot 32 字节))`,缺省零值,含零值回填缓存。三张 mutable 缓存 + mutable 毒旗;头注写明:单线程契约、唯一写者不变式、禁协程上下文调用、KEEP 契约原文(自 StateViewAdapter.h 迁入)。
- [ ] **Step 4: 跑绿 + Commit**。`ctest -R BcosEvmOpstackTests` 全绿。`rtk git add -A bcos-evm && rtk git commit -m "feat(bcos-evm): Storage2Ledger 读桥(syncWait+三缓存+毒旗+has_storage+存在性判据)"`

---

### Task 4: applyDiff 写回 + LedgerSeed + 写穿/删除失效测试

**Files:**
- Modify: `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(applyDiff)
- Create: `bcos-evm/bcos-evm/ledger/LedgerSeed.h`
- Test: `bcos-evm/test/opstack/Storage2LedgerTest.cpp` 追加

**Interfaces:**
- Produces: `Storage2Ledger::applyDiff(const StateDiff&)`(strict 单形态);
  `template <class Ledger> void seedFromTestState(Ledger&, const evmone::test::TestState&)`(`LedgerSeed.h`;MemoryLedger 与 Storage2Ledger 通用,Task 2 的临时版删除并改用此实现)。

- [ ] **Step 1: 写失败测试**:

```cpp
// (h) 写穿-负缓存:桥读不存在地址(种负缓存)→ applyDiff 建该账户 → 立即再读 has_value
// (i) 写穿-正缓存:桥读槽值(种正缓存)→ applyDiff 覆写 → 再读得新值;删槽(写零)→ 再读零且
//     has_storage 翻 false
// (j) 删除三表失效(CREATE2 同址重生):建账户+槽+code → 桥读全部(种满三缓存)→
//     applyDiff 删除 → applyDiff 同址重建不同 code/槽 → 再读全部得新值,无陈旧残留
// (k) 往返-写向:桥 applyDiff 写 balance/nonce/code/槽 → EVMAccount 读逐字段相等
//     (abi 豁免:不断言 abi()——spec §5)
// (l) SYS_CODE_BINARY 不删:两账户同 code → 删其一 → 另一 get_account_code 仍完整
// (m) 空账户播种:seedFromTestState(pre 含空账户)→ 桥 get_account(空账户).has_value()
// (n) strict tripwire:deleted 项底层不存在 → throw
```

- [ ] **Step 2: 实现 applyDiff**。逐 modified entry:构造 `ledger::account::EVMAccount<Storage>`,`syncWait`:`exists()` 假则 `create()`(**无条件 ensure-exists,不因空 entry 跳过**)→`setBalance`/`setNonce(十进制串)`→code `has_value()` 时 `setCode(code, /*abi=*/"", keccak256(code))`(**桥不写 abi 内容**;`setCode` 若强制写 ABI 空行照 EVMAccount 行为即可,判据以往返测试豁免 abi 为准)→槽:零值 `storage2::removeOne` / 非零 `setStorage`。deleted:先 `exists()` 校验(假则 throw,tripwire)→删 SYS_TABLES 行→删字段行→range 扫删全部槽行。**每步写穿缓存**(spec §4.2:删除⇒三表按地址全失效置负)。`LedgerSeed.h`:遍历 TestState map 合成 `StateDiff::Entry`(code 空也显式 `optional{空}`?否——空 code 账户 `code=std::nullopt`,契约③不写即为空;槽逐对入 modified_storage)→ `applyDiff`。Task 2 的 MemoryLedger 临时播种删除、`MemoryLedgerBackend::fromPre` 改调 `seedFromTestState`。
- [ ] **Step 3: 跑绿(含回放回归)+ Commit**。`ctest -R BcosEvmOpstackTests` 全绿(MemoryLedger 腿走新 seed 后仍 33/33)。`rtk git commit -m "feat(bcos-evm): Storage2Ledger applyDiff 写回(EVMAccount 委托+写穿失效)+ LedgerSeed 统一播种"`

---

### Task 5: visitAccounts + 遍历建根 + 三后端同根

**Files:**
- Modify: `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(visitAccounts)、`bcos-evm/bcos-evm/adapter/StateRootCompute.h`(泛型重载)、`T8nReplayHarness.h`(MemoryLedger 腿 stateRoot 换正式实现,删 Task 2 过渡代码)
- Test: `Storage2LedgerTest.cpp` 追加 + `bcos-evm/test/opstack/LedgerRootTest.cpp`

**Interfaces:**
- Produces: `template <class Ledger> evmone::hash256 bcos::evm::stateRootOf(const Ledger&)`
  (约束:Ledger 有 `visitAccounts`;账户叶 `rlp::encode_tuple(nonce, balance, storageRoot, codeHash)`,键 `keccak256(addr)`,存储树同 `opStorageRoot` 先例——对齐 vendored `mpt_hash.cpp:27-36`);既有 `stateRootOf(TestState&)` 标 `[[deprecated]]`。

- [ ] **Step 1: 写失败测试**:

```cpp
// (o) 三后端同根:同一状态(含带码带槽/空账户/多槽)在 TestState/MemoryLedger/
//     Storage2Ledger 构造 → 三根逐字节相等(TestState 用既有 stateRootOf 对照)
// (p) 墓碑跳过:Storage2Ledger 建 2 账户 → 块内 applyDiff 删其一 →
//     visitAccounts 只见存活者;建根 == 单账户 MemoryLedger 根
// (q) 键分类:手工 writeOne 注入 ALIVE/FROZEN/SHARD/ABI 字段行 → visitAccounts
//     不误判为槽、不误报毒旗;注入 17 字节未知键 → poisoned()
// (r) 大规模:1 账户 1024 槽 + 32KB code → visitAccounts 槽计数 1024、
//     建根 == 同数据 MemoryLedger 根、applyDiff 删除后遍历零残留
// (s) 建根逐字段矩阵:基线根 vs 分别篡改 nonce/balance/code(→codeHash)/单槽后的根,
//     四例均不等于基线(常驻 gtest,spec §7 探针 4)
```

- [ ] **Step 2: 实现**。Storage2Ledger::visitAccounts:`range` seek `SYS_TABLES` 前缀 `/apps/`(值变体判别:DELETED/NOT_EXISTS 跳过)→逐账户表:读字段(nonce/balance/CODE_HASH,code 惰性 getter 走 SYS_CODE_BINARY)→槽遍历 `range` 该表(跳过 `ACCOUNT_TABLE_FIELDS` 全集字段名、跳过墓碑、32 字节键=槽、实值零计数→毒旗(适用域注释)、其余→毒旗);访问者 false 或毒旗置位→短路 return false。`stateRootOf` 泛型:两层 MPT 组装(存储树复用 `opStorageRoot` 的逻辑,提为共用 inline 或直接调用)。harness 的 MemoryLedgerBackend/Storage2 backend `stateRoot` 均改此实现。
- [ ] **Step 3: 跑绿 + Commit**。全套 `ctest -R BcosEvmOpstackTests`;`rtk git commit -m "feat(bcos-evm): visitAccounts 遍历(墓碑/键分类/毒旗)+ 泛型 stateRootOf,三后端同根"`

---

### Task 6: E-b gate + 接线完整性

**Files:**
- Create: `bcos-evm/test/opstack/EbT8nReplayTest.cpp`(条件编译块内)
- Test: 同文件

**Interfaces:**
- Consumes: `replayAllVectors<Backend>`、`Storage2Ledger`、`LedgerSeed`、`CountingStorage`、
  `MultiLayerStorage` fixture(仿 `transaction-scheduler/tests/testMultiLayerStorage.cpp:20-37`:
  `MemoryStorage<StateKey,StateValue,ORDERED|LOGICAL_DELETION>` 可变层 + `ORDERED|CONCURRENT` 后端)。

- [ ] **Step 1: 实现 Storage2Backend 与测试**。每向量:`multiLayerStorage.fork()` → `newMutable()` → `CountingStorage` 包住 view → `Storage2Ledger` 实例 → `seedFromTestState` → 回放 → 断言 `!ledger.poisoned()`(毒旗消费方契约)→ 向量末 `RecordProperty("storage2_reads", …)`/`("storage2_writes", …)` 并 `ASSERT_GT(reads, 0)`(接线完整性常驻判据,spec §7 探针 5)。失败弃 view(天然回滚),不 pushView。
- [ ] **Step 2: 跑 E-b gate**。`bcos-evm/build/test/bcos-evm-opstack-tests --gtest_filter='EbT8nReplay*'`,预期 33/33、`known_diverges=0`、每向量比对计数>0。**任何向量翻红按 DIVERGENCES 三选一纪律停下归因,禁止改向量;若归因指向桥语义,回 spec 对应节修实现**。
- [ ] **Step 3: Commit**。`rtk git commit -m "test(bcos-evm): E-b gate——33 向量真桥(storage2)回放全绿,storage2 调用计数常驻断言"`

---

### Task 7: 五探针留痕仪式 + 文档收尾 + 全量验收

**Files:**
- Modify: `docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`(§2/§10 回填条件编译边界与实施实况)、`bcos-evm/README.md`(ledger/ 目录与 E-b 状态)、`docs/superpowers/specs/2026-07-27-eth-utils-removal-c-route-todo.md`(step 1 状态更新)
- Create: `.superpowers/sdd/probe-ledger-bridge-report.md`(探针留痕)

- [ ] **Step 1: 五探针逐个执行并留痕**(spec §7 清单:毒旗/KEEP/空账户播种/建根矩阵(已常驻,记录一次输出)/接线完整性(假后端批量翻红))。每个:注入 diff → 跑对应测试记录翻红输出 → 回退 → 复绿输出,全部写入留痕文件。KEEP 探针注入点:临时把 Storage2Ledger 空账户返回改 nullopt → 预期消毒误删向量翻红;接线探针:E-b Backend 临时换 MemoryLedger → 预期 storage2_reads 断言批量翻红。
- [ ] **Step 2: 验收清单全跑**(spec §8 逐项,含基线清单 N0 对照、`ports/` diff 空、库纯净 `rg -l "nlohmann|gtest" bcos-evm/bcos-evm/ | rg -v statetest.hpp` 空)。
- [ ] **Step 3: 文档回填**:spec 补"条件编译边界(standalone 不含 Storage2 腿)"与实测数字;README 补 ledger/ 一节 + E-b 状态(措辞守 §10 边界:解除"真账本桥缺失"层,不宣称生产可用,Karst 独立缺口);C 路线清单 step 1 标注(账本已交付,15 测试迁移状态如实)。
- [ ] **Step 4: Commit**——`rtk git commit -m "docs(bcos-evm): E-b 真桥里程碑收尾——探针留痕/验收清单/spec-README 回填"`

---

### Task 8(可裁剪,spec §3 末位任务): 15 个存量测试迁 MemoryLedger

**Files:**
- Modify: `bcos-evm/test/opstack/` 下 15 个用 `TestState` 的测试文件(清单见 spec 审查记录:OpDepositTest/OpZeroDiffTest/OpPredeploysTest/OpBlockFinalizeTest/OpHostTest/OpBlockSealTest/OpTransitionTest/Op7702Test/OpFeeParamsTest/OpBlockExecuteTest/OpStateDiffSanitizeTest/OpBlockHarnessTest/OpT8nReplayTest/OpFloorGasTest/OpValidateTest)

**替换规则**(机械,逐文件):`evmone::test::TestState ts` → `MemoryLedger l`;map 直访 `ts[addr].xxx` → `l.accounts()[addr].xxx`(LedgerAccount 字段名同构:nonce/balance/code/storage);`applyStateDiffStrict(ts, d)` → `l.applyDiff(d)`;`stateRootOf(ts)` → 泛型 `stateRootOf(l)`。每迁 3-5 个文件跑一次全套(`ctest -R BcosEvmOpstackTests`),红了即回看该文件对 TestState 特有 API 的依赖并在报告中记录。全部完成后:C 路线 step 1 全额清账,`OpT8nReplayTest` 的 TestStateBackend 保留(旧 gate 对照腿不动)。

- [ ] 分批迁移 + 每批全绿 + Commit(每批一笔:`rtk git commit -m "test(bcos-evm): 存量测试迁 MemoryLedger(batch N/M)"`)
- [ ] **裁剪出口**:若时间/风险不允许,跳过本任务,在 spec §10 与 C 路线清单如实记账"迁移债务延后",不影响 E-b 验收(Task 1-7 已完备)。

---

## 验收清单(spec §8 同口径)

- [ ] 三腿回放各 33/33、`known_diverges=0`、每向量比对计数>0
- [ ] 基线 N0 用例全过;新增用例名单如实记录
- [ ] 专项单测(a)-(s) 全绿;往返(abi 豁免)、三后端同根绿
- [ ] 五探针翻红并复绿,留痕在案;storage2 计数常驻断言生效
- [ ] `ports/` 零改动;库目标纯净;standalone 构建(MemoryLedger 部分)不回归
