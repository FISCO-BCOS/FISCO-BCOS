# evmone 0.21.0 → 0.24.0 升级评估

> 评估基线:分支 `karst-on-318-merged` @ `5f0bc54d0`(本 worktree:`/root/wt-evmone-024`,分支 `eval/evmone-0.24.0`)
> 评估日期:2026-10-11。上游源码证据:`ipsilon/evmone` v0.21.0 与 v0.24.0 官方 release tarball 逐文件 diff。

## TL;DR

**技术上可行,无根本性障碍;工作量中大型(约 1.5–2.5 人周,含行为差异排查则 2–4 人周)。**
跨三个 minor 版本,evmc ABI 从 12 连跳到 19(0.22→13、0.23→18、0.24→19),三次破坏性变更叠加是主要成本。

除了获得 Amsterdam 支持(EIP-7778/7843/7708/7976/7981/8024/8037/8038/7954/8246)之外,**升级有明确的安全动机**:0.23.0 修复了 EIP-7702 授权签名从未被验证的漏洞,而当前 vendored `eth/state`(基于 0.21 的 test/state)的代码注释明确承认只做 "partial verification"(见 §5)。FISCO 的 Isthmus/Karst fork 映射到 EVMC_PRAGUE/EVMC_OSAKA,7702 语义已激活,该修复对本项目是真实需要的。

最高风险不是编译错误,而是一个**静默错位陷阱**:两处按位置聚合初始化的 `evmc_host_interface` fnTable,在 ABI 18 把 `get_nonce` 插到 `get_balance` 之后时会整体错位——编译通过、运行时才崩(§3.2 P0)。

另有三个**行为变化点**(不是纯 API 适配)需要产品决策:Osaka(Karst)起取消 1024 调用深度上限、7702 授权开始真实验签(历史链上依赖"不验签"行为的 delegation 会开始被拒绝)、blob fee 溢出修复(§4)。

---

## 1. 当前集成方式(基线盘点)

| 组件 | 位置 | 版本 | 说明 |
|---|---|---|---|
| evmone port | `ports/evmone/`(overlay) | 0.21.0, port-version 10 | `vcpkg_from_github` + tarball 源码包 |
| SM3 补丁 | `ports/evmone/fisco-sm3.patch` | 230 行 / 9 文件 | `evmone::VM::hash_fn` 钩子 + macOS 静态库合并 + noexcept/RTTI 例外构建 |
| evmc port | `ports/evmc/` | 0.21.0 vendored 头 | header-only,由 evmone tarball 派生 |
| intx / blst port | `ports/intx/`、`ports/blst/` | 0.15.0 / 20240520 | blst 供消费者独立使用;evmone 自身另经 ExternalProject 内嵌构建 blst v0.3.15 |
| vendored state 机 | `bcos-evm/bcos-evm/eth/state/`(22 文件) | 基于 0.21 `test/state` 拷贝 | 本地分歧:chain_id 显式参数、自研建根、SM3、系统合约 |
| test/utils 消费 | port 安装 `test/utils/{rlp,rlp_encode,test_state,stdx}` 源码 | bcos-evm 直接编译 | port 对 `test_state.cpp` 有一处 `vcpkg_replace_string`(chain_id) |
| precompile 内部头 | port 安装 14 个 `lib/evmone_precompiles/*.hpp` + `pairing/` | bcos-evm 直接 include | |

消费方模块:`bcos-evm`(eth lane + opstack lane)、`bcos-executor`、`transaction-executor`、`ethereum-executor`(EthereumHost)、`bcos-framework`(仅 evmc 头)、`engine`、`opstack-executor`(测试)。

## 2. 上游变更摘要(0.21.0 → 0.24.0)

| 版本 | 主题 | evmc ABI | 对本项目的关键项 |
|---|---|---|---|
| 0.22.0 "Amsterdam begins" | Amsterdam 开始实现(SLOTNUM 0x49、calldata floor、SWAPN/DUPN/EXCHANGE 等) | **13** | `evmc_tx_context` 布局变;EOFCREATE call kind 与 initcodes 从 API 移除;`evmc_result` optional storage 移除;内嵌 blst → 0.3.16 |
| 0.23.0 "Signatures verified" | EIP-7708(ETH 转账发日志)、7954(代码尺寸上限)、8246(去 SELFDESTRUCT 销毁) | **18** | **Host 接口新增 `get_nonce`;CREATE 地址改由 VM 计算并经 `msg.recipient` 传递;`create2_salt`/`evmc_result::create_address` 删除;Constantinople revision 删除(枚举重编号);capabilities API 删除;`evmc_access_status : bool`;EIP-7702 验签安全修复** |
| 0.24.0 "Second dimension" | EIP-8037/8038 state gas(第二 gas 维度);Amsterdam state 访问重定价 | **19** | `evmc_message::state_gas`/`evmc_result::state_gas` 新增;**Osaka 起不再检查 1024 深度上限**;`evmone::evmmax` 并入 precompiles;CMake 最低 3.25;内嵌 blst → 0.3.17;libsecp256k1 v0.8.0(仅测试工具链) |

其他不变的利好:intx 仍为 0.15.0(无需动);`test/utils/stdx` 逐字节未变;`EVMONE_TESTING/TOOLS` 选项仍在(port 的 OFF 配置继续有效);blst 获取机制仍是 ExternalProject 自建(0.21 同款,仅 URL 版本号变)。

## 3. 影响面分层分析

### 3.1 Port 层(工作量:小,约 0.5–1 天)

- **REF/SHA512 bump**:`vcpkg_from_github` REF → `v0.24.0`,重新计算 SHA512;`vcpkg.json` 与 `evmoneConfigVersion.cmake` 版本号改 0.24.0。
- **SM3 补丁 rebase**(逐 hunk 核对结果):

| 补丁文件 | 0.24 上游变化 | rebase 难度 |
|---|---|---|
| `lib/evmone/vm.hpp` | 零变化 | 直接套用 |
| `cmake/LibraryTools.cmake` | 零变化 | 直接套用 |
| `lib/evmone/advanced_execution.hpp` | 零变化 | 直接套用 |
| `lib/evmone/advanced_execution.cpp` | 仅结果构造改 `make_execution_result` | 直接套用(补丁 hunk 上下文未触及) |
| `lib/evmone/instructions.hpp` | 全文件 278 行 churn,但 **keccak256 指令体(L404–421)结构未变** | 可套用,建议 fuzz 验证 |
| `lib/evmone/baseline_execution.cpp` | 上下文 `analysis.raw_code()` → `code` | **1 处上下文手工 rebase**(纯机械) |
| `lib/evmone/execution_state.hpp` | 新增 `state_gas` 字段与初始化 | 补丁 hunk(reset() 内 hash_fn 清零)上下文仍在,可套用 |
| `lib/evmone/CMakeLists.txt`(noexcept/RTTI) | 22 行 diff | 小 rebase |
| 根 `CMakeLists.txt`(头文件 DIRECTORY 安装) | 69 行 diff | 小 rebase |

  补丁的 `DIRECTORY install lib/evmone/*.hpp` 顺带会安装 0.24 新增的 `create_address.hpp`、`state_gas.hpp`——正好是 vendored host.hpp 新 include 所需,无需额外动作。
- **`test_state.cpp` 的 `vcpkg_replace_string`**:目标串 `get<state::TransactionProperties>(tx_props_or_error))` 在 0.24(L84)逐字存在,继续有效。
- **test/utils 安装闭包**:5 个文件全部仍存在,churn 为 rlp.hpp 26 行 / rlp_encode.cpp 4 行 / test_state.cpp 19 行 / test_state.hpp 8 行,stdx/ 零变化。闭包无需扩(新增文件都与本闭包无关)。
- **precompiles 头安装列表**:14 个文件在 0.24 全部保留,只新增 `kzg_precomputed_lines.{cpp,hpp}`、`modarith.hpp`(可选加入安装列表;若 bcos-evm 未 include 则不必)。
- **evmc port**:同步安装 0.24 vendored 头;`tooling.hpp` 已从上游删除(本仓库无引用)。
- **blst port**:`ports/blst` 20240520 是消费者依赖,与 evmone 内嵌 blst(0.3.15→0.3.17)是两条线;建议一并 bump 以避免双版本共存,但非硬性。
- **CMake 3.25 最低要求**:根工程 3.28、bcos-evm 3.25,满足。
- **libsecp256k1 / nlohmann_json**:仅在 `EVMONE_TESTING=ON`/tools 路径使用,port 保持 OFF,不引入新依赖。

### 3.2 evmc ABI 19 宿主/消息面(工作量:中;风险:最高)

**P0 — fnTable 位置初始化静默错位(必须最先做,且必须改为 designated initializer)**
- `evmc_host_interface` 在 ABI 18 把 `get_nonce` 插到 `get_balance` 之后(第 5 位)。
- 本仓库两处**按位置**初始化的 fnTable:
  - `bcos-executor/src/vm/EVMHostInterface.cpp:309`
  - `transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h:247`
- 升级后这两处**编译通过**,但 `getBalance` 落进 `get_nonce` 槽位……逐位后移,`set_transient_storage` 变 nullptr——运行时才崩,且崩溃形态是函数指针类型错乱,极难排查。修法:补 `getNonce` 回调并顺手改为 designated initializer(`{.account_exists = ..., .get_nonce = ...}`),一劳永逸免疫后续字段插入。

**P0 — `get_nonce` 缺失即崩**:0.24 的 CREATE 指令(`instructions_calls.cpp:290`)每次 CREATE 都调 `state.host.get_nonce(sender)`(为计算创建地址)。需要实现的共 4 处:
1. vendored `eth/state/host.cpp` 的 `evmone::state::Host`(OpHost 继承它,一处实现全链生效);
2. `ethereum-executor/EthereumHost.h:157`(`: public evmc::Host`,不实现则编译失败——这是好事,编译器逼你修);
3. 上述两个 fnTable;
4. 测试内 mock(上游 `mocked_host.hpp` 随 evmc 头更新自动获得)。

**P1 — CREATE 地址流(ABI 18 语义变化)**
- `create2_salt` 删除(5 处:`OpTransition.cpp:66`、`eth/state/state.cpp:99`、`system_contracts.cpp:80`、`host.cpp:248`、`EthTransitionTest.cpp:449`);
- `compute_create_address` 从 vendored host 移到 `<evmone/create_address.hpp>`,命名空间 `evmone::`(不再 `evmone::state::`):`OpTransition.cpp:169`、`OpDepositTest.cpp:262,814`、vendored `host.hpp:24`;
- `host.cpp:294` `result.create_address = msg.recipient;` 与 `precompiles.cpp:873` 聚合初始化注释需按新结果结构改写(`evmc::Result` 的 create_address 构造函数已删除,新增 `StateGas` 参数)。

**P2 — 清理项(机械删除)**
- `host.cpp:219,332` 的 `assert(msg.kind != EVMC_EOFCREATE)`(枚举值已删);
- `host.cpp:421-422` 的 `initcodes`/`initcodes_count` 填充(字段已删)。

**已核实无影响(不需要动的)**
- `evmc_capabilities`/`has_capability`:全仓库无使用;
- `EVMC_LATEST_STABLE_REVISION`(Cancun→Osaka):无使用;
- revision 枚举重编号(Constantinople 删除,其后全部 -1):所有比较均为符号化(`rev >= EVMC_ISTANBUL` 等),SYS_CONFIG 持久化走 `evmcRevisionFromName`(按名字),无 `EVMC_CONSTANTINOPLE` 直接引用——**但注意**:若有任何旧链把 revision 以数字落盘,升级后会静默错读,上线前需确认历史数据格式(评估结论:当前代码按名字存,风险低);
- `evmc::VM{evmc_create_evmone()}` 用法(测试 20+ 处):wrapper 构造不变;
- SM3 钩子消费方(`bcos-executor/src/vm/VMInstance.cpp:48,82`、`transaction-executor/.../VMInstance.cpp:27`、`HashFnRoutingTest`):`vm.hpp` 上游零变化,补丁继续提供 `hash_fn`,不受影响。

### 3.3 Vendored `eth/state` rebase(工作量:最大)

本地分歧 vs 上游 churn(±行数,基于 0.21 三方对比):

| 文件 | 本地分歧 | 上游 0.21→0.24 churn | 说明 |
|---|---|---|---|
| `state.cpp` | 134 | 328 | 本地:chain_id 参数、建根、SM;上游:Amsterdam 全家 + state gas + CREATE recipient 化 |
| `host.cpp` | 22 | 320 | 上游:get_nonce、recipient、call 重构 |
| `state.hpp` | 39 | 50 | 本地:transition 签名(chain_id) |
| `transaction.hpp` | 0 | 58 | 上游变更,直接快进 |
| `errors.hpp` | 0 | 132 | 直接快进 |
| `account.hpp` | 0 | 32 | 直接快进 |
| 其余 15 文件 | ≤9 | ≤17 | 基本直接快进 |

- 建议 rebase 方向:**以 0.24 的 `test/state` 为底,重放本地分歧**(而不是把上游 diff 打到本地拷贝上)。本地分歧小而集中(chain_id、自研建根 `StateRootCompute`、SM3 hash 注入、系统合约),可枚举重放。
- 上游新增文件需一并纳入:`authorization.{cpp,hpp}`(7702 验签,安全修复本体)、`precompiles_libsecp256k1.*`/`precompiles_gmp.*`(测试工具路径用,eth lane 生产的 `precompiles.cpp` 路径不依赖,可按需取舍)、`rlp_decode.*`、`account.hpp`。
- **Amsterdam 特性在本仓库全部休眠**:fork schedule 最高只映射到 EVMC_OSAKA(Karst),没有 rung 映射 EVMC_AMSTERDAM。因此 3.3 的工作以"编译适配 + 保持现行为为默认"为主,Amsterdam 语义天然不激活(直到未来某 OP fork 需要它)。
- 关联测试:`EthTransitionTest.cpp`(14 处 revision 引用)、`OpKarstTest/OpOsakaSemanticsTest/OpHostTest/OpForkScheduleTest` 及 `engine` 侧 parity 测试。

### 3.4 行为变化点(需要产品/协议决策,非纯适配)

1. **Osaka(Karst)起取消 1024 深度上限**(EIP-7825 + 63/64 规则):0.24 `instructions_calls.cpp:217` `if (state.rev < EVMC_OSAKA && depth >= 1024)`。FISCO Karst 即 EVMC_OSAKA → 升级后超深调用不再被 `evmc_max_depth` 拒绝。需检查 `OpOsakaSemanticsTest`/`OpKarstTest` 是否断言旧行为,并确认这是否与 FISCO 对 Karst 的 fork 语义定义一致(注:FISCO 已在 OpTransition 层自行实现 7825 的 per-tx gas cap,深度语义需与之对齐)。
2. **EIP-7702 授权开始真实验签**(0.23 安全修复):见 §5。若历史链上有依赖"不验签"被接受的 delegation 交易,升级后将按规则拒绝——这是修复而非回归,但需在变更日志中明示。
3. **blob fee 可负担性检查溢出修复**(0.24):`max_fee_per_blob_gas` 巨值可绕过检查。对本仓库 opstack blob 费用路径(Ecotone+)是低概率正确性修复。

## 5. 安全动机(为什么值得升)

0.21 vendored `test/state/state.cpp` 的 `process_authorization_list` 中,上游原文注释:

```
// TODO: We actually only do "partial" verification by assuming the signature is valid
//   when the test has the signer specified.
```

即 7702 authorization list 的 ecrecover 从未真正执行,"任何地址都可以被授予 delegation 指定"(上游 0.23 release note 原话)。0.24 将验签移入 `authorization.cpp`,经 `evmone::crypto::secp256k1` 严格模式做 ecdsa recover,并修复了 chain-id > 255、v > 0xff 等边界。

影响面判断:FISCO 的 Isthmus→Karst 映射 EVMC_PRAGUE/EVMC_OSAKA,7702 语义在该 fork 窗口激活;bcos-evm eth lane 的 `state::transition` 即此代码。**前提待确认**:生产交易的 authorization list 是否可由外部提交并走此路径(若是,这是必须升级/必须回补修复的理由;若 eth lane 仅用于内部结算不可达,则降级为"建议跟进")。除此之外 0.23 还带 EIP-7708/7954/8246(Amsterdam,休眠),0.24 带 state gas(Amsterdam,休眠)。

## 6. 风险登记册

| # | 级别 | 风险 | 缓解 |
|---|---|---|---|
| R1 | P0 | fnTable 位置初始化静默错位(§3.2) | 改 designated initializer + 补 getNonce;升级 PR 里 grep 全仓库 `evmc_host_interface` 聚合初始化清零 |
| R2 | P0 | get_nonce 缺失 → CREATE 空指针崩溃 | 4 处实现点清单化;编译期靠 evmc::Host 纯虚兜底(fnTable 除外,靠 R1 的 designated 改造) |
| R3 | P1 | Karst(Osaka)深度上限语义变化 | 升级前后跑 OpOsaka/OpKarst 语义回归;与 OpTransition 已实现的 7825 对账 |
| R4 | P1 | 7702 验签启用后的历史行为差异 | 确认生产可达性;变更日志明示;fixture 回归 |
| R5 | P1 | vendored state rebase 冲突(state.cpp/host.cpp) | 以 0.24 为底重放本地分歧;本地分歧先整理成显式清单 |
| R6 | P2 | 双 blst 版本(内嵌 0.3.17 vs port 20240520) | 同步 bump `ports/blst` |
| R7 | P2 | macOS 构建(补丁内 libtool/noexcept/RTTI 部分)在新 CMake 下的适配 | CI mac job 验证;补丁 hunk 已核对仅小 rebase |
| R8 | P2 | ethereum-tests fixture 版本落后于新 state 机 | 可选跟进 `ports-data/ethereum-tests`(上游用 tests@v21);非硬依赖 |

## 7. 建议实施顺序与估算

1. **阶段 1 — port rebase**(0.5–1 天):REF/SHA512/版本号;SM3 补丁按 §3.1 表 rebase;evmc port 同步;`ports/blst` bump。验收:fullnode 依赖树完整安装,bcos-framework 仅用 evmc 头可编译。
2. **阶段 2 — 宿主接口适配**(1–2 天):R1+R2(4 处 get_nonce + designated 化);create2_salt/EOFCREATE/initcodes/create_address 清理(§3.2 P1/P2)。验收:全仓库编译零错误(编译器在此阶段兜底大部分遗漏)。
3. **阶段 3 — vendored eth/state rebase**(2–4 天):以 0.24 test/state 为底重放本地分歧;纳入 authorization.cpp;快进 15 个低分歧文件。验收:bcos-evm 单测绿。
4. **阶段 4 — 行为与回归**(2–3 天):R3/R4 语义对账;ethereum-tests fixture 跑通;engine parity 测试;e2e(c2-e2e)。
5. **阶段 5(可选)**:预研 Amsterdam(EVMC_AMSTERDAM)在 fork schedule 中的挂载点,为 Karst 之后的 OP fork 铺路。

合计:理想路径 ~1.5–2.5 人周;含 R3/R4 行为对账与 fixture 升级 2–4 人周。

## 8. 结论

- **建议升级**,直接跳 0.24.0(不必停在中间版;0.22/0.23 的破坏会在 0.24 一次性吸收)。
- 核心收益:7702 安全修复(条件见 §5)、Osaka 语义对齐(7825 深度、blob fee 修复)、Amsterdam 就绪、持续跟进上游(0.21 已落后三个 minor)。
- 最大单点风险是 R1(fnTable 静默错位),但它同时是最便宜的修复(designated initializer)。
- 本 worktree(`eval/evmone-0.24.0`)可直接作为实施分支;阶段 1 的 port 改动可以在此分支上开始。
