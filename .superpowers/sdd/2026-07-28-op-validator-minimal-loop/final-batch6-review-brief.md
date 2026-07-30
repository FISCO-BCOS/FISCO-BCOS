# 终审批 6 复审:`SYS_HASH_2_TX` 补写(裁定 B —— OP 专用表 `s_eth_hash_2_rawtx`)

工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`。
**不豁免编译与测试。** 回归基线:批 4 收口 in-tree 238/238、standalone 131/131、`test-bcos-engine` 11 例、E-b 桥三腿 33×3。

## 输入

- 派单:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch6-brief.md`
- 实施者报告:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch6-report.md`
- 差异包:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/review-ca28d9114..7b7e0afb3.diff`(BASE `ca28d9114` → HEAD `7b7e0afb3`,两提交)

## 本批背景(为什么裁定 B)

用户裁定"交易本体需要补写实现"。实施者第一步调研查明:方案 (A)(写通用 `SYS_HASH_2_TX`)不只是代价高,而是**会造出假交易**——`takeToTarsTransaction()` 只覆盖类型 0/1/2/3(`0x04`/`0x7E` 在 `Web3Transaction.cpp:408-413` 硬拒);tars IDL 无 `sourceHash`/`mint`/`authorizationList` 承载位;`Transaction::verify` 无条件 ecrecover 并对 Web3 类型 forceSender,未签名的 deposit 会得到**伪造的 sender**;且 tars 反序列化任意字节**通常不抛**(字段全 optional + `Tars.h:328-356` 空 catch),之后工厂在 `checkHash=false` 下重算出自洽哈希,返回一个**非空、看起来合法的假交易**,其 `hash() != key` 而无人校验——会流入 `eth_getTransactionByHash` **以及 txpool `requestMissedTxs`(共识/提案校验)**。

控制器据此裁定 **(B)**:写 raw envelope 到 OP 专用表 `s_eth_hash_2_rawtx`(表名常量按裁定 B5 放 `bcos-evm/bcos-evm/engine/`,不动 `LedgerTypeDef.h`),**通用 `SYS_HASH_2_TX` 刻意不写**并在 spec/README 写明这是 OP 专用检索面。

## 复审重点(按优先级)

1. **实现正确性**:五张表的写入(键、值、时机、失败路径)。特别核对 `s_eth_hash_2_rawtx` 的键是否确为 `keccak(raw envelope)`、与同一块 `SYS_HASH_2_RECEIPT` 的键**逐笔一致**(这是交叉验证的支点);写入是否与其余四张表处在同一失败语义下(任一失败应导致块不被登记,而不是半登记)。
2. **断言的判别力**:新用例 `RawTransactionEnvelopesAreRegisteredUnderEthTxHash` 报称覆盖三种交易类型(deposit `0x7E` / eip1559 `0x02` / setcode `0x04`)各五条断言,含一条**反例断言"`SYS_HASH_2_TX` 必须仍为空"**。请独立做一次反证:**把新增写入注释掉 → 重建 → 确认翻红且红的是预期用例**;再做一次**把反例断言对应的生产行为反转**(例如故意也往 `SYS_HASH_2_TX` 写一条)→ 确认那条反例断言翻红。两次都要**还原 + 重建 + 复绿**(三步缺一不可,多构建目录须各做一次)。
3. **实施者自陈的三条,逐条独立核实**(原文引述):
   - *"standalone 腿对本条不是红绿见证,已如实标注:engine 测试仅编入 in-tree(`if(TARGET bcos-framework)` 门控),禁用写入时 standalone 照样 131/131。它在这里只是**无回归检查**"* —— 核实门控确实存在且结论成立;并判断它建议的"把这层区分补进 §11"(区分"两个目录各做一次自验"与"两个目录都构成红绿见证")是否值得采纳,给出你的意见。
   - *"命名债主动披露:`ValidPayloadRegistersAllFourTables` 名字已过时(现写五张表)。**故意不改名**——批 3 报告按名引用它作为回执写入变异实验的翻红见证,改名会断掉可追溯性"* —— 判断这个取舍是否成立,是否应改为改名 + 在报告加重定向注记。
   - *"零漂移:`EngineServiceImpl.h` 42 增 / 10 删,**10 行删除全部是注释行**,`-w` 过滤后非注释删除为 0"* —— 用 `git diff -w` 独立复核通用组合根零漂移,并确认无 OP 依赖名进入任何成员函数**签名**(T5b 踩过此坑:签名随类模板实例化,体才惰性实例化)。
4. **文档准确性**:spec §6.4 (f) 从"未实现"改写后的措辞是否与代码实际一致;新增记账条目 q/r/s 是否属实且无灌水——
   - q:`bcos-rpc/EngineEndpoint.cpp:164` 把以太坊 RLP 信封喂给 tars 反序列化器,且 `rawTransactions` 在任何生产路径上从未被赋值(op-node 实连第一道墙,已置顶);
   - r/s:两条 pre-existing 缺陷(`LedgerMethods.h:233-235` 未 `has_value()` 即解引用;`RocksDBStorage.cpp:228-233` 成功回调在 `try` 内 → 双回调 → 同一协程 handle `resume()` 两次,UB)。
   逐条到源码核对行号与结论。
5. **边界遵守**:`ports/` + `vectors/` + `golden/` + `transaction-scheduler/` + `bcos-rpc/` 零触碰;未改 `LedgerTypeDef.h`。
6. **全量回归零退化**:in-tree 全量、standalone、`test-bcos-engine`、E-b 桥三腿。实施者报 in-tree 239/239。**不要重复实施者已在同一代码上跑过的测试**,但上面第 2 点的两次反证注入必须你自己跑。

## 交付

报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch6-review-report.md`。若注入实验有未还原的改动,**必须在结束前还原并重建确认复绿**(批 3 出过审查者断连留下未还原注入的事故)。

返回四行 STATUS / FINDINGS(按 Critical/Important/Minor 分级) / TESTS / CONCERNS。
