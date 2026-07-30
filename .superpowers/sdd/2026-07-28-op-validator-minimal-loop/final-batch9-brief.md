# 终审批 9(rev.2):账本桥零值槽语义统一

工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`。
**不豁免编译与测试。** 回归基线:in-tree 239/239、standalone 131/131、`test-bcos-engine` 11 例、E-b 桥三腿 33×3。

> **rev.2 说明**:rev.1 假设这是"`probeHasStorage` 少一个 `is_zero`,三行可修"。控制器亲自做完调研后**范围变了**——见下。rev.1 的"若零值槽会进 trie 则停下等裁定"这一条**已由控制器回答:不会进 trie**,你不需要再查这一问。

---

## 已查明的事实(控制器亲自核对,你不需要重新验证)

**两处判据互相矛盾,方向相反,以太坊正解是两处都不对。**

| 位置 | 对零值槽的处理 | 生产账本上的后果 |
|---|---|---|
| `Storage2Ledger.h:485-494` `fetchAllStorage` | **查零值 → `throw`** | throw 被 `visitAccounts:250-265` 接住 → `poison()` → 遍历中止、产物全部作废。而 `stateRootOf` 正走 `visitAccounts → visitAccountsImpl:536 → fetchAllStorage` → **每个 OP 块都 -32603,节点完全不可用** |
| `Storage2Ledger.h:682` `probeHasStorage` | **不查零值 → 返回 `true`** | `has_storage` 误真 → `eth/state/state.cpp:259 .has_initial_storage` → `eth/state/host.cpp:88 is_create_collision` → **同一 CREATE2 在 op-geth 成功、在本桥 INVALID**(EIP-7610 误判) |

以太坊语义:**槽值为 0 等价于该槽不存在**(geth 的 trie 会删掉零值槽)。所以正确行为是"跳过",既不是 throw,也不是当成有内容。

**关键上下文**:`:486-489` 的注释**逐字预言过这件事**——

> 此规则仅在桥自写的 E-b 世界成立……**不得被继承到编排接入层**(生产 `HostContext::set` 对零值照写不删,真实链账户表**必然**含零值槽行)

**但全仓不存在任何 E-b/生产模式开关**(`ProductionMode` / `strictInvariant` / 类似命名 grep 皆空)。规则是硬编码的,"不得被继承"没有任何机制保证。

已实测确认存在的两个实例:①账户只有一个零值槽 → `has_storage` 返回 1;②`/apps/HelloWorld` 这类非 20 字节 hex 表名 → `poisoned=1`(同一个 poison 机制的另一个生产触发点)。

---

## 第一步(必做,先于代码):怎么保住 E-b 的写回泄漏检查

那条 `throw` **有真实价值**:它守护的是"`applyDiff` 的契约②从不写零值槽(零值=删槽)"这条不变量,一旦写回路径有漏,它会响亮地叫。**直接删掉就丢了这条守护。** 但把它留在读路上,生产账本就永远过不去。

请评估至少这三个方案,写进报告等我裁定:

- **(A) 把不变量检查移到写回路径** —— 在 `applyDiff` 写槽的地方断言"不写零值",读路(`fetchAllStorage`)改为**跳过**零值槽。
  理由:这条不变量的主体是**写**,不是读。在读路检查是在下游抓上游的错,而下游同时还要服务不遵守该契约的生产账本。移到写路后,契约在它真正的归属地被守护,读路可以只实现以太坊语义。
- **(B) 加模式开关**(构造参数或模板参数),E-b 模式 throw、生产模式 skip。
  代价:多一个状态维度,且"哪个模式"会成为新的配置错误来源;但它最贴近注释原意。
- **(C) 降级为 debug-only 断言**(`assert` / 仅在测试构建生效),生产构建跳过。
  代价:Release 构建下守护消失,而生产恰恰是 Release。

**控制器倾向 (A)** —— 它同时解决"生产过不去"和"不变量无人守护",且不引入模式状态。**但若你发现 `applyDiff` 的写槽点无法覆盖全部写入来源(例如还有别的路径能往账户表写槽),(A) 就不成立,报我裁定。**

**你要查清并写进报告**:
1. `applyDiff` 写槽的**全部**代码路径(逐个 `file:line`)。有没有绕过它的写入来源?
2. 那条不变量今天有测试覆盖吗?——即"写回路径漏写了零值槽"这个场景,现在有用例吗,还是只靠 `fetchAllStorage` 的 throw 在运行时兜?**这一问决定 (A) 会不会造成守护净减少。**
3. 除 `fetchAllStorage` 与 `probeHasStorage`,还有没有第三、第四处依赖"行存在 = 有内容"或对零值有特殊处理的判据?(读单槽的 `get_storage` 路径尤其要看:它对零值返回什么,与前两者一致吗?)

---

## 第二步:按裁定统一语义

把零值语义在**所有**相关判据上统一到"零值 = 不存在"。

**修法约束**:
- **不要为了修这条把 KEEP 契约改坏**。"零值槽 = 不存在"与"账户存在但 code 为空 = 存在"是两回事,后者必须继续返回"存在"。这是本桥的核心语义。
- **不要破坏既有的墓碑过滤**。`liveContent()` 过滤 `NOT_EXISTS_TYPE`/`DELETED_TYPE` 是"终审 I-1"已修的东西(注释在 `:665-669`、`:388-393`),零值过滤是**叠加**在它之上的第二层,不是替代。
- 只动 `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`;若 (A) 需要动 `applyDiff` 所在文件,那是方案本身要求的,可以动,但**先报我裁定通过**。

## 第三步:测

测试放 `bcos-evm/test/opstack/Storage2LedgerTest.cpp`。至少覆盖:

- **`stateRootOf` 在含零值槽的账户表上不再 poison,且算出的根与"该槽不存在"时一致** ← 这是本批最核心的一条,它同时钉死"不 throw"和"不进 trie"
- 账户只有零值槽 → `has_storage` 为 **false**
- 账户有非零槽 → `has_storage` 为 true(防改过头)
- 零值槽 + 非零槽混合 → true,且 `stateRootOf` 只计入非零槽
- 零值槽 + 墓碑混合 → false,且不因新增零值过滤而破坏墓碑过滤
- 按裁定方案对应的不变量守护测试(若走 (A):写回路径写零值槽 → 断言在**写**的时候就失败)

**自验六步一个不能少**:改回旧判据 → 重建 → **必须翻红** → 还原 → **重建** → 复绿。

**红绿见证必须点名目录**:`Storage2LedgerTest` 在 `if(TARGET bcos-framework)` 门控内,**standalone 对它是「依赖图无边」的空真**,131/131 不得当作证据。上一轮就是靠"standalone 报 Built target 但二进制日期比源码旧"才发现这一点的。

---

## 交付要求

1. **第一步的三问先交报告等裁定**,不要直接开写;
2. 通用组合根零漂移;`ports/` `vectors/` `golden/` `transaction-scheduler/` `bcos-rpc/` 零触碰;
3. 全量回归零退化;
4. **spec §6.4 落记账(发现方当场落地,这是本工作流已裁定的惯例)**,内容至少含:零值槽语义的修复与残留边界、以及**把"poison 机制在生产账本上的触发点"列入 op-node 实连前置清单**(与条目 q 同级)——它今天只存在于代码注释里,五轮审查没人把它当接入阻塞项看,这正是"发现了但没传播到位"的又一例;
5. 报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch9-report.md`;
6. **尽早、分段提交**:三问答完先提交报告初稿,代码一到绿立刻提交,最后动文档。本闭环已有两次 watchdog 停滞 + 三次 API 断流,提交是唯一的抗断流保险。

返回四行 STATUS / COMMITS / TESTS / CONCERNS。
