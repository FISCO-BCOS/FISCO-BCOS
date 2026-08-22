# PR 5443 review context (Round 1 for this reviewer)

## 元数据
- PR: FISCO-BCOS/FISCO-BCOS#5443 — "[NodeConfig] Remove getters/setters, expose public members, migrate callers (phase 3 of 3)"
- head: `703013f5d8aba4cc02e66e244f753df99095e0be`（本地 ref: `pr-5443-head`，已核对与 GitHub live 一致）
- base: release-3.18.0（merge-base `63e131920`）。Stacked：#5439(phase 1)+#5440(phase 2) 均未合入，本 PR diff 含全栈改动，但前两阶段在各自 PR 审；本轮聚焦 phase 3 语义（删 getter/setter、公共成员、调用方迁移），前两阶段内容只做一致性抽查。
- 规模：47 files, +1260/-2191。完整模式 5 agent。
- CI：最新一轮 Build ubuntu/macos/arm/windows、Coverage、L2 均 pass，macos-15-intel pending；**check commit FAIL：valid_insertions 994 > limit 666（insertions 1067，git reports 1394）——合并门禁红**（run 32326838612）。较早一轮的 macos-intel/ubuntu 失败已被新 run 覆盖。

## PR 内容（作者描述 + diff 核实）
- `bcos-tool/bcos-tool/NodeConfig.h`（+234/-438）：删全部 getter/setter 声明（~130 个）；`m_genesisConfig`/`m_ledgerConfig`/`m_keyFactory` → 公共成员 `genesisConfig`/`ledgerConfig`/`keyFactory`；删死代码 `loadAlloc()`、2 参 `checkAndGetValue()`、`m_genesisData`/`m_encKeyIndex`；3 参 `checkAndGetValue` 成员方法 → 自由函数。
- `NodeConfig.cpp`（+356/-1129）：删 ~149 个 getter/setter 定义。
- 调用方迁移 ~35 文件：bcos-rpc、bos-security、bos-scheduler、bos-sealer、libinitializer、fisco-bcos-air、fisco-bcos-tars-service、legacy/lightnode、tests/perf、tools/（archiveTool +31/-29、storageTool +36/-31、encryptCertFile +2/-2）+ 测试文件若干。

## 已有 reviewer 评论状态（上轮，供对照勿重复报）
- kyonRay R1 BLOCKING「tools/ 未迁移但 getter 已删 → -DTOOLS=ON 编译不过」→ 已在 head 修复（kyonRay 自己复核确认 58 处调用已迁移，blob 对账通过）。
- ywy2090 IMPORTANT ×2：`(void)` 降级反模式——`NodeConfigTest.cpp:93`（base `BOOST_CHECK_NO_THROW(cfg.chainId())` → `(void)cfg.genesisConfig.m_chainID;`）、`NodeConfigChainLoadersTest.cpp:234`（pdAddrs/genesisData 断言消失）。kyonRay 附和并给数据：test tree `(void)` 计数 base=0 → head=2。⚠️ 需核实 head 是否仍是 2。
- kyonRay：`NodeConfig.h:50` 附近残留孤儿分隔注释行（`=====` 底边框）。
- ywy2090 SUGGESTION ×2：genesisConfig 公共化失去 const 守卫（写点为 0，潜在风险）；HsmDataEncryption.cpp:40 "encKeyIndex never configurable" 注释与历史不符（273888d7c 前可配置）。

## 审查方法要点
- 这是机械重构 PR，核心风险：① getter 非平凡（带默认值/转换/校验逻辑）被字段直读替换后语义丢失；② setter 里的一致性副作用（清缓存/派生字段联动）被直写绕过；③ 删掉的"死代码"实际有调用方（宏/条件编译/TiKV/WASM 路径）；④ 迁移把字段名映射错（如 `chainId()` ↔ `genesisConfig.m_chainID` 类型/语义不一致）；⑤ 测试断言降级。
- 真值来源：`git show pr-5443-head:<path>` 与 `git show 63e131920:<path>`（base）对照；diff 在 `pr_diff_5443.txt`（6204 行，按文件选读）。
- 每条 finding 必须 `git show` 验证，禁止只凭 diff 猜。

## 输出 schema
findings: [{severity: CRITICAL|IMPORTANT|SUGGESTION|NIT, category, title, file, line, detail, impact, suggested_fix}]，另附 verified_ok 清单（验证过没问题的点）。行号取 head 文件行号。
