# 视角 5 · 测试有效性、文档一致性与可运维性

**先读** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-context.md`。
**报告写** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p5-report.md`。

## 你的范围

- 测试:`bcos-evm/test/opstack/` 下的 `EngineNewPayloadGateTest.cpp`(1720)、`EngineOpBranchTest.cpp`(1663)、`OpSchedulerImplTest.cpp`(1017)、`EthBlockHeaderTest.cpp`(390)、`EngineVersionGateTest.cpp`(330)、`Storage2LedgerTest.cpp`(99)
- 金值语料:`bcos-evm/test/opstack/t8n/golden/engine/`(33 向量 + `manifest.txt` + `SHA256SUMS` + `README.md`)与生成器 `t8n/generator/main.go`(+367/-50)
- 文档:`docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md`(+210)、`t8n/golden/engine/README.md`(+179)

## 你的问题

你负责回答"**这 239 个绿色的测试,到底证明了什么,又假装证明了什么**"。历史上这个视角产出最高。

1. **假绿猎杀(主任务)**。逐类找**断言不了任何东西**的测试:
   - 断言了自身计算结果的(用被测代码算出期望值,再拿去比被测代码)
   - 只断言"没抛异常"或"返回非空"的
   - 子串匹配过宽的(如 `find("blockHash")` 也被 `parentBlockHash` 满足;同一条子串同时匹配两条语义不同的错误消息 → 桶的区分没被钉死)
   - 被更前面的 fatal `ASSERT_*` 挡住、实际从不执行的后续断言
   - 依赖执行顺序的(用例间共享状态)
   **对每一条,给出:哪一行生产代码删掉之后这个测试仍然会绿。** 这是本仓已验证有效的判据。
2. **覆盖清点(用清单法,不要凭印象)**:把 `EngineServiceImpl.h` 的每一条比对项、每一条校验、每一个 `throw`,和 `OpSchedulerImpl.h` 的每一个解码器拒绝分支,列成一张表,逐行标注"哪个用例覆盖它 / 无覆盖"。**无覆盖的行**就是发现。历史上这个方法找到过"8 条比对里有 2 条从未被任何用例触碰"。
3. **金值语料的结构性盲区**(已记账,但请独立量化):33 条向量里 `number` / `timestamp` / `baseFeePerGas` / `currentRandom` / `currentCoinbase` 各取几个不同值?**取值恒定的字段 = 该字段的映射零守护**(把生产代码里的映射改成硬编码常量,测试照样全绿)。逐字段给出实际的取值集合(自己扫 json,不要相信我的描述)。
4. **provenance**:`SHA256SUMS` + `manifest.txt` + `README` 里的 op-geth pin。**如果有人用本仓自身的实现重新生成金值,gate 会静默退化成同义反复而全绿吗?** 现在的机制挡住了这条路多少?诚实评估(它显然挡不住"同时刷新校验和"的重新生成——那么它到底挡住了什么)。
5. **生成器 `main.go` +367 行**:它是金值的来源,**它自己有没有被测过**?生成器的缺陷会同时污染金值和"通过率",是三方归因(实现缺陷 / 生成器缺陷 / 语料缺陷)里最难发现的一类。
6. **文档与代码一致性**:spec §6.4 的 a–s 逐条到源码核对——描述是否属实、行号是否还对、"已修复/未实现"的状态标注是否与代码当前状态一致。**找出任何一条"文档说已做但代码没做"或"文档说没做但代码做了"**。§11 的 checklist 同理。
7. **可运维性**:节点运维在生产上看到一个 INVALID 时,`validationError` 的消息够不够定位问题?已记账条目 (j):`catch(...)` 丢弃 `e.what()`,导致 `OpBlockExecute.cpp` 的四处不同 `throw`(空块 / 首笔非 L1 attributes / deposit 乱序 / 非 deposit 交易校验失败)抵达 engine 后**共用同一条泛化消息**。核实这条,并清点还有多少处同类的信息湮灭。

## 交付

按共享上下文的报告格式。第 1 点的每条发现**必须**带"删掉哪一行生产代码它仍然绿"——这是可验证的断言,标 `[需验证]` 由协调者事后执行。第 3 点必须给出你自己扫出来的取值集合。
