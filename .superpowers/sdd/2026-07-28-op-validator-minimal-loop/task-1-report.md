# Task 1 报告: 协程契约条件式许可修订(spec §4.4 前置义务)

## 结论

Step 1(改写)已落地,Step 2(重编译验证)按用户执行协议豁免——本项目开发期跳过一切
编译/测试运行,全部开发完成后统一编译验证,报告如实标注"未编译验证"。Step 3(commit)
已完成。

## 语义依据

`docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md` §4.4 三条:

1. **嵌套拓扑声明**:engine `newPayload` → `executeOpBlock` → 桥读方法(syncWait)→
   (stateRoot 段)`visitAccounts`(syncWait)→ 惰性 code getter(`AccountView::code()`,
   内部经 `get_account_code` 再次 syncWait 驱动 `fetchCode`)——多层嵌套,取代终审
   I-3"仅一层、仅一个调用点"。
2. **安全前提**:a) 桥对接的 storage2 后端全部在 `co_await` 处线程内同步完成(内存
   MultiLayerStorage;RocksDB 为线程内阻塞读),内层任务从不真正让出线程、外层协程
   从不跨线程恢复,嵌套 syncWait 退化为纯栈递归;b) engine 执行段整体被 `x_state`
   锁串行(单线程,无并发观察窗口)。
3. **失效判据**:任何后端引入跨线程/事件循环真正异步完成语义,本许可立即失效,必须
   重新设计——同时覆盖 `handleNewPayload`"持锁跨 co_await"现存 TODO(共享同一安全
   前提)。

## 修订前后文本对照

### 1. `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(头注,原 20-31 行 → 现 20-38 行)

**修订前**(终审 I-3,单层嵌套例外):
```
// 禁协程上下文调用:每次读操作由 task::syncWait 驱动单层协程(Wait.h:42-121)。禁止
// 在已处于协程上下文(即外层已有一个 syncWait 尚未返回)时再次调用本桥的读方法——
// 嵌套 syncWait 是已知的栈陷阱,本桥不做该场景下的正确性保证(design §4.1)。
//
// 许可例外(终审 I-3,单层嵌套):AccountView::code()(§6 惰性 code getter)是本条款下
// 唯一的合法用法——它在 visitAccountsImpl 的协程体内被 visitor 同步调用,内部经
// get_account_code 再次 syncWait 驱动 fetchCode,构成一层嵌套。安全前提不是"嵌套
// syncWait 总体安全"这条被推翻的论断,而是本桥当前只对接内存/线程内阻塞完成的
// storage2 后端——底层任务在 co_await 处同步落地,不会真正让出线程、不会在嵌套帧
// 之间产生并发观察窗口。仅此一层嵌套、仅此一个调用点被认可;禁令本身不放宽——任何
// 更深的嵌套,或未来后端引入真正跨线程/跨事件循环的异步完成语义,都会使这条例外失效
// (design §4.1)。
```

**修订后**(条件式许可,三条对应 design §4.4):
```
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
```

### 2. `docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`(§10.1,原
"协程重入契约单测裁剪未交付"条目末尾追加修订段)

在原有"当前的契约落地形态"段落末尾(引用行号从 `Storage2Ledger.h:20-31` 更新为
`Storage2Ledger.h:20-39`)追加以下修订段:

```
**（op-validator-loop spec §4.4 修订,2026-07-28）**:终审 I-3 原表述的"仅一层嵌套、仅此
一个调用点"已被证明不敷合流执行链(engine `newPayload` → `executeOpBlock` → 桥读方法 →
`visitAccounts` → 惰性 code getter,多层嵌套)的实际拓扑需要,改写为**条件式许可**:
1) 嵌套拓扑声明——许可上述多层嵌套链路,不再限定单层单调用点;2) 安全前提——桥对接的
storage2 后端全部线程内同步完成(内层任务从不真正让出线程、外层协程从不跨线程恢复,嵌套
`syncWait` 退化为纯栈递归)+ engine 执行段被 `x_state` 锁整体串行(单线程,无并发观察窗口);
3) 失效判据——任何后端引入跨线程/事件循环的真正异步完成语义,本许可立即失效,且该判据与
`handleNewPayload`"持锁跨 co_await"现存 TODO 共享同一安全前提。详见
`2026-07-28-op-validator-minimal-loop-design.md` §4.4。
```

原文中其余对"终审 I-3"的引用(如"含终审 I-3 新增的惰性 code getter 单层嵌套例外"一句)
已随行号引用一并更新为"取代式"框架——修订前逐字文本已在头注段被替换,不再残留自相矛盾的
"仅一层"断言;`§10.1` 段落保留终审 I-3 作为历史背景引用,不影响当前契约以条件式许可为准。

## 校验

- `rtk grep -n "仅一层\|终审 I-3\|单层嵌套"` 对两个文件的扫描确认:仅剩的"仅一层"/
  "单层嵌套"字样均出现在"取代式"引用语境中(即"终审 I-3 原表述...已被...改写为"),
  没有与新许可矛盾的独立断言残留。
- 三条许可文字(嵌套拓扑声明/安全前提/失效判据)与 design §4.4 逐条对应,未做改写式转译。

## Commit

```
959ebbf6c docs(bridge): 协程嵌套例外改写为条件式许可(验证者闭环 spec §4.4 前置义务)
 bcos-evm/bcos-evm/ledger/Storage2Ledger.h          | 29 ++++++++++++++--------
 .../specs/2026-07-27-real-ledger-bridge-design.md  | 20 ++++++++++-----
 2 files changed, 32 insertions(+), 17 deletions(-)
```

暂存时的事故与修复:首次 `git add -f docs/superpowers bcos-evm`(brief 字面命令,`-f`
作用到整个 `bcos-evm` 路径)误将 gitignore 掉的 `bcos-evm/build/` 编译产物目录(4983
文件、约 98.8 万行插入)强制纳入暂存区。已立即 `git reset` 撤销,改为分别精确
`git add bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(无需 `-f`,该路径未被忽略)+
`git add -f docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`(仅对
`docs/superpowers` 这一条 gitignore 规则命中的路径使用 `-f`),提交前用 `git status`
核实暂存区仅含预期两个文件。

## Step 2(编译验证)

按用户执行协议:本项目开发期跳过一切编译/测试运行(FISCO 编译耗时,全部开发完成后
统一编译验证)。**未编译验证(协议豁免)**——本任务为纯注释/文档改动,预期对编译产物
零行为影响,留待全部 7 个任务完成后统一编译验证确认。

## 测试用例

本任务无测试用例产出(纯注释/文档改动,brief 未要求新增测试)。
