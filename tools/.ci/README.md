# Air 集成测试：原生场景与 Java 用例对照

`ci_check_air.sh` 用分支自己的 `fisco-bcos` 二进制起四条链并行跑 `scenarios/*.sh` 里的原生场景（ops 子命令 + `jq` 断言）。Java 套件已全部退场：第一期删了 console 与 java-sdk-demo 的 clone/构建/测试脚本（`console_ci_test.sh`、`java_sdk_demo_ci_test.sh`），第二期删了 java-sdk（`java_sdk_ci_test.sh`、`java_legacy_air.sh`）、workflow 的三处 `setup-java`，以及 workflow 早已不调用的 `ci_check_pro.sh` / `ci_check_baseline.sh`（spec `docs/superpowers/specs/2026-10-12-ops-explorer-tui-governance-native-ci.md` §5.4.4）。CI 不再装 JDK、不再 clone 任何 Java 仓库；`web3_test.sh`（Hardhat，不是 Java）仍在 v1 链上跑。

## 四条链

| 链 | 节点 | build_chain | 端口（`CI_PORT_BASE` 默认 `30300,20200,8545`） | 场景 |
|---|---|---|---|---|
| nonsm | 3 + 扩容 node3 | 默认 | p2p/rpc/web3 = base；扩容 +3/+3/+1 | 1 2 4 5 8 9 7 13 14 10 11 15 16 12 19 20 17 |
| sm | 2 + 扩容 node2 | `-s` | base + 100 | 1 2 4 5 8 9 7 14 10 11 15 16 12 19 17（13 跳过：只有 3 个 sealer）|
| v1 | 2 + 扩容 node2 | genesis `executor version=1`，node0 开 web3 | base + 200 | 1 2 4 5 8 9 7 14 10 11 15 16 12 web3 17（13 跳过；第二个参数为 `true` 时 `web3_suite` 在 17 之前跑 `web3_test.sh`，监听端口 = base web3 + 200，经 `WEB3_PORT` 传给脚本） |
| auth | 2 + 扩容 node2 | `-a <fisco-bcos account new>` | base + 300 | 1 2 4 5 8 9 7 14 10 11 15 16 12 18 17（13 跳过；5、7、14、17、18 与 8/9 的治理写走提案；5 断言 `--direct` 为 NO_AUTHORIZED；15 不改 tx_gas_limit；18 结束时把投票比例还原成 0/0、部署权限恢复开放） |

每条链一个后台子 shell，日志在 `logs/<chain>.log`；任一链失败，主脚本打印该链日志尾 200 行、每个节点的 error/warn 行与 nohup，退出 1。ops 命令通过 `--node-dir` 走 `node0/data/fisco-bcos.ipc`，与 RPC 端口的 TLS 无关；只有 AMOP（场景 12）走 WebSocket RPC，所以 `build_chain_for` 把每条链 node0 的 `[rpc] enable_ssl` 改成 false、`listen_ip` 改成 127.0.0.1。扩容节点由 `expand_node` 复制 node0 的 config.ini（`build_chain.sh:2049`），因此也是明文、只监听回环；其余初始节点保持 TLS。

## web3（Hardhat）套件

`web3_test.sh` 不是 Java，保留：`ci_check_air.sh` 的 `web3_suite` 在 v1 链跑完场景 16/12 之后、17 之前调用它。此时 v1 链是场景 1–16 留下的状态：场景 8/11 开了 `feature_balance` / `feature_balance_precompiled`，场景 9 开了 `feature_dmc2serial`；17 会把全部 feature（含 corpus 没写过的新 EVM 分叉）打开，所以放在它之前。签名账户是 build_chain 生成到 `ca/accounts` 的管理员 pem；充值 200 ether 与 `tx_gas_price 0x1` 由 `balance enable` / `balance add` / `sysconfig set` 经 node0 的 socket 完成（过去是 console `addBalance` / `setSystemConfigByKey`，三次重试）。金额是纯数字串 `200000000000000000000`：`typedArg`（`bcos-ops/tx/PresetCall.cpp`）把整数参数按十进制文本交给编码器，不需要引号。v1 链的 web3 端口是 `CI_PORT_BASE` 的第三项 + 200（默认 8745），通过 `WEB3_PORT` 传给 `web3_test.sh`（它原来写死 8545）。

## 本机运行

```
mkdir -p /private/tmp/ci-run && cd /private/tmp/ci-run          # 路径要短：socket 路径上限 104 字节
FISCO_BCOS_BIN=/path/to/build/fisco-bcos-air/fisco-bcos \
CI_PORT_BASE=31300,21200,9545 \
bash /path/to/FISCO-BCOS/tools/.ci/ci_check_air.sh release-3.18.0 false   # 第二个参数 true 才跑 web3
```

| 变量 | 作用 |
|---|---|
| `FISCO_BCOS_BIN` | 被测二进制，默认 `../build/fisco-bcos-air/fisco-bcos`（相对 `tools/`） |
| `CI_PORT_BASE` | nonsm 链的 `p2p,rpc,web3` 起始端口；其余链各加 100 |
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
| `ComplexSol` | java-sdk `e5ec9236` `src/test/resources/contracts/ComplexSol.sol` | 场景 10：int/string 构造参数、动态 address[]、bytes、静态 bytes4、bytes[] 映射、多返回值、五个事件 |
| `Payable` | 本套件自写（`contracts/Payable/Payable.sol`），ABI 与 java-sdk `e5ec9236` 的 `PayableTest.java` 内嵌合约相同（该仓库没有它的 .sol） | 场景 11：`constructor() payable`、`incWithPayable(string) payable`、非 payable 的 `inc(string)`、`value()` |
| `Reverter` | 本套件自写（`contracts/Reverter/Reverter.sol`） | 场景 16：`fail()` 带原因 `require`、`failNoReason()` 裸 `revert()`；场景 15 也用它做准入探针 |

两点说明：构造参数编码与隐式构造函数的两个工具侧问题已修（见下文「工具侧问题」）。console 的 `build.gradle` 钉的是 `solcJ:1.0.0`，CI 脚本里把它 sed 成 0.6.10 的那行早已匹配不到；这些源码在 0.6.10 下也编不过（`address payable[]` 字面量），所以统一用 0.8.11。仓库里的 `.abi` 是 solc 原样输出，`scenarios/common.sh` 的 `contract_abi` 直接返回它。

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
| — | 旧 `ci_check_air.sh:402-427` console `addBalance <account> 200 ether`、`setSystemConfigByKey tx_gas_price 1` | web3 测试账户有余额、gas price 为 1 | `ci_check_air.sh` `web3_suite` 的 `balance enable / balance add / sysconfig set tx_gas_price 0x1` | 命令退出 0（三次重试），然后 `web3_test.sh` 退出 0 | v1 链，17 之前 |
| console | `WelcomeInfoTest` 等 console 自身的单测 | console 启动/欢迎页 | 不替代 | — | console 自测，不在节点 CI 范围 |
| java-sdk | `HashCalculatorTest` | keccak / sm3 哈希计算 | 不替代 | — | SDK 自测，不在节点 CI 范围 |
| java-sdk | `AssembleTransactionWithRemoteSignProcessorTest` | 远程签名组装交易 | 不替代 | — | SDK 自测，不在节点 CI 范围 |
| java-sdk | `ContractTest.testV2Contract` | 合约包装类 v2 代码生成 | 不替代 | — | SDK 自测，不在节点 CI 范围 |
| java-sdk | `EventDecodeTest` / `TransactionDecoderServiceTest` 的纯解码部分 | 事件 / 交易输入解码 | 不替代 | — | SDK 自测，不在节点 CI 范围；链上部分由场景 10（票 11）覆盖 |
| java-sdk-demo | `java_sdk_demo_ci_test.sh check_all_contract:179-213` | console `deploy ContractTestAll`（SM 链换 ECRecoverSMTest）、区块 gasUsed ≠ 0、`addBalance <contract> 10000000`、`call ContractTestAll check` status 0；`contract_test` 在 `feature_dmc2serial` 开启后再跑一遍 | `scenario_9_contract_test_all` (`scenarios/contracts.sh`) | `tx deploy contracts/ContractTestAll/ContractTestAll[.sm].bin` status 0、部署块 gasUsed > 0、`balance add <contract> 10000000` ok、`tx send check` status 0 且 `logEntries` 非空（14 个子合约各发 Info 事件）、所在块 gasUsed > 0；`sysconfig set feature_dmc2serial 1` 后同样一轮 | 四链；deploy/check 用 `--timeout 180000`（check() 烧约 7e8 gas，auth 链 15 s 默认超时不够） |
| java-sdk | `PrecompiledTest.test1ConsensusService:77` | addObserver / addSealer / removeNode 后 getSealerList / getObserverList 变化，错误码 | `scenario_7_consensus_membership` (`scenarios/topology.sh`) | 扩容节点：不在列表时 `add-sealer` → `SEALER_MUST_BE_OBSERVER_FIRST`(-51104)、坏 id → `INVALID_NODE_ID`(-51100)；`add-observer` → `consensus list` 为 observer → `add-sealer 1` → sealer 且 `tx smoke` 仍出块 → 对已是 sealer 的节点 `add-sealer 2` 为原地改权重（code 0，ConsensusPrecompiled.cpp addSealer）→ `remove` → 不在列表 → `add-observer` → `add-sealer` 回到 sealer | 四链；auth 链每步 `mode=proposal` 且 `executed=true`，负码取 `proposal.execCodeName` |
| — | 新增 | — | `scenario_13_viewchange` (`scenarios/topology.sh`) | nonsm（3 sealer + 扩容 sealer = 4，停 1 个仍有 quorum 3）：`status` 全 ok → 由 `getConsensusStatus.leaderIndex` 找到 leader 节点目录并 `stop.sh` → 存活节点 `status` 轮询到 `leaderIndex` 变化且 `inTimeout=false`（本机实测 1–13 s；`consensus_stable fail` 的窗口短于 1 s 轮询，看到则打印、不断言）→ `log viewchange` 含 `reason=consensus_timeout` 或 `f_plus_one_higher_view`（`PBFTEngine.cpp:1617` 两种 reason 都发 ViewChangeTriggered）、`toView` 大于停前 view、`newLeader` 不是停掉的 leader 的一轮 → `tx smoke` 仍出块且存活节点高度前进 → `start.sh` 后该节点 `status` 全 ok 且高度 ≥ 存活节点 smoke 后的高度 | 2 节点链打印 `skip: needs 3 sealers…` 返回 0 |
| — | 新增 | — | `scenario_14_sync` (`scenarios/topology.sh`) | 扩容节点先 `add-observer`（降为 observer，2 节点链停一个 sealer 会停链）→ `stop.sh` 并删 `data/`、`log/` → node0 上部署 HelloWorld + 19 笔 `set` → `start.sh` → 目标高度取发交易前后两次 `getBlockNumber`，差必须 ≥ 20 → 第一次能答的 `status`：`sync_caught_up` 为 `fail`（本机 0–1 s 时高度 14–21 / 56–59；追得太快时只接受已到目标高度的 ok/skipped）→ 轮询到 `sync_caught_up ok`、`sync.lag == 0`、高度 ≥ 目标（本机 10–26 s） | 四链；节点结束时保持 observer |
| java-sdk | `AssembleTransactionProcessorTest` ComplexSol 用例（部署 `(int,string)`、`_intV`/`_s`、`emptyArgs`、`incrementUint256`、`setValues`/`getValues`、`setBytes`、`setStaticByte4`）、`CodecTest`/`CodecComplexTest` 的链上部分 | 构造参数回读、返回值解码、事件解析 | `scenario_10_complex_abi` (`scenarios/contracts2.sh`) | 部署 ComplexSol(7, "init s") 回执 1 条 `LogInit`；`_intV` = 7、`_s` = "init s"；`incrementUint256(41)` 输出 42、`LogIncrement` topic、`tx get` 回执带同一条 log 与 data、`getUint256` = 42；`setValues(-3,[a1,a2],"vals")` → `LogSetValues`、`getValues` 三个值逐一相等；`setBytes(hex://abcdef)` 输出含 3 字节、`LogSetBytes`、`_bytesV` 读回；`setStaticByte4(hex://deadbeef)` 返回 true、`_bytes4V` 读回；`setBytesMapping([0102,0304])` → `getByBytes(0102)` 读回数组；`getSArray` 输出 `[1,2]` | 四链；topic[0] 在 keccak 链上断言等于事件签名的 keccak256（脚本内常量），SM 链上 topic 是 sm3、runner 上没有 sm3 工具，只断言形状；`CodecComplexTest` 的 struct 用例（tuple 参数）不在本工具的签名编码范围，不替代；java-sdk `e5ec9236` 里已没有 EventDemo.sol，事件断言落在 ComplexSol 的五个事件与 Payable 的 `incEvent` 上 |
| java-sdk | `TransactionManagerPayableTest.testPayableTestWithSDKInterface` / `testPayableTestWithCodeWrapper` | 带 value 部署、payable 方法带 value、非 payable 方法带 value 失败、合约余额 = deploy + set | `scenario_11_payable` (`scenarios/contracts2.sh`) | 治理账户 `balance add` 给新账户 1000 → `tx deploy Payable --value 10` → `balance get` 合约 10、账户 990 → `tx send incWithPayable --value 5` 返回 1、`incEvent` topic → 合约 15、账户 985、`value()` = 1 → `tx send inc --value 5` status ≠ 0 且合约仍 15 | 四链；SM 链的付款账户用 `account new --sm` |
| — | 新增 | — | `scenario_15_admission` (`scenarios/admission.sh`) | `tx send --block-limit <高度-1>` → `{"error":{"code":10001,"message":"BlockLimitCheckFail"}}` 退出 2；同一 `--nonce` 第二次 → 10000 `NonceCheckFail`；`sysconfig set tx_gas_limit 100000`（节点接受的最小值）后部署 ComplexSol → status ≠ 0 且 gasUsed ≤ 100000，`Reverter.ok()` 仍 status 0，恢复后同一部署 status 0；扩容节点改 `[txpool] limit=1` 重启、六笔签名交易同时 `tx raw` → 至少一笔 `10002 TxPoolIsFull`（其余 status 0 或 `10010 TransactionPoolTimeout`），配置恢复后重启 | 四链；auth 链跳过 tx_gas_limit 段（见下「节点侧问题」1）。错误码来源：`bcos-protocol/bcos-protocol/TransactionStatus.h:55-57`（10000/10001/10002）、`:88` 的 10023 对 tars 交易不可达（`bcos-tx-validator` CheckSet 的 Bcos 列没有 MaxGasLimit），超 gas 的创建在执行期以回执 status 12 `OutOfGas` 失败（本机 nonsm/sm/v1 实测；`TransactionStatus.h:38`），10010 见 `TransactionStatus.h` `TransactionPoolTimeout` |
| — | 新增 | — | `scenario_16_revert` (`scenarios/contracts2.sh`) | `Reverter.ok()` status 0 → `fail()` status 16、output 以 `Error(string)` 选择子开头（keccak 链 `0x08c379a0`，SM 链 `0xc703cb12`：sm3 版 solc 连选择子也用 sm3）、解出的原因为 "reverted on purpose"、`tx get` 回执同形 → `failNoReason()` status 16、output `0x` → `counter` 仍为 1 | 四链；`tx send --json` 的回执 `message` 为空、原因由脚本从 output 解码（预设命令走 `revertReason()`，`tx` 命令还没有） |
| — | 新增 | — | `scenario_17_features` (`scenarios/features.sh`) | `sysconfig features` 列出全部 flag → 把所有未开启、未废弃的 flag 一次交给 `sysconfig enable-features`（`featureEnableOrder` 先排依赖、已开的记为 skipped；auth 链每个 key 一个提案）→ 每步 enabled/skipped、`feature_balance` 在 `feature_balance_precompiled` 前、后者在 `feature_balance_policy1` 前 → 读回无关闭 flag → 收尾 `tx smoke` 证明链仍出块 | 四链，各链最后一个场景（全开后链行为改变）；任一 flag 被拒即失败（历次实测无拒绝） |
| java-sdk-demo | `check_all_dmc_transfer`（`RUN_DMC=true` 才跑，CI 从不开） | DMC 转账压测 | 不替代 | — | 性能用例，不在节点正确性 CI 范围 |
| java-sdk | `AmopTest`（subscribe / publish / unsubscribe） | 订阅后能收到发布的消息，取消订阅后发布报错 | `scenario_12_amop` (`scenarios/amop.sh`) | 后台 `amop sub <topic> --count 1 --wait 30`，等 stderr 的 `subscribed <topic>`，`amop pub <topic> <msg>` 返回 `ok=true` 且 `response` 为订阅方回显的同一消息；sub 退出 0 且输出 `{"topic","data"}` 为该消息；无订阅者的 topic `pub` 退出 2、`error.code == 3001`（`CommonError::NotFoundPeerByTopicSendMsg`）；订阅方退出后再 `pub` 同样 3001 | 四链；node0 的 `[rpc] enable_ssl=false` |
| — | 新增（CI 从未起过 auth 链） | — | `scenario_18_auth_governance` (`scenarios/governance.sh`) | `committee info` 单 governor（`-a` 账户）、比例 0/0 → `auth set-deploy-auth-type 1` 自动提案并执行、`auth deploy-type` 读 1 → `account new` 的账户 `tx smoke` 退出 2 `reason=auth_denied`、`has-deploy-auth` false → `auth open-deploy-auth` 提案执行 → `has-deploy-auth` true、同账户 smoke 成功、`get-admin` 为该账户 → 扩容节点（场景 14 后为 observer）`consensus add-sealer` 走提案执行、`consensus list` 为 sealer → `propose-governor <acct2> 1` 执行、`committee info` 两个 governor → `propose-rate 100 100` 执行 → `propose-sysconfig tx_count_limit 2500` 为 `voting`、`executed=false` → acct2 `vote <id> true` 后 `passed`、`executed=true`、`sysconfig get` 读 2500 → `proposals --last 5` 含该提案 passed → `propose-rate 0 0` + 第二票还原比例 → `set-deploy-auth-type 0` 恢复部署开放（白名单连 `-a` 管理员都拦，17 结尾的 smoke 要部署） | auth 链；放在 17 之前、19/20 不跑 auth |
| — | 新增（替代 console `get_account.sh` / `get_gm_account.sh`） | — | `scenario_19_account` (`scenarios/account.sh`) | `account new [--sm]` 两个文件存在；用它 `tx smoke` 成功；`tx get` 的 `from` 等于打印地址 | nonsm / sm |
| — | 现有 `ops_tool_test` | — | `scenario_20_ops_status` (`scenarios/ops.sh`) | `tx smoke`、`status` 五项 ok、`log status` 来源为 log、除 `consensus_stable` 外无 `state=fail` 的 check、高度与 RPC 相差 ≤ 2、`tui --version-check`；停 node3（observer）后 node0 的 `log status` identity 七个字段不变、高度不回退，node3 重启后 status 全 ok；`tui --snapshot-dir` 写出 12 页（home、home-wide、consensus、node、log、help、block、tx、send、send-confirm、gov、proposal）且均非空，非 auth 链 gov 页注明 not an auth chain | nonsm；`log status` 退出 2 只能来自 `consensus_stable`（空闲链 viewchange，见下） |

18 组场景全部落地，Java 两期退场完成（spec §5.4.4）。SDK 自测用例（`HashCalculatorTest`、`AssembleTransactionWithRemoteSignProcessorTest`、`ContractTest.testV2Contract`、`EventDecodeTest` / `TransactionDecoderServiceTest` 的纯解码部分、`WelcomeInfoTest`）不替代，见上表。

## 跑本套件时发现的节点侧问题（未修，记录）

1. **auth 链经提案改 `tx_gas_limit` 后所有 EVM `call` 返回 status 12 "Execution out of gas."**：`sysconfig set tx_gas_limit 3000000001`（以及 2999999999、3300000000）走 `createSetSysConfigProposal` 执行成功、`getSystemConfigByKey` 读回新值，之后 `call` HelloWorld `get()` 与 CommitteeManager `_proposalMgr()` 都失败，异常抛自 `transaction-executor/bcos-transaction-executor/vm/HostContext.h:843`（`consumeTransferGas`：`ref.gas < BALANCE_TRANSFER_GAS`），交易（`tx smoke`）仍正常；非 auth 链直接 `setValueByKey` 同样的值不复现。场景 5 在 auth 链上因此只改 `tx_count_limit`。
2. **空闲链每隔几秒触发 consensus_timeout 的 viewchange**（`ViewChangeTriggered,reason=consensus_timeout` → `NewViewReached`），`log status` 的 `consensus_stable` 若恰好读在两行之间为 fail、命令退出 2；场景 20 只允许这一项 fail（主干 #5643 的 freshTimer 修复不在本分支）。
3. 四条链同时启动时 `start.sh` 的 4 秒就绪检查会打印 `Exceed waiting time`；在节点初始化中途 stop/start 会让 PBFT 停在 `ViewChangeRejected,reason=committed_conflict`。脚本改为轮询进程与 RPC 就绪，不再中途重启。

4. **本机 Debug 构建跑 web3 套件时 node0 在 `test/exception/RecursiveCall-test.js` 崩溃**：macOS 崩溃报告为 `EXC_BAD_ACCESS / SIGBUS`「Could not determine thread index for stack guard region」（栈溢出，深递归调用在 Debug 帧大小下打穿线程栈），此前 52 个用例已通过、端口与充值无误。CI 的 Air 腿用的是 RelWithDebInfo 构建，旧流程在同一 corpus 上一直是绿的；本机验证 web3 段时要用 Release 构建的二进制。

## 工具侧问题（bcos-ops，已修）

1. cpp-sdk 的 `ContractABICodec::encodeConstructor` 给构造参数前面多塞 4 字节选择子（`bcos-sdk/bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h:269-277`、`ContractABICodec.cpp:111-116`），带构造参数的部署 revert（status 16）。`aa9550cd2` 让 `Abi::encodeConstructor` 不带选择子编码；场景 10、15 现在直接 `tx deploy ComplexSol.bin --abi ComplexSol.abi 7 "init s"`，手工拼接的参数常量与去掉构造函数的 ABI 副本已删除。
2. 失败回执的 `message` 为空。`aa9550cd2` 让 `tx send/get --json` 从 `Error(string)` output 解出原因填进 `message`（keccak `08c379a0`，SM 链 sm3 `c703cb12`）；场景 16 直接断言 `.message == "reverted on purpose"`，脚本内的 output 解码已删除。
3. `tx deploy` 曾要求 ABI 里有构造函数条目，而 solc 不写隐式构造函数。`85e24b2bd` 起没有条目时只发字节码；`contract_abi` 直接返回仓库里的 solc 原样 `.abi`，不再在 `$TMPDIR` 写副本。
