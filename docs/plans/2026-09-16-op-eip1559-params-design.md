# 链级 EIP-1559 参数通道（P0）设计

状态：待评审
日期：2026-09-16
范围决定：窄（参数通道 + 引擎级验证闭环）+ lane × 键 矩阵最小版 + C2 声明三元组

---

## 0. TL;DR

FISCO 的 OP lane 把链的 EIP-1559 三参数（`elasticity` / `denominator` /
`denominator_canyon`）硬编码成 OP 主网值 `6/50/250`，且 `config.genesis` 里没有任何
字段可以携带链自己的值——**参数在结构上无法到达引擎**。

后果的准确边界（不可笼统说成"pre-Holocene 全错"）：

- **pre-Canyon 窗口必然错**：FISCO 取 Bedrock 常量 50，链里（devnet/C2）是 8 → 每个
  `gasUsed != gasLimit/elasticity` 的区块期望 base fee 都不同，`engine_newPayload`
  会把真链的合法区块判为 `Invalid`。
- **Canyon ~ Holocene 窗口**：FISCO 取 Canyon 常量 250/6。只有链的
  `denominator_canyon != 250` 或 `elasticity != 6` 时才错。devnet 的
  `denominator_canyon/elasticity` 是 250/6（`devnet.toml:43-44`），这段恰好一致；C2 的
  `elasticity` 是 **2**（`intent.toml`），按此段会错，但 C2 是 Jovian-at-genesis、没有
  这段窗口，所以未暴露——本设计的 encode 兜底测试用 `{2,8,250}` 正是为了覆盖这个组合
  （§5 第 4 行）。
- **Holocene 及以后**：`denominator`/`elasticity` 来自父块 extraData
  （`OpBaseFee.h:217-280` 对 `eip1559.go:77-79`），链参数不参与 → 不受本缺陷影响。
  这也解释了 C2 e2e（Jovian-at-genesis）为什么全绿。

本设计的修法：新增 `[op_eip1559]` 链级配置节，值经 `GenesisConfig` → `Initializer`
引导期直传进引擎（值语义，不落存储、不进 `OpForkSchedule`），四个消费点改为显式取用；
缺省保持当前三元组且**不改变既有链的创世引脚**；同时建立 lane × 键 矩阵最小版与三模式
golden 引脚测试，把"哪个键属于哪条 lane、是否进引脚"从散点校验变成可执行表。

---

## 1. 问题

### 1.1 缺陷位置

| 侧 | 事实 | 证据 |
|---|---|---|
| FISCO | 三参数硬编码 | `bcos-framework/bcos-framework/engine/OpBaseFee.h:43-47`；encode 兜底同值在 `engine/bcos-engine/EngineServiceCommon.cpp:294-299` |
| op-geth | 从链配置读，且决定每个后代区块的 base fee | `params/config.go:1349-1368`（`BaseFeeChangeDenominator`/`ElasticityMultiplier` 读 `config.Optimism`）、`consensus/misc/eip1559/eip1559.go:97`（`parentGasTarget = parent.GasLimit / elasticity`） |
| 真实链 | 不是 50 | 语料 devnet `tools/devnet/devnet.toml:42` = 8；C2 `setup_c2.sh` 的 `intent.toml` = 8 / elasticity 2 |
| 后果 | 拒真块 | `engine/bcos-engine/OpEngineService.inl:1054-1061`：newPayload 用算出的期望 baseFee 比对，不等返回 `Invalid` |

### 1.2 复现（已提交）

用例 `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp` →
`OpNewPayloadRpcE2eSuite/PreCanyonBaseFeeIgnoresTheChainsEip1559Denominator`（提交
`dd8ea180d`）。构造：Regolith 窗口（pre-Canyon + pre-Holocene）、父块
`number 0 / time 0 / gasLimit 30M / gasUsed 20M / baseFee 1e9 / extraData 空`、子块
时间 1s。观测到两件事：

- 建块侧：FISCO 产出 `baseFeePerGas = 1_060_000_000`（= op-geth 在 denominator 50
  下的值），而 denom-8 链需要 `1_375_000_000`；
- 校验侧：把带 `1_375_000_000` 的块喂进 `newPayload` → `Invalid`，
  `baseFeePerGas does not match the value computed from the parent`。

### 1.3 黄金值（op-geth pin `e8800cffe` 实跑 `eip1559.CalcBaseFee`，非手算）

父块同上，子块时间 1（pre-Canyon），`elasticity = 6`、`denominatorCanyon = 250`：

| 链 `EIP1559Denominator` | op-geth `CalcBaseFee` |
|---|---|
| 8 | **1_375_000_000** |
| 50 | 1_060_000_000 |
| 250 | 1_012_000_000 |
| `gasUsed == gasTarget`，denominator 8 或 50 | 1_000_000_000（步长为零，denominator 不可观测） |

最后一行是"为什么一直没人发现"的机理。生成方式：一个两行 `go.mod` 的临时模块，
`replace github.com/ethereum/go-ethereum => <op-geth 检出>`，直接调
`eip1559.CalcBaseFee`。

### 1.4 为什么此前所有关卡都没拦住

| 关卡 | 为什么瞎 | 证据 |
|---|---|---|
| 引擎 e2e fixture | 父块 `gasUsed` 固定为 `gasLimit / 6`，正好等于 gasTarget → 步长为 0 | `opstack-executor/tests/support/OpEngineE2eFixture.h:212` |
| t8n 回放 | 结构上不比较 base fee：`currentBaseFee` 是输入，`_op_expected.header` 无该字段 | `OpT8nReplayTest.cpp:588`；`_op_expected.header` = `gasUsed/receiptsRoot/logsBloom/stateRoot` |
| 语料生成器 | 与实现同错：也写死 50 | harness `opstack-executor/tests/t8n/generator/cases.go:820` |
| 真实链快照 | 窗口之外：400 块全是 `_info.hardfork=jovian`，pre-Canyon 未入快照 | `devnet_1875-2274_c38db356.json` |

---

## 2. 范围

**做**：

1. 新增链级三参数通道（`[op_eip1559]` → 引擎四个消费点）；
2. lane × 键 矩阵最小版 + 三模式 golden 引脚测试；
3. 引擎级验证闭环：翻转复现用例、控制组、`OpBaseFee` 黄金值单测、配置与引脚单测；
4. C2 e2e 的 `config.genesis` 同步声明三元组 `8/2/250`；
5. `gen_official_genesis.py` 从 registry toml `[optimism]` 生成该节。

**不做**（见 §6）：RPC 的 pre-Holocene 缺口、Eth lane 硬编码、driver gas limit、
EL fork 时间显式化、corpus generator 硬编码同步、真栈 pre-Canyon devnet。

---

## 3. 设计

### 3.1 值类型

新文件 `bcos-framework/bcos-framework/engine/OpEip1559Params.h`：

```cpp
struct OpEip1559Params
{
    std::uint64_t elasticity;
    std::uint64_t denominator;
    std::uint64_t denominatorCanyon;
};
```

只依赖 `<cstdint>`，因此 `bcos-framework/ledger/GenesisConfig.h`（ledger）与
`OpBaseFee.h`（engine）都能 include 而不成环。**不复用 `OpForkSchedule`**：那个类型
负责"何时激活"，这个类型负责"如何定价"，职责不同；schedule 的 codec 与链上元数据
（`ChainMetadata.h`）保持不动。

### 3.2 配置面

```ini
[op_eip1559]
    elasticity         = 8
    denominator        = 8
    denominator_canyon = 250
```

规则：

| 规则 | 内容 | 理由 |
|---|---|---|
| 必需键 | 节存在时 `elasticity` / `denominator` 必填 | 与 `denominator` 二者构成步长，缺一不可 |
| `denominator_canyon` | 可选，缺省 250 | op-deployer 的 standard 预设值与它生成的每条链一致（`op-deployer/pkg/deployer/standard/standard.go:32`）；避免强迫一条 Canyon 永不激活的链编造数值 |
| 值域 | 三者都必须 > 0 | op-geth 在 nil/0 上 panic（`params/config.go:1352-1355`），panic 不是模型 → 配置加载期以带字段名的 `InvalidConfig` 拒 |
| lane 绑定 | 节存在但 `executor.version < 3`（非 OP lane）→ 拒 | 与既有 `[op_fork_timestamps]` 的双向绑定风格一致 |
| 缺省 | 节缺席 = `6/50/250`，且**不向创世引脚输出任何东西** | 既有链的引脚逐字节不变 |
| 进入引脚的 | **生效值**，不是"是否配置过" | 显式写 `250` 与不写等价，避免同一条链出现两个引脚 |

校验落点：`NodeConfig::validateL2Invariants`（`bcos-tool/bcos-tool/NodeConfig.cpp:497`）
新增一组绑定，同时在 `chain_fork` 附近补"节存在 ↔ OP lane"一条；矩阵最小版建立后，
这条改由 §3.5 的表驱动。

### 3.3 贯通路径

```
config.genesis [op_eip1559]
  → NodeConfig::loadOpEip1559                     新函数，排在 loadOpForkTimestamps 之后
  → GenesisConfig::m_opEip1559 (std::optional)    模式同 m_opForkSchedule
  → generateGenesisData                           仅当有值时 emit（照抄 txGasPrice/evmRevision 先例）
  → Initializer::init                             libinitializer/Initializer.cpp:~712
       生效三元组 = effectiveOpEip1559(genesisConfig)
  → EngineServiceInitializer::buildOp(..., eip1559)   新增尾随默认实参
  → OpEngineService 构造器新增尾随默认实参，持有不可变副本
```

**生效值只有一处计算**：`effectiveOpEip1559()`（放 `OpEip1559Params.h`，或作为
`GenesisConfig` 的成员函数）负责 `value_or(6/50/250)` 与 `denominator_canyon` 的 250
缺省落地；`generateGenesisData`（写引脚）与 `Initializer`（喂引擎）**都调它**，不允许
两处各自补默认值——否则"节缺席时引脚无键、引擎却用缺省值"这类不一致会无法被测试发现。

`OpEngineService`（`engine/bcos-engine/OpEngineService.h:147-159`）与
`buildOp`（`libinitializer/EngineServiceInitializer.h:90-98`）都新增**尾随默认实参**，
因此既有调用点（含 149 个 e2e 用例的 fixture）不改也能编译；fixture 只为新用例显式传入。

### 3.4 四个消费点

| # | 位置 | 改法 |
|---|---|---|
| ① | `OpEngineService.inl:433`（FCU 建块） | 经 `baseFeeClockFor()` 把参数作为显式实参传入 |
| ② | `OpEngineService.inl:1055`（newPayload 校验） | 同上 |
| ③ | `OpBaseFee.h:307-339` `calcOpNextBlockBaseFee` 的 pre-Holocene 分支 | `denominator = clock.newBlockIsCanyon ? p.denominatorCanyon : p.denominator`；`elasticity = p.elasticity` |
| ④ | `EngineServiceCommon.cpp:294-299` encode 兜底 | 由 `c_eip1559DenominatorCanyon/c_eip1559ElasticityCanyon` 改为 `p.denominatorCanyon/p.elasticity` |

`OpBaseFee.h:43-47` 三个常量**降级为缺省值常量**（重命名 `c_default*`），生产路径不再
直接取用。③ 的 Holocene+ 分支不变：那里 denominator/elasticity 来自父块 extraData
（与 op-geth `eip1559.go:77-79` 一致），链参数只在 pre-Holocene 生效。

④ 是唯一的行为变化点：从"永远 250/6"变成"该链的 Canyon 对"。对现有 devnet/C2
（250/6）是 no-op，对自定义链才修正，因此单独加一条测试而不是让它静默变化。

### 3.5 lane × 键 矩阵（最小版）

现状：模式判定有四路信号（`executor.version` ∈ `ExecutorLane{Baseline=1, Ethereum=2,
Opstack=3}`（`LedgerConfig.h:288-296`）、`feature_l2_ethereum_compat`、
`[ethereum] mode=el`、`[op_fork_schedule]`/`[op_fork_timestamps]` 的存在），校验散在
五处：`validateL2Invariants`（6 对手工绑定，`NodeConfig.cpp:497-628`）、
`validateELModeInvariants`（2 对，`:636-658`）、`LedgerInitializer.cpp:34-58`、
`validateOpModeGenesisOnly`（`transaction-scheduler/BaselineSchedulerMPTHelpers.h:172`）、
`Initializer.cpp:621-692`。**四个地方在回答"这是哪条 lane"**，新增一个模式相关参数要
在多个文件补 if——这正是本次参数无处安放的根因。

最小版（不做完整表驱动重构）：

新文件 `bcos-tool/bcos-tool/ChainLaneConfig.h`：

```cpp
enum class ChainLane { Fisco, Eth, Op };          // 由 executor.version 判定，唯一权威
enum class KeyPresence { Required, Forbidden, Optional };
struct LaneKeyRule
{
    std::string_view section;    // 节名
    ChainLane lane;
    KeyPresence presence;
    bool pinned;                 // 是否必须进 generateGenesisData
    std::string_view reason;     // 反查依据（源码位置或测试名）
};
```

- 判别式取 `executor.version`（已创世冻结、已映射 scheduler 槽位、已有枚举与
  `static_assert`）；其余三路信号降级为"被矩阵校验的派生断言"。
- 先只迁入 **OP 节族**三行（`[op_fork_schedule]`/`[op_fork_timestamps]`/`[op_eip1559]`），
  其余 8 对保留原样并在本文件头注释里列 TODO，避免一次性重构。
- 遍历校验函数替代那 1 对新增 if。
- 表里**暂列"需确认"的格子**（见 §7）不得凭推断填，必须逐格反查现有校验/测试后填实。

矩阵（初稿，`需确认` 处待反查）：

| 键 | FISCO native (v1) | Eth lane (v2) | OP lane (v3) |
|---|---|---|---|
| `[eth_genesis_header]` | 禁止 | 需确认 | 必需 |
| `[alloc.*]` | 禁止 | 需确认 | 必需 |
| `[fork_timestamps]` | 禁止 | 必需 | 禁止 |
| `[op_fork_schedule]` / `[op_fork_timestamps]` | 禁止 | 禁止 | 必需（二者互斥、至少一个） |
| `executor.evm_revision` | 可选 | 可选 | 禁止 |
| `[op_eip1559]`（本次新增） | 禁止 | 禁止 | 可选（缺省 = 6/50/250） |

已从代码反查确认的绑定（作为矩阵的锚点）：
`[eth_genesis_header]` ↔ `feature_l2_ethereum_compat`（双向，`:523-536`）、
`[alloc.*]` ↔ 同一 feature（双向，`:507-518`）、
`[fork_timestamps]` ↔ `[ethereum] mode=el`（双向，`:542-553`）、
`mode=el` genesis → `[web3] chain_id` 非零（`:561-590`）、
OP schedule ↔ `executor.version >= 3`（双向，`:599-614`）、
OP lane 禁 `executor.evm_revision`（`:620-627`）、
config.ini `ethereum.mode=el` ↔ genesis 声明（双向，`:636-658`）、
OP lane 仅创世激活（`BaselineSchedulerMPTHelpers.h:172`、
`LedgerInitializer.cpp:51-58`）。

### 3.6 创世引脚规则（可执行化）

判据（写入本文件并作为后续评审依据）：**两个节点配置不同会让同一高度得到不同结果 →
必须进 `config.genesis` 且进 `generateGenesisData`；只影响本节点资源/连通性 →
`config.ini`。**

机械守卫：每条 lane 一份 golden 引脚字符串测试（三份）。任何新键漏进引脚或误进引脚
都当场失败。这是唯一能防住"新参数根本没进引脚"这类回归的手段——P0 的形态正是如此
（三参数既不在配置里也不在引脚里，所以没有任何测试会失败）。

### 3.7 工具链

- `tools/opstack-genesis/gen_official_genesis.py`：从 registry toml 的 `[optimism]`
  （`eip1559_elasticity` / `eip1559_denominator` / `eip1559_denominator_canyon`）
  生成 `[op_eip1559]` 节，写入 `genesis.ini`。这样 FISCO 的 ini 与
  `gen_rollup_config.py` 已产出的 `chain_op_config` 来自**同一个源**，EL 与 CL 不可能
  各说各话。
- `setup_c2.sh` 的 `config.genesis` heredoc 增加该节，值 `8/2/250`（与它的
  `intent.toml` 一致）。C2 是 Jovian-at-genesis，常量路径不可达，所以对 base fee 是
  no-op；价值在于：(a) 让真栈 e2e 也走一遍新解析/校验/引脚路径；(b) 它是该链的**正确
  声明**——C2 的 `elasticity` 是 2，与缺省 6 不同（§1）。(c) 需要留意的一处：消费点 ④ 在
  `{2,8,250}` 下会编码 `(250,2)` 而非旧的 `(250,6)`，会改变 header 的 extraData。
  该路径只在 op-node 送来全零 `eip1559Params` 时触发，而 C2 已按"根因 F"把 L1
  SystemConfig 的 `eip1559Params` 设为 8/2，所以不触发；C2 e2e 全绿即是这一点的回归证明，
  若它变红则说明 ④ 被触发了，属于真实信号而非噪声。只改引脚不改 stateRoot，且 C2 每次
  从空目录重建，无迁移问题。

---

## 4. 兼容与迁移

1. **既有链不受影响**：节缺席 → 引脚不含新键、行为仍是 `6/50/250`。
2. **硬约束（必须写进对外说明）**：该参数是创世冻结的。给一条**已初始化**的链补上这
   节会改变创世引脚，`buildGenesisBlock` 的重启守卫会拒绝启动
   （`bcos-ledger/bcos-ledger/Ledger.cpp:1835-1883`）。这是**正确**的——op-geth 里它
   就是 chain config，改动等于硬分叉——但意味着**本修复只对新链生效，不能让一条出生
   时参数就写错的既有链就地改正**。
3. Lane 矩阵的判别式换成 `executor.version` 不改变任何现有接受/拒绝行为：矩阵初稿只
   表达现状，迁入的只有 OP 节族三行。

---

## 5. 验证矩阵

| # | 层 | 内容 | 期望 |
|---|---|---|---|
| 1 | 引擎 e2e（翻转） | `OpE2eFixture` 增加可选参数（默认 legacy，149 个既有用例不变）；新用例传 `{6,8,250}` | 建块 `baseFeePerGas == 1_375_000_000`；`newPayload` 对 denom-8 块返回 `Valid`；保留 `!= 1_060_000_000` 反向守卫 |
| 2 | 引擎 e2e（控制组） | 同父块把 `gasUsed` 改成 `gasLimit/6` | 两套参数都返回父块 base fee（1_000_000_000），证明"步长为零所以看不见" |
| 3 | `OpBaseFee` 单测 | `{6,8,250}` 与 `{6,50,250}` 对同一父块 | 分别等于 1_375_000_000 / 1_060_000_000 |
| 4 | encode 兜底单测 | ④ 的零参数兜底，用 `{elasticity 2, denominator 8, denominator_canyon 250}`（= C2 真实值） | 编码出 `(250, 2)`。**不能用 `{6,8,250}`**：它的 Canyon 对正好等于旧的固定值 250/6，测不出变化 |
| 5 | 配置解析单测 | 缺 key / 零值 / 非 OP lane 带该节 / 节缺席 = unset | 各自命中带字段名的 `InvalidConfig`，或解析为 unset |
| 6 | 创世引脚单测 | 三模式 golden 引脚（native / eth / op）+ 节存在与否 | 节缺席与旧链逐字节一致；节存在含生效值；`denominator_canyon` 缺省 → 250 |
| 7 | ledger 单测 | 加了该节后 B0 的 `stateRoot` / `hash` | 均不变（引脚只进 `SYS_CONFIG/eth_genesis_data`，不在 L2 的 RLP 里） |
| 8 | 回归 | 编译 + UT（`opstack-executor-block-tests` 149、`test-bcos-engine`、bcos-tool/ledger 各套）+ C2 e2e | 全绿 |

黄金值一律来自 op-geth pin `e8800cffe` 实跑（§1.3），生成配方写进用例注释，不手算。

---

## 6. 不做（记录，作为后续项）

1. **RPC 的 pre-Holocene 缺口**：`bcos-rpc/bcos-rpc/web3jsonrpc/utils/FeeHistory.cpp:185-204`
   在父块 extraData 为空时直接返回父块 base fee，不套步长（注释说明该路径没有 fork
   schedule，只能靠 header 形状）。修它需要给 `bcos-rpc` 补一个 fork 源，是另一条贯通
   路径；本设计的值语义类型正是为这一步铺路。
2. **Eth lane 同类硬编码**：`FeeHistory.cpp:44-45` 的 `elasticity = 2` / `denominator = 8`。
3. **driver gas limit 默认 30M**：`OpBaseFee.h:345`，devnet 链是 60M；自建链会静默用 30M。
4. **genesis config 的 EL fork 时间解耦**：`shanghai/cancun/pragueTime` 目前隐式耦合到
   canyon/ecotone/isthmus。
5. **corpus generator 的 `EIP1559Denominator: 50` 硬编码**（harness 仓
   `cases.go:820`）：需在其仓内同步，且注意 t8n 回放结构上测不到本参数（§1.4）。
6. **真栈 pre-Canyon devnet 端到端复现**：需把 C2/opdevnet 工具链扩成能表达
   pre-Canyon 窗口（`[op_fork_schedule] canonical=0:regolith,…`），并让 denom=8 同时到
   op-geth 与 op-node，再把 FISCO 当 EL 接 op-node。扩展配方见会话记录。

---

## 7. 开放项

1. 矩阵的两格 `需确认`（`[eth_genesis_header]` 与 `[alloc.*]` 在 Eth lane 的必需性）：
   必须逐格反查现有校验与测试后填实，不得推断。
2. 矩阵余下 8 对绑定迁入表驱动的时间点（本次只迁 OP 节族三行）。
3. 对外说明的措辞：如何向运维表达"这条链的参数是创世冻结的，写错只能重新建链"。

---

## 8. 交付物

| 项 | 路径 |
|---|---|
| 值类型 | `bcos-framework/bcos-framework/engine/OpEip1559Params.h`（新） |
| 配置解析 | `bcos-tool/bcos-tool/NodeConfig.{h,cpp}`（`loadOpEip1559` + 校验 + 引脚） |
| 数据结构 | `bcos-framework/bcos-framework/ledger/GenesisConfig.h`（`m_opEip1559`） |
| 引擎 | `bcos-framework/bcos-framework/engine/OpBaseFee.h`、`engine/bcos-engine/{OpEngineService.h,.inl,EngineServiceCommon.cpp}` |
| 注入 | `libinitializer/{EngineServiceInitializer.h,Initializer.cpp}` |
| lane 矩阵 | `bcos-tool/bcos-tool/ChainLaneConfig.h`（新） |
| 工具链 | `tools/opstack-genesis/gen_official_genesis.py`；`tools/op-e2e/setup_c2.sh`（harness 仓） |
| 测试 | `opstack-executor/tests/{OpNewPayloadRpcE2eTest.cpp,support/OpEngineE2eFixture.h}`、`bcos-tool/test/unittests/libtool/`、`bcos-ledger/test/unittests/ledger/` |
