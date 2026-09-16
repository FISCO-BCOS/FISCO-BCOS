# Task 4 (C6/WI-21) — target 计数表（实测 40 行）+ PR gate 预算实测

- 日期: 2026-09-13
- 分支: `feat/karst-on-318-merged` @ cc185536a（worktree `merge-318-rehearsal`，macOS arm64 本机）
- 性质: 过程文档，**untracked，永不入库**；本任务零 git 提交、零 tracked 文件改动。
- 原始数据: `/tmp/target-counts.txt`、`/tmp/target-times.txt`、`/tmp/tc-out/*.log`

## 方法

- Step 4.1 按 plan 修正版 find（排除 `*.sh`/`*.py`/`*.cmake`）列出全部测试二进制：**恰好 40 个**（与 F5 审查一致；`opstack-executor-*-tests` 5 个）。
- 逐个串行执行（无并行），每二进制 180s 看门狗（macOS 无 GNU timeout，用 shell `wait`+`kill` 实现）。计数规则：`Running N test cases` 首个匹配，绝不用 `errors detected` 子串。同时记录 rc 与 wall time。
- 已知陷阱两条，都已实测并记入下表：(a) 部分二进制从 repo 根目录直接跑会因相对路径 fixture 失败（ctest 配置了 `WORKING_DIRECTORY`）；(b) 日志含 NUL 字节时 macOS BSD grep 会判为 binary 并对 `-o` 静默，造成假 SKIP/ERR（需 `grep -a` 复核）。

## 40 行计数表

| #  | target                                                | cases | wall | rc      | 分类 | 依据 / 备注 |
|----|-------------------------------------------------------|-------|------|---------|------|-------------|
| 1  | build/bcos-boostssl/test/test-boostssl                | 27    | 0s   | 201     | known-pre-existing (F-TEST-1) | `WsToolsTest/test_WsToolsTest` "check !valid has failed"；Boost 1.88 行为，已立案；bcos-boostssl 无本分支提交 |
| 2  | build/bcos-codec/test/test-bcos-codec                 | 70    | 0s   | 0       | green | |
| 3  | build/bcos-crypto/test/test-bcos-crypto               | 47    | 2s   | 0       | green | |
| 4  | build/bcos-devp2p/test/test-bcos-devp2p               | 35    | 0s   | 0       | green | |
| 5  | build/bcos-evm/test/bcos-evm-eth-tests                | 7     | 0s   | 0       | green | |
| 6  | build/bcos-evm/test/bcos-evm-opstack-tests            | 183   | 0s   | 0       | green | fork-matrix 目标 |
| 7  | build/bcos-executor/test/unittest/test-bcos-executor  | 368   | 54s  | 201     | known-pre-existing | `precompiledCryptoTest/testSM3AndKeccak256` 2 项 check 失败；两棵树都红、源码 upstream-identical |
| 8  | build/bcos-framework/test/test-bcos-framework         | 75    | 0s   | 0       | green | |
| 9  | build/bcos-front/test/test-bcos-front                 | 26    | 5s   | 0       | green | |
| 10 | build/bcos-gateway/test/test-bcos-gateway             | 146   | 9s   | 201→0   | invocation artifact（修正后 green） | 根目录直跑 10 失败全是 `data/config/config_*.ini: cannot open file`（相对路径 + CWD）；从 ctest `WORKING_DIRECTORY`=`bcos-gateway/test/unittests` 重跑：rc=0、`*** No errors detected` |
| 11 | build/bcos-ledger/test/test-bcos-ledger               | 242   | 1s   | 0       | green | |
| 12 | build/bcos-pbft/test/test-bcos-pbft                   | 143   | 16s  | 0       | green | |
| 13 | build/bcos-protocol/test/test-bcos-protocol           | 11    | 0s   | 0       | green | |
| 14 | build/bcos-rlp-protocol/test/test-bcos-rlp-protocol   | 89    | 0s   | 0       | green | |
| 15 | build/bcos-rpbft/test/test-bcos-rpbft                 | 1*    | 0s   | 0       | count-rule artifact（实际 PASS） | 自定义 main()，无 Boost banner；实际跑 1 个用例 `testRPBFTConfig`，`*** No errors detected`、rc=0 |
| 16 | build/bcos-rpc/test/test-bcos-rpc                     | 336   | 7s   | 0       | green | |
| 17 | build/bcos-scheduler/test/test-scheduler              | 48    | 10s  | 0       | green | |
| 18 | build/bcos-sdk/tests/test-bcos-cpp-sdk                | 83    | 0s   | 0       | green | |
| 19 | build/bcos-sealer/test/test-sealer                    | 53    | 2s   | 0       | green | |
| 20 | build/bcos-security/test/test-bcos-security           | 27    | 5s   | 0       | green | |
| 21 | build/bcos-storage/test/unittest/test-storage         | 23    | 3s   | 0       | green | |
| 22 | build/bcos-sync/test/test-bcos-sync                   | 39    | 11s  | 0       | green | |
| 23 | build/bcos-table/test/test-table                      | 110   | 0s   | 0       | green | |
| 24 | build/bcos-tars-protocol/test/test-bcos-tars-protocol | 143   | 3s   | 0       | green | |
| 25 | build/bcos-tool/test/test-bcos-tool                   | 119   | 1s   | 0       | green | |
| 26 | build/bcos-tx-validator/test/test-bcos-tx-validator   | 90    | 0s   | 0       | green | |
| 27 | build/bcos-txpool/test/test-bcos-txpool               | 73    | 6s   | 0       | green | |
| 28 | build/bcos-utilities/test/test-bcos-utilities         | 79    | 3s   | 0       | green | |
| 29 | build/engine/test/test-bcos-engine                    | 351   | 0s   | 0       | green | fork-matrix 目标 |
| 30 | build/ethereum-executor/tests/test-ethereum-state-smoke | n/a* | 0s  | 0       | count-rule artifact（PASS） | 独立 smoke main()，非 Boost target：`TestEthereumStateSmoke: all checks passed` |
| 31 | build/legacy/bcos-ledger/test/test-legacy-ledger      | 2     | 0s   | 0       | green | |
| 32 | build/libtask/tests/test-task                         | 19    | 5s   | 0       | green | |
| 33 | build/mempool/test/test-bcos-mempool                  | 25    | 0s   | 0       | green | |
| 34 | build/opstack-executor/tests/opstack-executor-block-tests | 146 | 1s | 0       | green | fork-matrix 目标 |
| 35 | build/opstack-executor/tests/opstack-executor-detail-tests | 12 | 0s | 0       | green | fork-matrix 目标 |
| 36 | build/opstack-executor/tests/opstack-executor-receipt-tests | 26 | 0s | 0      | green | fork-matrix 目标 |
| 37 | build/opstack-executor/tests/opstack-executor-scheduler-tests | 40 | 0s | 0     | green | fork-matrix 目标 |
| 38 | build/opstack-executor/tests/opstack-executor-tests   | 134   | 0s   | 0       | green | fork-matrix 目标 |
| 39 | build/transaction-executor/tests/test-transaction-executor | 180** | 0s | 201   | pre-existing（upstream 继承，非本分支回归） | `TestHostContext/nestConstructor` 2 项 check 失败 [0 != 10]；**该模块 `fisco/release-3.18.0..HEAD` 零提交**，用例源自 upstream 老提交（cf8fb1077b、56dbae3c5b）→ 与基线树同红 |
| 40 | build/transaction-scheduler/tests/test-transaction-scheduler | 123 | 1s | 0      | green | |

\* 原始文件记为 SKIP/ERR：15 号是自定义 main 无 banner（实跑 1 用例）；30 号同理（smoke，无用例计数口径）。
\*\* 原始文件记为 SKIP/ERR 是 grep 陷阱：日志含 NUL 字节，BSD grep 判 binary 后对 `-o` 静默；`grep -a` 复核 banner 为 `Running 180 test cases`。

### 汇总

- 40/40 全部执行完成；**0 HANG、0 corpus-absent-skip**（最长单二进制 54s，全程串行 145s）。
- Banner 计数总和：37 行数值 3570 + #39 修正 180 + #15 自定义 main 1 = **3751 用例 / 39 个 Boost 口径二进制**（#30 smoke 无用例计数口径）。
- 非绿 rc 共 3 个二进制：#1 boostssl、#7 executor（两者即计划认定的已知预存失败）、#39 transaction-executor（新识别，**定性为 upstream 继承、非本轮回归**，证据见上表：模块零分支提交）。#10 gateway 的 rc=201 是测量方法 artifact，按 harness 条件重跑即绿。
- **regression-suspect：0 行。**

## Step 4.2 — PR gate 预算实测（只读现成 CI run，未推送任何东西）

### Run 34737509739（FISCO-BCOS GitHub Actions，feat/karst-on-318-merged，2026-09-13T04:16Z，overall failure — 失败根因已另行定案：gcc-14 -Werror aggregate-init（修于 7d378a14c）+ macos regen 步 GitHub "not our ref" 瞬态 flake（corpus 侧修于 cc185536a retry））

| job | 结论 | 关键 step 计时 |
|---|---|---|
| CI pin gates | success | 全程 7s（pin 一致性/语料/citation 各 <1s） |
| Build (ubuntu-24.04) | failure | Configure 31m18s；Build **失败** 9m04s（gcc-14 -Werror）；Checkout e2e assets / Symlink / Regenerate t8n / **Test / Fork-label guard / Mutation harness 全部 skipped** |
| Build (ubuntu-24.04-arm) | failure | Configure 29m34s；Build 失败 8m40s；fork-matrix 步全 skipped |
| Build (macos-15) | failure | Configure 25m16s；Build **成功** 16m14s；Checkout op-stack-e2e-tests assets **2s**；Symlink **<1s**；**Regenerate opstack t8n vectors 18s（失败于瞬态 fetch flake）**；Test / Fork-label guard / Mutation harness 被 step 链截断 skipped |
| Build (windows-2025) | success | fork-matrix 步按 `matrix.os != 'windows-2025'` 条件不适用 |
| L2 unit suites | failure | Build 失败（同一 gcc-14 break），测试步 skipped |
| Coverage | failure | Build 失败（同一 break），regen/test/coverage 步 skipped |

### 基线（fork-matrix 步存在之前，同一 workflow 的现成 run，Test 步 = 纯 `ctest -j3`）

| run | 分支 | ubuntu-24.04 | arm | macos-15 |
|---|---|---|---|---|
| 34696802512 | feat/engine-cutover-on-prereqs | 1m29s | 1m23s | 1m33s |
| 34690370085 | feat/engine-cutover-on-prereqs | 1m41s | 1m23s | 1m13s |
| 34621542180 | chore/move-op-e2e-harness | 1m52s | 1m24s | 1m34s |

→ 纯 build+ctest 基线 Test 步 **1m23s–1m52s**（这些分支的 job 里根本不存在 fork-matrix 各步，即增量全部来自本分支引入的步）。

### Fork-matrix 增量（PR gate 口径 = ubuntu-24.04 leg；guard/mutation 仅 ubuntu 跑）

| 分量 | 实测/估计 | 依据 |
|---|---|---|
| Checkout op-stack-e2e-tests assets | **2s**（实测） | run 34737509739 macos leg |
| Symlink e2e assets | **<1s**（实测） | 同上 |
| Regenerate opstack t8n vectors | **18s**（实测，但为失败尝试；成功时长无 run 可考，含 corpus 克隆，量级应为数十秒） | 同上 |
| Test 步增量（新增 fork 二进制：5×opstack-executor=358 + bcos-evm-opstack=183 用例；本机串行合计 ≈2s） | **≈10–30s（估计）** | 本机实测 × CI 减速系数估计 |
| Fork-label coverage guard（3 个二进制 `--list_content=DOT` + grep） | **≈2–10s（估计）** | 纯本地进程工作，无构建 |
| Mutation harness（16 变体 × (apply + 单 TU 增量重编 + 重链 19–30MB 二进制 + 过滤跑红/绿对照)） | **未实测**（本 repo 所有历史 run 均未跑到该步；本机不跑——它会临时改 tracked 源码） | 结构估计 20–70s/变体 → **≈5–19 min** |

### 5 分钟预算判定

- **可直接实测的增量分量合计 ≈ 21s（checkout+symlink+regen 尝试），加 Test 增量与 label guard 的估计后 ≈ 1–2 min —— 单看这些，预算内余量充足。**
- **但 mutation 门是预算主导项且无任何实测数据**：16 个变体每个都要付一次目标重链（test-bcos-engine 30MB 等链 12 次）。按 CI 常见链速估计中位 ≈ 8–12 min，**大概率单独突破 ~5 min 判据**；乐观情形（链 ≤15s）也贴着预算线。
- 结论：**5 分钟超限判据"可能被触发但未被数据证明"** —— 需要一次真正跑到该步的 CI run（gcc-14 修复 7d378a14c 与 corpus flake 修复 cc185536a 已在 HEAD，下次 run 即可测得）拿 mutation 门的实测时长后再定是否对 `workflow.yml` 分档。按 plan 约定，重分档是单独决策，不在本任务内执行。

## 口径变更记录（按 plan Step 4.1 要求记入台账）

- 设计文档 §6.9 的"30 行"口径过时：本机实测 `build/` 下可执行测试 target **40 行**（opstack-executor 5 个），验收按 40 行执行。
- 本机非绿三个二进制均非本分支回归；其中 transaction-executor 的 `TestHostContext/nestConstructor` 为新识别的预存红（建议后续按 F-TEST 台账口径补立案，并注明"模块零分支提交、upstream 继承"证据）。
