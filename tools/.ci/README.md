# Air 集成测试：原生场景与 Java 用例对照

`ci_check_air.sh` 用分支自己的 `fisco-bcos` 二进制起四条链并行跑 `scenarios/*.sh` 里的原生场景（ops 子命令 + `jq` 断言），再按 `RUN_JAVA` 跑一轮 `java_legacy_air.sh`（只剩 java-sdk 与 v1 链上的 web3/Hardhat 套件，沿用旧流程）。第一期已删除 console 与 java-sdk-demo 的 clone/构建/测试脚本（`console_ci_test.sh`、`java_sdk_demo_ci_test.sh`）；第二期删除 java-sdk 与 JDK（spec `docs/superpowers/specs/2026-10-12-ops-explorer-tui-governance-native-ci.md` §5.4.4）。`ci_check_pro.sh` / `ci_check_baseline.sh` 仍引用已删的 `console_ci_test.sh`，但 workflow 不再调用它们，第二期一并删除。

## 四条链

| 链 | 节点 | build_chain | 端口（`CI_PORT_BASE` 默认 `30300,20200,8545`） | 场景 |
|---|---|---|---|---|
| nonsm | 3 + 扩容 node3 | 默认 | p2p/rpc/web3 = base；扩容 +3/+3/+1 | 1 2 4 5 8 9 7 13 14 19 20 |
| sm | 2 + 扩容 node2 | `-s` | base + 100 | 1 2 4 5 8 9 7 14 19（13 跳过：只有 3 个 sealer）|
| v1 | 2 + 扩容 node2 | genesis `executor version=1`，node0 开 web3 | base + 200 | 1 2 4 5 8 9 7 14（13 跳过；`web3_test.sh` 在 Java 阶段的 v1 链最后，不变） |
| auth | 2 + 扩容 node2 | `-a <fisco-bcos account new>` | base + 300 | 1 2 4 5 8 9 7 14（13 跳过；5、7、14 与 8/9 的治理写走提案；5 断言 `--direct` 为 NO_AUTHORIZED） |

每条链一个后台子 shell，日志在 `logs/<chain>.log`；任一链失败，主脚本打印该链日志尾 200 行、每个节点的 error/warn 行与 nohup，退出 1。ops 命令通过 `--node-dir` 走 `node0/data/fisco-bcos.ipc`，与 RPC 端口的 TLS 无关（CI 链保持 `enable_ssl=true`）。

## java-sdk 套件为何仍串行跑在默认端口

`java_sdk_ci_test.sh` 把 `nodes/127.0.0.1/sdk` 的证书拷进配置，并把 `peers` 固定成 `127.0.0.1:20200`（`java_sdk_ci_test.sh:86`）。它没有端口参数，所以不能并行跑在四条链上：原生阶段清链后，`java_legacy_air.sh` 在 20200 上依次起 nonsm / sm / v1 三条链跑 java-sdk 用例；v1 链最后跑 `web3_test.sh`，它签名用 build_chain 生成的 `nodes/ca/accounts` 管理员 pem，充值（200 ether）与 `tx_gas_price 0x1` 改由 `balance enable` / `balance add` / `sysconfig set` 经 node0 的 socket 完成（过去是 console `addBalance` / `setSystemConfigByKey`）。这段在第二期随 java-sdk 退场一起删除。

## 本机运行

```
mkdir -p /private/tmp/ci-run && cd /private/tmp/ci-run          # 路径要短：socket 路径上限 104 字节
FISCO_BCOS_BIN=/path/to/build/fisco-bcos-air/fisco-bcos \
CI_PORT_BASE=31300,21200,9545 RUN_JAVA=false \
bash /path/to/FISCO-BCOS/tools/.ci/ci_check_air.sh release-3.18.0 false
```

| 变量 | 作用 |
|---|---|
| `FISCO_BCOS_BIN` | 被测二进制，默认 `../build/fisco-bcos-air/fisco-bcos`（相对 `tools/`） |
| `CI_PORT_BASE` | nonsm 链的 `p2p,rpc,web3` 起始端口；其余链各加 100 |
| `RUN_JAVA` | `false` 跳过 Java 阶段（只限本机；CI 保持默认 `true`） |
| `CI_CHAINS` | 只跑子集，如 `"nonsm auth"`（本机调试） |
| `CI_KEEP_NODES` | `true` 跑完不清链、节点继续运行（本机调试） |
| `CI_FAIL_SCENARIO` | 在场景 n 之后注入失败，验证失败报告路径（本机调试） |

SM 链需要 `~/.fisco/tassl`；`jq` 必装；bash ≥ 4（`declare -A`、`${x,,}`，macOS 自带的 3.2 不行，用 Homebrew bash）。四条链的 build_chain 串行执行（并发会在 tassl 下载/安装与 `/tmp/secp256k1.param` 上互相踩），之后并行启动。清链先停 `nodes/127.0.0.1/stop_all.sh`（workflow 前一步 `rpcapi_ci_prepare.sh` 留下的四个节点占着 30300/20200），再只杀本工作目录 `nodes/` 下启动的进程，不会碰同机的其他链。

## 合约产物 `contracts/`

`contracts/<Name>/` 放 `<Name>.sol`（及其 import 的全部源文件，每个文件头注明来源仓库、路径、commit、许可）、`<Name>.bin`（keccak256 solc）、`<Name>.sm.bin`（sm3 solc）、`<Name>.abi`。`regenerate.sh` 用钉死的 solc 0.8.11（`SOLC_DIR` 默认 `~/.fisco/solc/<版本>/{keccak256,sm3}/solc`，console 的 `tools/download_solc.sh -v 0.8.11` 可取；脚本核对 `solc --version`）重编；`regenerate.sh --check` 编到临时目录与仓库里的产物逐字节比对，改了 `.sol` 或 `.bin` 的 PR 由 reviewer 跑一次。

| 合约 | 来源 | 用途 |
|---|---|---|
| `ContractTestAll`（含 CodeOp/Create2/DelegateCall/RecursiveDelegateCallTest/EventTest/LibraryTest/ProxyTest/TablePrecompiledTest/ECRecoverTest/ECRecoverSMTest/DeployTreeTest/Balance*/StaticCall 等 20 个文件） | java-sdk-demo `65e9cbd9` `src/main/java/org/fisco/bcos/sdk/demo/contract/sol/` | 场景 9；`.sm.bin` 由 `regenerate.sh` 把 `ECRecoverTest` 换成 `ECRecoverSMTest` 后编出（console CI 过去用 sed 做同样的事） |
| `HelloWorld` | console `fc2a37b0` `src/main/resources/contract/solidity/HelloWorld.sol` | 场景 2 的外部 ABI 路径；二进制内嵌的 HelloWorld 仍给 `tx smoke` 用 |

两点说明：console 的 `build.gradle` 钉的是 `solcJ:1.0.0`，CI 脚本里把它 sed 成 0.6.10 的那行早已匹配不到；这些源码在 0.6.10 下也编不过（`address payable[]` 字面量），所以统一用 0.8.11。solc 不把隐式构造函数写进 ABI，而 `tx deploy` 背后的 cpp-sdk 编码器要有这一项，`scenarios/common.sh` 的 `contract_abi` 在 `$TMPDIR` 里写一份追加了空构造函数的副本给命令用，仓库里的 `.abi` 保持 solc 原样。

## 对照表

| Java 仓库 | 测试类.方法:行 | 断言 | 原生函数 | 原生断言 | 备注 |
|---|---|---|---|---|---|
| console | `ConsoleClientTest.clientNotParamsTest:23-94` | getBlockNumber/getPbftView/getSealerList/getObserverList/getSyncStatus/getConsensusStatus/getPeers/getPendingTxSize/getTotalTransactionCount/getGroupList/getGroupPeers/getGroupInfo/getGroupInfoList 不抛异常且输出非空 | `scenario_1_rpc_shape` (`scenarios/rpc.sh`) | `status --json` 五项 check 全 ok；13 个方法经 `attach` 各一条 `jq` 类型/非空断言 | 四链 |
| console | `ConsoleClientTest.clientWithParamsTest:96-117` | deploy HelloWorld、set、get 返回设置值 | `scenario_2_hello` (`scenarios/hello.sh`) | `tx smoke` 三步 ok、`get` 等于 set 值；两笔交易 `tx get` 回执 status 0、`to` 等于合约地址；再用 `contracts/HelloWorld` 的 bin/abi 走 `tx deploy` / `tx send set` / `tx call get`，decoded 含设置值 | 四链 |
| java-sdk | `BcosSDKTest.testHelloWorldInSolidity` | 同上 | `scenario_2_hello` | 同上 | |
| java-sdk | `AssembleTransactionProcessorTest` HelloWorld 用例 | 组装/发送/回执解析 | `scenario_2_hello` | 同上 | 纯 SDK 组装逻辑不替代 |
| console | `ConsoleClientTest.clientWithParamsTest:143-205` | getBlockByNumber/getBlockByHash（含 onlyHeader/onlyTxHash）、getTransactionByHash、getTransactionReceipt、getCode、getABI | `scenario_4_block_tx` (`scenarios/block.sh`) | `block get <n>` 与 `block get <hash> --txs` 高度/哈希互查一致；header-only 无 `transactions`；`--proof` 每笔带 txProof/receiptProof；`tx get --proof`；`getCode` 以 `0x` 开头、`getABI` 可解析 | 四链 |
| java-sdk | `BcosSDKTest.testClient` | 同上 | `scenario_4_block_tx` | 同上 | |
| console | `ConsoleClientTest.clientWithParamsTest:211-215` | getSystemConfigByKey tx_gas_limit ≥ 0 | `scenario_5_sysconfig` (`scenarios/sysconfig.sh`) | `sysconfig set` 后 `get` 读回值与 `enableNumber` | 四链 |
| java-sdk | `PrecompiledTest.test3SystemConfigService:160-195` | setValueByKey tx_count_limit / tx_gas_limit(当前值+1) 后读回 | `scenario_5_sysconfig` | 同上；auth 链 `--direct` 为 `NO_AUTHORIZED` 退出 2，提案路径 `mode=proposal`、`statusName=passed`、`executed=true` | auth 链不改 tx_gas_limit，见下 |
| java-sdk-demo | `java_sdk_demo_ci_test.sh contract_test:227-254` | console `setSystemConfigByKey feature_balance 1`、`feature_balance_precompiled 1`、`addBalance <account> 10000000000` | `scenario_8_balance` (`scenarios/balance.sh`) | `balance enable` 后 `sysconfig features` 两个 flag 为 true；`balance add 1000` → `get` 1000 → `transfer 300` → 700（治理账户 ≥ 300）→ `sub 200` → 500；`list-caller` 含治理账户；非 caller 的 `add` 退出 2、status ≠ 0；auth 链再由 governor `register-caller` 新账户 → `list-caller` 含它 → 该账户 `add` 成功 → `unregister-caller` → 不再在列表 | 四链；治理账户 = auth 链的 `-a` 账户 / 其他链 build_chain 生成的 `ca/accounts` 管理员（创世 `auth_admin_account`，开启 feature 时登记为 caller）；auth 链两次写走提案 |
| — | 旧 `ci_check_air.sh:402-427` console `addBalance <account> 200 ether`、`setSystemConfigByKey tx_gas_price 1` | web3 测试账户有余额、gas price 为 1 | `java_legacy_air.sh` web3 段的 `ops_with_retry balance enable / balance add / sysconfig set tx_gas_price 0x1` | 命令退出 0（三次重试） | 已切换；v1 链 |
| console | `WelcomeInfoTest` 等 console 自身的单测 | console 启动/欢迎页 | 不替代 | — | console 自测，不在节点 CI 范围 |
| java-sdk | `HashCalculatorTest` | keccak / sm3 哈希计算 | 不替代 | — | SDK 自测，不在节点 CI 范围 |
| java-sdk | `AssembleTransactionWithRemoteSignProcessorTest` | 远程签名组装交易 | 不替代 | — | SDK 自测，不在节点 CI 范围 |
| java-sdk | `ContractTest.testV2Contract` | 合约包装类 v2 代码生成 | 不替代 | — | SDK 自测，不在节点 CI 范围 |
| java-sdk | `EventDecodeTest` / `TransactionDecoderServiceTest` 的纯解码部分 | 事件 / 交易输入解码 | 不替代 | — | SDK 自测，不在节点 CI 范围；链上部分由场景 10（票 11）覆盖 |
| java-sdk-demo | `java_sdk_demo_ci_test.sh check_all_contract:179-213` | console `deploy ContractTestAll`（SM 链换 ECRecoverSMTest）、区块 gasUsed ≠ 0、`addBalance <contract> 10000000`、`call ContractTestAll check` status 0；`contract_test` 在 `feature_dmc2serial` 开启后再跑一遍 | `scenario_9_contract_test_all` (`scenarios/contracts.sh`) | `tx deploy contracts/ContractTestAll/ContractTestAll[.sm].bin` status 0、部署块 gasUsed > 0、`balance add <contract> 10000000` ok、`tx send check` status 0 且 `logEntries` 非空（14 个子合约各发 Info 事件）、所在块 gasUsed > 0；`sysconfig set feature_dmc2serial 1` 后同样一轮 | 四链；deploy/check 用 `--timeout 180000`（check() 烧约 7e8 gas，auth 链 15 s 默认超时不够） |
| java-sdk | `PrecompiledTest.test1ConsensusService:77` | addObserver / addSealer / removeNode 后 getSealerList / getObserverList 变化，错误码 | `scenario_7_consensus_membership` (`scenarios/topology.sh`) | 扩容节点：不在列表时 `add-sealer` → `SEALER_MUST_BE_OBSERVER_FIRST`(-51104)、坏 id → `INVALID_NODE_ID`(-51100)；`add-observer` → `consensus list` 为 observer → `add-sealer 1` → sealer 且 `tx smoke` 仍出块 → 对已是 sealer 的节点 `add-sealer 2` 为原地改权重（code 0，ConsensusPrecompiled.cpp addSealer）→ `remove` → 不在列表 → `add-observer` → `add-sealer` 回到 sealer | 四链；auth 链每步 `mode=proposal` 且 `executed=true`，负码取 `proposal.execCodeName` |
| — | 新增 | — | `scenario_13_viewchange` (`scenarios/topology.sh`) | nonsm（3 sealer + 扩容 sealer = 4，停 1 个仍有 quorum 3）：`status` 全 ok → 由 `getConsensusStatus.leaderIndex` 找到 leader 节点目录并 `stop.sh` → 存活节点 `status` 轮询到 `leaderIndex` 变化且 `inTimeout=false`（本机实测 2 s；`consensus_stable fail` 的窗口短于 1 s 轮询，看到则打印、不断言）→ `log viewchange` 含 `reason=consensus_timeout`、`toView` 大于停前 view、`newLeader` 非空的一轮 → `tx smoke` 仍出块 → `start.sh` 后该节点 `status` 全 ok 且高度 ≥ 停前 | 2 节点链打印 `skip: needs 3 sealers…` 返回 0 |
| — | 新增 | — | `scenario_14_sync` (`scenarios/topology.sh`) | 扩容节点先 `add-observer`（降为 observer，2 节点链停一个 sealer 会停链）→ `stop.sh` 并删 `data/`、`log/` → node0 上部署 HelloWorld + 19 笔 `set` → `start.sh` → 第一次能答的 `status`：`sync_caught_up` 为 `fail`（本机 0 s 时高度 14/58；追得太快时接受 ok/skipped 并打印）→ 轮询到 `sync_caught_up ok`、`sync.lag == 0`、高度 ≥ 发交易时的高度（本机 16 s） | 四链；节点结束时保持 observer |
| java-sdk-demo | `check_all_dmc_transfer`（`RUN_DMC=true` 才跑，CI 从不开） | DMC 转账压测 | 不替代 | — | 性能用例，不在节点正确性 CI 范围 |
| — | 新增（替代 console `get_account.sh` / `get_gm_account.sh`） | — | `scenario_19_account` (`scenarios/account.sh`) | `account new [--sm]` 两个文件存在；用它 `tx smoke` 成功；`tx get` 的 `from` 等于打印地址 | nonsm / sm |
| — | 现有 `ops_tool_test` | — | `scenario_20_ops_status` (`scenarios/ops.sh`) | `tx smoke`、`status` 五项 ok、`log status` 来源为 log、除 `consensus_stable` 外无 `state=fail` 的 check、高度与 RPC 相差 ≤ 2、`tui --version-check` | nonsm；`log status` 退出 2 只能来自 `consensus_stable`（空闲链 viewchange，见下）；spec 里的「停节点前后 `log status`」与 `tui --snapshot-dir` 全页渲染待 TUI 改版（票 13）后补入 |

场景 7/10/11/12/13/14/15/16/17/18 与 Java 退场的第二期见后续票（spec §5.4.1、§5.4.4）。SDK 自测用例（`HashCalculatorTest`、`AssembleTransactionWithRemoteSignProcessorTest`、`ContractTest.testV2Contract`、`EventDecodeTest` / `TransactionDecoderServiceTest` 的纯解码部分、`WelcomeInfoTest`）不替代，第二期补入本表。

## 跑本套件时发现的节点侧问题（未修，记录）

1. **auth 链经提案改 `tx_gas_limit` 后所有 EVM `call` 返回 status 12 "Execution out of gas."**：`sysconfig set tx_gas_limit 3000000001`（以及 2999999999、3300000000）走 `createSetSysConfigProposal` 执行成功、`getSystemConfigByKey` 读回新值，之后 `call` HelloWorld `get()` 与 CommitteeManager `_proposalMgr()` 都失败，异常抛自 `transaction-executor/bcos-transaction-executor/vm/HostContext.h:843`（`consumeTransferGas`：`ref.gas < BALANCE_TRANSFER_GAS`），交易（`tx smoke`）仍正常；非 auth 链直接 `setValueByKey` 同样的值不复现。场景 5 在 auth 链上因此只改 `tx_count_limit`。
2. **空闲链每隔几秒触发 consensus_timeout 的 viewchange**（`ViewChangeTriggered,reason=consensus_timeout` → `NewViewReached`），`log status` 的 `consensus_stable` 若恰好读在两行之间为 fail、命令退出 2；场景 20 只允许这一项 fail（主干 #5643 的 freshTimer 修复不在本分支）。
3. 四条链同时启动时 `start.sh` 的 4 秒就绪检查会打印 `Exceed waiting time`；在节点初始化中途 stop/start 会让 PBFT 停在 `ViewChangeRejected,reason=committed_conflict`。脚本改为轮询进程与 RPC 就绪，不再中途重启。
