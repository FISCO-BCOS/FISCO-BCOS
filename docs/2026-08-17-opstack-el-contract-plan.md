# FISCO opstack OP-node EL 契约合规实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **版本:** v2 —— 2026-08-17 经 **Round 1 四子代理审查**(引擎 API / deposit tx / op-node 契约 / op-e2e 脚本)修订;每处修订标注来源(R1-A/B/C/D)。

**Goal:** 让 FISCO opstack 满足 OP-node 作为执行客户端的硬契约 —— 引擎 API 方法版本矩阵合规、payload/attributes 校验规则齐全、`eth_getBlockBy*` 的 deposit tx round-trip 正确,并搭出 op-node 集成 harness 的骨架。

**Architecture:** 分三段推进 —— (A) 引擎 API 契约合规矩阵:修正 `a1_active.py` 主路径到 op-node 真实版本(FCU V3)、实现 **FCU attrs.gasLimit 被采纳**(当前被丢弃,是块哈希分叉最大风险)、测试既有 newPayload 校验、补 forkchoice 状态机单测;(B) deposit tx round-trip:补 `TransactionResponse.cpp` 的 deposit 字段下发 + RPC 单测 + op-e2e 断言;(C) op-node 集成 harness(依赖外部,单独计划)。

**Tech Stack:** C++(bcos-rpc EngineEndpoint/TransactionResponse + Boost.Test 单测)、Python(op-e2e `a1_active.py`/`chain_driver.py`)、op-node/op-geth(参考契约 `op-node/rollup/types.go`、`op-geth/eth/catalyst/api_optimism.go`)。

## Global Constraints

- 测试不可退步:已通过 ctest(1935/1935)与 op-e2e(ALL OP-E2E GREEN)必须保持全绿;新增测试只增不改。
- ⚠️ **基线重建(R1-A)**:`bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp` 的 `forkchoiceUpdatedV4`/`getPayloadV4`/`newPayloadV4` 三个既有用例断言 `response["error"]["code"]==UnsupportedFork`,但当前 `MockOpEngineService` 返回 VALID 且端点不写 error —— 这三个用例对**当前源码是红的**(旧 build 二进制通过是因为它是 Aug 11 陈旧产物)。**Phase A 开始前必须先重建 `build/bcos-rpc/test/test-bcos-rpc` 并对齐这三个用例**(mock 抛 UnsupportedFork 或改断言为 VALID),否则"无退步"不可验证。
- `[executor] version=3` 为 OP 模式;引擎 API 仅在 `enableOPEngine` 时注册(`EndpointsMapping.cpp:42-44`)。
- **op-node 实际引擎版本**(R1-C,`op-node/rollup/types.go`):FCU V1(Pre-Canyon)/V2(Canyon)/V3(Ecotone+,**Isthmus+ 也是 V3,连 nil attrs 都发 V3**);NewPayload V2/V3(V4=Isthmus+);GetPayload V2/V3/V4(Isthmus)/V5(Karst)。**不存在 FCU V4 被 op-node 调用**。
- 校验错误码(仅列 op-geth 真实存在的):`-38002 InvalidForkchoiceState`、`-38003 InvalidPayloadAttributes`(gasLimit 缺失、attrs.timestamp<=head)、`-38005 UnsupportedFork`(newPayload/getPayload 版本门)、`-32602 InvalidParams`。**无 `-38006`**(R1-C:那是 op-node 内部 sync 错误,非引擎码)。
- ⚠️ **FCU V1/V2 不做版本门测试**(R1-A):op-node 在 Ecotone+ 永不发 FCU V1/V2(恒 V3);FISCO `updateForkchoice` 无 fork×version 门也不应加。op-geth 的 FCU V1/V2 门是标准以太坊门(仅 attrs 非空时:V1 post-Shanghai→`-32602`、V2 post-Cancun→`-38005`),与 OP 契约无关。
- **FCU attrs.gasLimit 当前被丢弃**(R1-A):`parsePayloadAttributes` 从不读 `gasLimit`(`targetGasLimit` 恒 nullopt),`buildOpPayload` 用 ledger-config `tx_gas_limit`。op-node 发的 attrs.gasLimit 必须被采纳,否则块 gasLimit 不符 → 块哈希分叉。**这是 EL 契约最关键缺口,Task 3 处理(采纳,而非仅拒绝缺失)。**
- deposit tx 类型字节 `0x7E`;C++ 字段名为 **`isSystemTx`**(非 `isSystemTransaction`,R1-B);函数名 **`combineTxResponse`**(非 `combineTransactionResponse`)。`TransactionResponse.cpp:66-68` 现排除 deposit 的 EIP 字段输出且**无 deposit 专属字段**。
- ⚠️ **op-node 消费的是 `engine_getPayload` 原始 RLP 字节**(R1-C:`PayloadToBlockRef`/`PayloadToSystemConfig` 走 `tx.UnmarshalBinary` → `DepositTx.decode`,读 `tx.Data()` L1-info calldata);但 `eth_getBlockBy*` full-tx JSON 仍关键 —— op-node `RPCBlock` 会把 JSON 反序列化再 `MarshalBinary` 重编码校验 tx-root/block-hash(缺字段→块校验失败)。
- 所有 DIVERGENCE/负向分支必须断言「PASS(登记为预期)」而非 FAIL;脚本退出码 0 表示通过。

---

## File Structure

| 文件 | 责任 | 动作 |
|---|---|---|
| `tools/op-e2e/a1_active.py` | 引擎 API e2e 驱动(B3a) | 改:FCU V3 主路径 + getPayload V4;删 FCU V1/V2 门(不存在);加 capabilities 断言 |
| `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp` | `parsePayloadAttributes` | 改:解析 `gasLimit` → `attrs.targetGasLimit`(Task 3) |
| `engine/bcos-engine/EngineServiceImpl.h` | `updateForkchoice` OP 分支 | 改:attrs 校验(`-38003`)+ 采纳 gasLimit(若有)(Task 3) |
| `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp` | 引擎 RPC 单测 | 改:修 3 个 stale V4 用例 + 补 FCU V3 captured-version + `-38003` 用例(Task 1,3) |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp` | 交易 JSON 序列化 | 改:deposit 补 `sourceHash`/`mint`/`isSystemTx`(omitempty)(Task 5) |
| `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp` | RPC 响应单测 | 改:补 deposit tx 3 字段断言 + 扩 `combineTxResponseDepositMinimalFields`(Task 5) |
| `tools/op-e2e/chain_driver.py` | 交易驱动 + 块/回执断言 | 改:补 deposit tx[0] round-trip 断言(用回执 blk_num)(Task 6) |
| `tools/op-e2e/run_all.sh` | op-e2e 回归门 | 改:更新断言数注释(a1_active 16→18、chain_driver 31→39) |
| `tools/op-e2e/a1_active.py` docstring | 修正 "enable_single_node_consensus=false"→true(R1-D) | 改 |

**Phase C(harness)额外文件**(依赖外部,见 Task 7):`tools/op-e2e/l1_mock/`、`tools/op-e2e/opnode_driver.py`。

---

## Phase A — 引擎 API 契约合规矩阵

### Task 0: 基线重建与 stale V4 用例对齐(R1-A 前置)

**Files:**
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp`(`forkchoiceUpdatedV4`/`getPayloadV4`/`newPayloadV4`)

**Interfaces:**
- Consumes:`EngineRpcTestFixture`、`MockOpEngineService`、`CALL_ENGINE` 宏。
- Produces:可验证的基线 —— 三个 V4 用例与当前 mock/端点一致。

- [ ] **Step 1: 重建测试二进制**

Run:`cd build && cmake --build . --target test-bcos-rpc 2>&1 | tail -5`
Expected:构建成功。

- [ ] **Step 2: 运行引擎 RPC 单测确认基线红**

Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest 2>&1 | tail -20`
Expected:`forkchoiceUpdatedV4`/`getPayloadV4`/`newPayloadV4` 红(当前 mock 返回 VALID,用例断言 UnsupportedFork)。

- [ ] **Step 3: 对齐三个 V4 用例(择一)**

(a) 改 `MockOpEngineService::updateForkchoice/getPayload/newPayload` 在 `version>=4` 时抛 `UnsupportedFork`(与用例断言一致);或
(b) 改三个用例断言为 VALID(反映当前 mock 无版本门)。
以 (a) 为准:mock 应忠实反映"V4 版本支持由 `m_maxEngineVersion` 决定"——默认 `m_maxEngineVersion=V3`,故 V4 应抛 `UnsupportedFork`。

- [ ] **Step 4: 运行确认全绿**

Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest 2>&1 | tail -5`
Expected:全绿。

- [ ] **Step 5: Commit**

```bash
git add bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp
git commit -m "fix(rpc): align EngineRpcTest V4 cases with mock version gate (rebuild baseline)"
```

### Task 1: EngineRpcTest — FCU V3 主路径(captured-version)

**Files:**
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp`

**Interfaces:**
- Consumes:`EngineRpcTestFixture`、`MockOpEngineService`(`m_state->forkchoiceUpdatedResult`、`capturedForkchoiceVersion`)、`CALL_ENGINE`。
- Produces:用例 `forkchoiceUpdatedV3_captures_version_3`(断言 FCU V3 把 version=3 转发给引擎)、`forkchoiceUpdatedV3_with_attrs_valid`(种子 payloadId 后断言 VALID+payloadId)。

- [ ] **Step 1: 写测试(参照既有 `forkchoiceUpdatedV1` 的 captured-version 模式)**

```cpp
BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV3_captures_version_3)
{
    EngineRpcTestFixture f;
    Json::Value params(Json::arrayValue);
    Json::Value fcs;
    fcs["headBlockHash"] = "0x" + std::string(64, 'a');
    fcs["safeBlockHash"] = "0x" + std::string(64, 'a');
    fcs["finalizedBlockHash"] = "0x" + std::string(64, 'a');
    Json::Value attrs;
    attrs["timestamp"] = "0x10";
    attrs["prevRandao"] = "0x" + std::string(64, '0');
    attrs["suggestedFeeRecipient"] = "0x4200000000000000000000000000000000000011";
    attrs["gasLimit"] = "0x7a1200";
    params.append(fcs);
    params.append(attrs);
    Json::Value response;
    CALL_ENGINE(forkchoiceUpdatedV3, params, response);
    BOOST_CHECK_EQUAL(f.mockService.m_state->capturedForkchoiceVersion, 3);  // 见既有用例断言方式
    BOOST_CHECK_EQUAL(response["result"]["payloadStatus"]["status"].asString(), "VALID");
}

BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV3_with_attrs_valid_has_payloadId)
{
    EngineRpcTestFixture f;
    f.mockService.m_state->forkchoiceUpdatedResult.payloadId = "0x0000000021f32cc1";  // 默认 nullopt,须种子
    Json::Value params(Json::arrayValue);
    Json::Value fcs; fcs["headBlockHash"] = "0x" + std::string(64, 'a');
    fcs["safeBlockHash"] = "0x" + std::string(64, 'a'); fcs["finalizedBlockHash"] = "0x" + std::string(64, 'a');
    Json::Value attrs; attrs["timestamp"] = "0x10"; attrs["prevRandao"] = "0x" + std::string(64, '0');
    attrs["suggestedFeeRecipient"] = "0x4200000000000000000000000000000000000011";
    params.append(fcs); params.append(attrs);
    Json::Value response;
    CALL_ENGINE(forkchoiceUpdatedV3, params, response);
    BOOST_CHECK_EQUAL(response["result"]["payloadStatus"]["status"].asString(), "VALID");
    BOOST_CHECK(response["result"].isMember("payloadId"));
}
```
> R1-A:`CALL_ENGINE` 用 `task::wait` 不捕获异常;本用例走 mock 返回路径不抛,断言用 `response["result"]` 前缀(与既有用例一致);`payloadId` 默认 nullopt 须先种子。

- [ ] **Step 2: 运行确认失败**

Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest/forkchoiceUpdatedV3_captures_version_3`
Expected:FAIL(capturedForkchoiceVersion 未记录或断言 3 失败)。

- [ ] **Step 3: 实现(若 mock 未记录版本)**

在 `MockOpEngineService::updateForkchoice` 记录 `capturedForkchoiceVersion = static_cast<int>(version)`(参照既有 `forkchoiceUpdatedV1` 用例如何断言)。

- [ ] **Step 4: 运行确认通过 + 全量引擎单测**

Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest 2>&1 | tail -5`
Expected:全绿。

- [ ] **Step 5: Commit**

```bash
git add bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp
git commit -m "test(rpc): engine FCU V3 captures version 3 + valid attrs with payloadId"
```

### Task 2: a1_active.py — FCU V3 主路径 + capabilities 断言

**Files:**
- Modify: `tools/op-e2e/a1_active.py`
- Modify: `tools/op-e2e/a1_active.py` docstring(第 2 行 "enable_single_node_consensus=false"→"true";主动/被动区别在 `produce_empty_blocks` 与端口)(R1-D)

**Interfaces:**
- Consumes:Task 1 的版本语义;`Rpc.call`、`check`、`eng`/`fcs`/`attrs`/`head`/`head_num`(均在插入点作用域)。
- Produces:断言 "FCU V3 VALID + payloadId"、"getPayload V4 after FCU V3 head+1"、"capabilities 含 FCU V3/GP V4/NP V4"。

- [ ] **Step 1: 在 `a1_active.py:92`(`pid = fc["payloadId"]`)与 `:94`(`# 3. getPayload V4`)之间插入 FCU V3 主流程**

```python
# ---- op-node actual version: FCU V3 (Isthmus+), not V4 ----
attrs3 = dict(attrs)
attrs3["gasLimit"] = "0x7a1200"   # op-geth 要求;FISCO 需 Task 3 采纳(当前忽略)
fc3 = eng.call("engine_forkchoiceUpdatedV3", [fcs, attrs3])
check("FCU V3 VALID + payloadId (op-node primary)",
      fc3["payloadStatus"]["status"] == "VALID" and fc3.get("payloadId") is not None, str(fc3))
pid3 = fc3["payloadId"]
pl3 = eng.call("engine_getPayloadV4", [pid3])   # Isthmus+ GetPayload 用 V4
check("getPayload V4 after FCU V3 builds head+1",
      pl3["executionPayload"].get("blockNumber") == hex(head_num + 1), str(pl3)[:120])

# ---- capabilities:op-node 会选 FCU V3/GP V4/NP V4(R1-C:无 FCU V4) ----
caps3 = eng.call("engine_exchangeCapabilities")
check("caps has FCU V3", f"engine_forkchoiceUpdatedV3" in caps3, str(caps3))
check("caps has GetPayload V4", f"engine_getPayloadV4" in caps3, str(caps3))
check("caps has NewPayload V4", f"engine_newPayloadV4" in caps3, str(caps3))
```
> R1-D:插入点安全(两 payload 同 head 同 attrs,共存于 64 槽缓存);**FCU V3 今天大概率就绿**(`updateForkchoice` OP 分支无版本门,`isGetPayloadVersionCompatible(V4,3)==true`);本任务无保证红的断言,真正的实现点在 Task 3(gasLimit 采纳)。

- [ ] **Step 2: 运行确认**

Run:`cd tools/op-e2e && python3 a1_active.py`
Expected:全部 PASS(FCU V3 绿;若红,查 `combineForkchoiceUpdatedResult` 是否含 payloadId)。

- [ ] **Step 3: 修正 docstring(R1-D)**

第 2 行 `enable_single_node_consensus=false` → `true`(B3a 实际是 true;主动/被动区别是 `produce_empty_blocks=false` 与端口 8563/8564)。

- [ ] **Step 4: 全量 op-e2e**

Run:`cd tools/op-e2e && bash run_all.sh`
Expected:ALL OP-E2E GREEN(a1_active 16→18 断言)。

- [ ] **Step 5: Commit**

```bash
git add tools/op-e2e/a1_active.py
git commit -m "test(e2e): a1_active FCU V3 opnode-primary flow + capabilities assert; fix docstring"
```

### Task 3: 引擎采纳 FCU attrs.gasLimit + -38003 校验

**Files:**
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp`(`parsePayloadAttributes`)
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(`updateForkchoice` OP 分支)
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp`(错误断言用例)

**Interfaces:**
- Consumes:`parsePayloadAttributes`(现不读 gasLimit)、`engine::PayloadAttributes::targetGasLimit`(Types.h:86,现恒 nullopt)、`updateForkchoice` OP 分支。
- Produces:`attrs.gasLimit` 被解析进 `targetGasLimit` 并被 `buildOpPayload` 采纳;attrs 缺 gasLimit 或 timestamp<=head → `-38003`。

- [ ] **Step 1: 写失败测试(引擎级,参照 `OpNewPayloadRpcE2eTest` 真实引擎模式,R1-A)**

在引擎级测试(非 mock)加:`ForkchoiceAttributesBuildsPayloadWithProvidedGasLimit` —— 发 FCU V3 带 `gasLimit=0x200000`,getPayload 后断言 `executionPayload.gasLimit == "0x200000"`(当前会得 ledger-config 的 0xb2d05e00)。

- [ ] **Step 2: 运行确认失败**

Run:`./build/opstack-executor/tests/opstack-executor-tests --gtest_filter=*GasLimit* 2>&1 | tail -10`
Expected:FAIL(gasLimit 用 ledger-config 而非 attrs 值)。

- [ ] **Step 3: 实现**

(a) `EngineHelper.cpp::parsePayloadAttributes`:读 `pa["gasLimit"]`(存在时)填入 `attrs.targetGasLimit = boost::lexical_cast<uint64_t>(hex)`。
(b) `EngineServiceImpl.h::buildOpPayload`:gasLimit 优先取 `attrs.targetGasLimit`,否则回退 ledger-config(现 `:1617-1624` 恒用 ledger-config)。
(c) `updateForkchoice` OP 分支 attrs 校验(R1-A:在 head 推进后):attrs 非空但 `targetGasLimit` 为 nullopt,或 `attrs.timestamp <= head.timestamp` → 抛 `UnsupportedOpPayloadAttributes`(`-38003`)。

- [ ] **Step 4: 运行确认通过 + 引擎层全量**

Run:`./build/opstack-executor/tests/opstack-executor-tests 2>&1 | tail -5 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest 2>&1 | tail -5`
Expected:全绿。

- [ ] **Step 5: 补 -38003 单测(用 BOOST_CHECK_THROW,R1-A)**

```cpp
// CALL_ENGINE 走 task::wait 不捕获;断言错误码用 BOOST_CHECK_THROW 捕获 JsonRpcException。
BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV3_missing_gaslimit_rejected)
{
    EngineRpcTestFixture f;
    // 让 mock 在 attrs.targetGasLimit==nullopt 时抛 UnsupportedOpPayloadAttributes(-38003)
    Json::Value params(Json::arrayValue);
    Json::Value fcs; fcs["headBlockHash"] = "0x" + std::string(64, 'd');
    fcs["safeBlockHash"] = "0x" + std::string(64, 'd'); fcs["finalizedBlockHash"] = "0x" + std::string(64, 'd');
    Json::Value attrs; attrs["timestamp"] = "0x10"; attrs["prevRandao"] = "0x" + std::string(64, '0');
    attrs["suggestedFeeRecipient"] = "0x4200000000000000000000000000000000000011";  // 无 gasLimit
    params.append(fcs); params.append(attrs);
    Json::Value response;
    BOOST_CHECK_THROW(CALL_ENGINE(forkchoiceUpdatedV3, params, response), JsonRpcException);
}
```
(若 mock 不抛,则由引擎级测试覆盖真实路径。)

- [ ] **Step 6: Commit**

```bash
git add bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp engine/bcos-engine/EngineServiceImpl.h bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp
git commit -m "feat(engine): honor FCU attrs.gasLimit + enforce -38003 payload-attribute validation"
```

### Task 4: 测试既有 newPayload 校验(非新实现,R1-A)

**Files:**
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp` 或引擎级测试

**Interfaces:**
- Consumes:既有 `validateOpNewPayloadRequest`(`EngineServiceImpl.cpp:322-517`:withdrawalsRoot 必需、pre-Jovian blobGasUsed==0、excessBlobGas==0、extraData 9/17B、executionRequests 空)+ `handleOpNewPayload` 的 `-38005` 版本门 + Jovian post-exec DA-footprint 六元组对拍。
- Produces:断言这些校验行为(测试既有,不新实现)。

- [ ] **Step 1: 写测试(引擎级,断言既有行为)**

- `newPayloadV4_isthmus_requires_withdrawalsRoot`:`validateOpNewPayloadRequest` 缺 withdrawalsRoot → 拒。
- `newPayloadV4_pre_jovian_blob_gas_used_zero`:`blobGasUsed` 非零(pre-Jovian)→ 拒。
- `newPayloadV1_V2_V3_isthmus_gate`:Isthmus+ payload 用 V1/V2/V3 → `-38005`(既有 `handleOpNewPayload` 已实现,a1_active 已断言)。
- `newPayloadV4_jovian_da_footprint_mismatch_invalid`:Jovian 头 `blobGasUsed` 与执行结果 DA footprint 不符 → `payloadStatus.status=="INVALID"`(post-exec 六元组对拍)。

- [ ] **Step 2: 运行 → Step 3: 若缺行为则实现(以测试结果为准)→ Step 4: 全绿 → Step 5: Commit**

```bash
git commit -m "test(engine): pin newPayload validation (withdrawalsRoot/blobGasUsed/version-gate/DA-footprint)"
```

---

## Phase B — eth_getBlockBy* deposit tx round-trip

### Task 5: TransactionResponse 下发 deposit 字段 + Web3ResponseTest

**Files:**
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp`
- Modify: `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp`

**Interfaces:**
- Consumes:`Web3Transaction` 字段 **`sourceHash`(h256)/`mint`(u256)/`isSystemTx`(bool)**(Web3Transaction.h:130-134;字段名是 `isSystemTx`,非 `isSystemTransaction`,R1-B)、`TransactionType::Deposit==0x7e`(:46)、`combineTxResponse`(函数名,R1-B)。
- Produces:`eth_getTransactionByHash`/`eth_getBlockByNumber(fullTx)` 的 deposit JSON 含 `sourceHash`/`mint`/`isSystemTx`(`isSystemTx` 仅 true 时下发,op-geth omitempty,R1-C)。

- [ ] **Step 1: 写失败测试(参照既有 `combineTxResponseDepositMinimalFields` 模式,R1-B)**

```cpp
BOOST_AUTO_TEST_CASE(deposit_tx_response_emits_op_fields)
{
    bcos::rpc::Web3Transaction web3Deposit;
    web3Deposit.type = bcos::rpc::TransactionType::Deposit;
    web3Deposit.from = bcos::Address("0xdead000000000000000000000000000000000011");
    web3Deposit.sourceHash =
        bcos::h256("6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    web3Deposit.mint = bcos::u256(100);          // "0x64"
    web3Deposit.isSystemTx = true;

    auto tarsTx = web3Deposit.takeToTarsTransaction();
    // ⚠️ D4 workaround:takeToTarsTransaction 故意留空 extraTransactionHash,combineTxResponse 会调 tx.hash() 抛 EmptyTransactionHash。
    bcos::h256 arbitraryHash("0101010101010101010101010101010101010101010101010101010101010101");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
    bcostars::protocol::TransactionImpl txImpl(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    Json::Value result = Json::objectValue;
    combineTxResponse(result, txImpl, 0u, 12, bcos::crypto::HashType{});

    BOOST_CHECK_EQUAL(result["type"].asString(), "0x7e");
    BOOST_CHECK_EQUAL(result["sourceHash"].asString(),
        "0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    BOOST_CHECK_EQUAL(result["mint"].asString(), "0x64");
    BOOST_CHECK_EQUAL(result["isSystemTx"].asBool(), true);
}
```

- [ ] **Step 2: 运行确认失败**

Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=Web3ResponseTest/deposit_tx_response_emits_op_fields`
Expected:FAIL(`sourceHash`/`mint`/`isSystemTx` 缺失)。

- [ ] **Step 3: 实现(R1-B 提供的代码)**

在 `TransactionResponse.cpp` 的 `else` 块 EIP4844 分支后加:

```cpp
if (web3Tx.type == TransactionType::Deposit)
{
    result["sourceHash"] = web3Tx.sourceHash.hexPrefixed();
    result["mint"] = toQuantity(web3Tx.mint);
    if (web3Tx.isSystemTx)
        result["isSystemTx"] = true;   // op-geth omitempty:仅 true 时下发
}
```

- [ ] **Step 4: 运行确认通过 + 扩既有用例**

Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=Web3ResponseTest 2>&1 | tail -5`
Expected:PASS。另扩 `combineTxResponseDepositMinimalFields`(Web3ResponseTest.cpp:328)断言 3 新字段(纯增)。

- [ ] **Step 5: Commit**

```bash
git add bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp
git commit -m "feat(rpc): emit OP deposit-tx fields (sourceHash/mint/isSystemTx) in combineTxResponse"
```

### Task 6: op-e2e — deposit tx[0] round-trip 断言

**Files:**
- Modify: `tools/op-e2e/chain_driver.py`
- Modify: `tools/op-e2e/run_all.sh`(断言数注释 31→39、16→18)

**Interfaces:**
- Consumes:Task 5 的字段下发;`chain_driver.py` 的 `Rpc.call`、`check`、循环内 `blk_num`(回执 blockNumber,该块已证明有 deposit at index0,`chain_driver.py:183-185`)。
- Produces:断言 `eth_getBlockByNumber(blk_num, true)["transactions"][0]` 为 deposit(`type==0x7e`、`isSystemTx is True`、`sourceHash` 存在),且 `eth_getTransactionByHash` round-trip 一致。

- [ ] **Step 1: 在 `chain_driver.py` 循环内 `:193`(receipt cumulative>=used)之后插入**

```python
# op-node derive.PayloadToBlockRef:this block's tx[0] must be a parseable deposit.
blk_full = rpc.call("eth_getBlockByNumber", [blk_num, True])
tx0 = blk_full["transactions"][0]
check(f"tx[{i}] block tx[0] is deposit (0x7e)", tx0.get("type") == "0x7e", str(tx0.get("type")))
check(f"tx[{i}] block tx[0] isSystemTx true", tx0.get("isSystemTx") is True, str(tx0.get("isSystemTx")))
bt = rpc.call("eth_getTransactionByHash", [tx0["hash"]])
check(f"tx[{i}] deposit roundtrip byHash type",
      bt is not None and bt.get("type") == "0x7e", str(bt))
check(f"tx[{i}] deposit roundtrip sourceHash present+equal",
      bt is not None and bt.get("sourceHash") is not None and bt.get("sourceHash") == tx0.get("sourceHash"),
      str(bt))
```
> R1-D 修正:用 `blk_num`(回执 blockNumber,证明含 deposit)而非 `"latest"`(B3 `produce_empty_blocks=true`,latest 可能是空块 → IndexError);`sourceHash` 比较加 `is not None`(否则 `None==None` 恒真,tautology)。

- [ ] **Step 2: 运行确认失败**

Run:`cd tools/op-e2e && python3 chain_driver.py`
Expected:新断言红(`isSystemTx` 缺失;`type==0x7e` 今天已绿)。若 Task 5 未合入,红点即"字段缺失"。

- [ ] **Step 3: 实现(依赖 Task 5;若已合入无额外代码)+ 更新 run_all.sh 断言数注释**

- [ ] **Step 4: 运行确认通过**

Run:`cd tools/op-e2e && bash run_all.sh`
Expected:ALL OP-E2E GREEN(chain_driver 31→39 断言,默认 `--txs 2` 每 tx 4 新断言;若需维持 31 可仅对 tx0 断言——但建议全量)。

- [ ] **Step 5: Commit**

```bash
git add tools/op-e2e/chain_driver.py tools/op-e2e/run_all.sh
git commit -m "test(e2e): assert op-node block-ref deposit tx[0] round-trip (keyed by receipt block)"
```

---

## Phase C — op-node 集成 harness(独立子系统,建议单独计划)

### Task 7: harness 组件拆解与骨架(依赖外部 infra)

> ⚠️ 该子系统依赖 optimism monorepo `op-e2e/actions` 框架或独立 L1 mock,建议**单独拆成一个计划**(spec §7.1 的 (a)-(g) 组件)。此处仅立项。

**组件(引自 spec §7.1 + R1-C 强化):**
- (a) **L1 mock**:geth devnet 或 op-e2e L1 fixture,带真实 `OptimismPortal` 发 `DepositEvent`;
- (b) **L1 批量数据**:op-batcher 或预播种;
- (c) **op-node 派生**:attrs 含 L1-info tx + deposits + fork 升级 tx(`op-node/rollup/derive/attributes.go`);
- (d) **FCU V3→getPayload V4→newPayload V4→FCU** 驱动 FISCO(注意:op-node 发 **FCU V3**,R1-C);
- (e) **确定性门**:同 attrs 下 FISCO 块哈希与 op-geth 逐字节一致(需 Task 3 的 gasLimit 采纳先合入);
- (f) deposit→L2 与 withdraw→MessagePasser→`eth_getProof`→finalize 端到端(`eth_getProof` 是 op-node `L2EthClient` 硬依赖,预 Isthmus output root,R1-C);
- (g) 重启与 reorg 韧性。

**前置依赖:** 先完成 Task 3(gasLimit 采纳,否则块哈希必分叉);确认 FISCO 以 `enableOPEngine` + 外部 FCU 驱动出块(B3a 已证明可)。

- [ ] **Step 1: 立项**:创建 `tools/op-e2e/l1_mock/` + `tools/op-e2e/opnode_driver.py` 骨架(空桩 + 任务清单)。
- [ ] **Step 2: 单独编写 harness 实施计划**(writing-plans),本计划不含其 TDD 任务。

---

## Self-Review(v2)

- **Spec 覆盖**:§7 P0 三项全覆盖 —— A(引擎契约)Task 0-4;B(deposit round-trip)Task 5-6;C(harness)Task 7 立项+独立计划。
- **Round 1 合入核对**:-38006 已删(R1-C);FCU V1/V2 门已删(R1-A);gasLimit 采纳升为 Task 3 核心(R1-A);`isSystemTx`/`combineTxResponse`/`extraTransactionHash`/omitempty 已修正(R1-B/C);`blk_num`/`is not None` 已修正(R1-D);Task 0 基线重建已加(R1-A)。
- **占位符扫描**:无 TBD/TODO;Task 4 的引擎级测试以"参照既有校验"为边界,具体断言由实现期从 `validateOpNewPayloadRequest` 逐字段提取。
- **类型一致性**:`TransactionType::Deposit`/`Web3Transaction.sourceHash/mint/isSystemTx`/`engine::ApiVersion`/`combineTxResponse`/`targetGasLimit` 均来自已读源码;错误码仅用 op-geth 真实存在的 5 个。
