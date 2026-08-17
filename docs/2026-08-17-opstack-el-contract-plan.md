# FISCO opstack OP-node EL 契约合规实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **版本:** v4 —— 2026-08-17 经 **Round 1(4 代理)+ Round 2(4 代理)+ Round 3(4 代理)审查**修订。v3 采纳 R2-D 裁决(全 attrs 采纳)。v4 采纳 R3-A/B/C/D:**新增 3 个核心循环硬阻塞的修复(B1 getPayload 信封 PBBR / B2 Isthmus blobGasUsed 回环 / B3 BlockResponse PBBR 零值)+ baseFee 热切换排序 + newPayload 回放检查用子块 extraData + noTxPool 接线 + extraData 版本字节按 fork + a1_active 基础 attrs 补 gasLimit(防 Task 4 回归)+ util.cpp 路径修正 + -38003 确定为必交付**。

**Goal:** 让 FISCO opstack 满足 OP-node 作为执行客户端的硬契约 —— `buildOpPayload` 完整采纳 op-node 的 FCU V3 attrs(含 deposits/extraData/baseFee),**getPayload 信封携带 PBBR/blobGasUsed 使 FISCO 自建块能被自己 newPayload 接受**,`eth_getBlockBy*` 的 deposit tx round-trip 与 PBBR 正确,safe/finalized 标签正确。

**Architecture:** 三段推进 —— (A) **引擎 API 契约合规(核心)**:`buildOpPayload` 全 attrs 采纳 + attrs/newPayload 校验对拍 + `buildOpPayload` 构造对拍 + safe/finalized 标签;(B) deposit tx round-trip:JSON 字段下发 + RPC 单测 + op-e2e 断言;(C) op-node 集成 harness(外部,单独计划)。

**Tech Stack:** C++(EngineHelper/EngineServiceImpl/TransactionResponse + Boost.Test)、Python(op-e2e)、op-node/op-geth(参考契约)。

## Global Constraints

- 测试不可退步:ctest(1935/1935)与 op-e2e 必须保持全绿;新增只增不改。
- ⚠️ **基线重建(R1-A/R2-A)**:`EngineRpcTest.cpp` 的 `forkchoiceUpdatedV4`/`getPayloadV4`/`newPayloadV4` 断言 `error==UnsupportedFork`,但当前 mock 返回 VALID 且 `CALL_ENGINE` 用 `task::wait`(不捕获,异常→`std::terminate`)。**Task 0 必须先重建 `test-bcos-rpc` 并把这三个用例改为断言 VALID**(或加端点 catch-and-translate)。否则基线不可验证。
- ⚠️ **引擎 API 已接线但 `buildOpPayload` 丢弃几乎所有 attrs(R2-D)**:V1-V4 端点已注册、`maxEngineVersion=V4`、OP 模式 `updateForkchoice`→`buildOpPayload` 已通;但 **`parsePayloadAttributes` 只读 prevRandao/feeRecipient/timestamp/withdrawals/parentBeaconBlockRoot(`EngineHelper.cpp:175-211`),`buildOpPayload` 只用 ledger-config gasLimit(`EngineServiceImpl.h:1617-1624`)、清零 PBBR(`:1658`)、extraData 硬编码 minBaseFee=0(`:1671-1675`)、合成自己的零 L1 deposit(`OpEngineSeam.h:82-149`)而**丢弃 op-node 的 `attrs.transactions`**。→ 块哈希/tx 列表必然分叉,op-node 不会接受。
- op-node 引擎版本(R1-C):FCU V1(Pre-Canyon)/V2(Canyon)/V3(Ecotone+,**Isthmus+ 也是 V3,连 nil attrs 都 V3**);NewPayload V2/V3(V4=Isthmus+);GetPayload V2/V3/V4(Isthmus)/V5(Karst)。无 FCU V4 被 op-node 调用。
- 错误码(仅 op-geth 真实存在):`-38002`/`-38003`/`-38005`/`-32602`。**无 `-38006`**。
- op-node Isthmus+ FCU V3 attrs 字段(R2-D):`gasLimit`/`eip1559Params`/`minBaseFee`/`parentBeaconBlockRoot`/`transactions`(deposits)/`noTxPool`/`withdrawals` —— **全部必须被 `buildOpPayload` 采纳**。
- deposit tx 类型 `0x7E`;C++ 字段 `isSystemTx`(非 isSystemTransaction);函数 `combineTxResponse`;`TransactionResponse.cpp:66-68` 现排除 deposit 字段输出。
- op-node 消费 `engine_getPayload` 原始 RLP 字节(`PayloadToBlockRef` 走 `tx.UnmarshalBinary`);`eth_getBlockBy*` full-tx JSON 经 `RPCBlock` 重编码校验 tx-root/block-hash。
- ⚠️ **三个核心循环硬阻塞(R3-D,Task 3/5 必须修复,否则第一块就断)**:
  - **B1** — getPayload V4 信封**必须携带 `parentBeaconBlockRoot`**:`GetPayloadData`(Types.h:167-183)现无 PBBR 字段、`combineGetPayloadResponse`(EngineHelper.cpp:304-352)不发射 → op-node `NewPayload(ctx, envelope, envelope.ParentBeaconBlockRoot)` 传 null → FISCO 自己的 `validateOpNewPayloadRequest` 拒。
  - **B2** — Isthmus(pre-Jovian)块 `blobGasUsed` 必须**存在且 ==0**:`buildOpPayload` 现置 nullopt(`EngineServiceImpl.h:1732-1734`),`validateOpNewPayloadRequest` 要求存在(`:366`)+==0(`:370`);op-geth 返回 0(`worker.go:398`)。当前 op-e2e 链是 Jovian,掩盖此问题。
  - **B3** — `eth_getBlockByNumber` 的 `parentBeaconBlockRoot` 现硬编码零(`BlockResponse.cpp:128`):Task 3 写真实 PBBR 后此处须发射真实值,否则 op-node `RPCBlock.Verify` 重算哈希≠上报 → 断。
- **safe/finalized 标签必须正确(R2-D/R3-B)**:`getBlockNumberByTag`(`bcos-rpc/bcos-rpc/util.cpp:43-44`,非 web3jsonrpc/utils)**现把 `safe`/`finalized`/`pending` 别名成 `latest`** —— 安全性正确性 bug,op-node `L2BlockRefByLabel` 会静默拿到错误头。
- 所有 DIVERGENCE/负向分支断言「PASS(登记为预期)」;脚本退出码 0 表示通过。

---

## File Structure

| 文件 | 责任 | 动作 |
|---|---|---|
| `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp` | 引擎 RPC 单测 | 改:Task 0 对齐 V4 用例;Task 1 FCU V3;Task 4 attrs 校验 |
| `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp` | `parsePayloadAttributes`/`serializeExecutionPayload` | 改:Task 3 全 attrs 解析(gasLimit/transactions/eip1559Params/minBaseFee);Task 4 校验 |
| `bcos-framework/bcos-framework/engine/Types.h` | `PayloadAttributes` | 改:Task 3 增 `transactions`/`eip1559Params`/`minBaseFee`/`noTxPool` 字段 |
| `engine/bcos-engine/EngineServiceImpl.h` | `buildOpPayload`/`updateForkchoice` | 改:Task 3 全 attrs 采纳 + B1/B2 + noTxPool + baseFee 热切换 + newPayload 回放检查;Task 4 `-38003` |
| `bcos-framework/bcos-framework/engine/Types.h` | `PayloadAttributes`/`GetPayloadData` | 改:Task 3 增 `transactions`/`eip1559Params`/`minBaseFee`/`noTxPool` 字段 + `GetPayloadData` 增 `parentBeaconBlockRoot`(B1) |
| `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp` | `parsePayloadAttributes`/`combineGetPayloadResponse` | 改:Task 3 全 attrs 解析 + getPayload 信封 PBBR(B1) |
| `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp` | `handleForkchoiceUpdated` | 改:Task 4 `-38003` 异常→error 翻译 |
| `bcos-rpc/bcos-rpc/util.cpp` | `getBlockNumberByTag`(自由函数) | 改:Task 6 safe/finalized 正确路由(接线在 EthEndpoint.cpp:934-941) |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp` | 块 JSON | 改:Task 3(B3)发射真实 `parentBeaconBlockRoot` |
| `tools/op-e2e/a1_active.py` | 引擎 API e2e 驱动 | 改:Task 2 FCU V3 主路径 + caps(16→21 断言) |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp` | 交易 JSON | 改:Task 7 deposit 字段下发 |
| `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp` | RPC 单测 | 改:Task 7 deposit 断言 |
| `tools/op-e2e/chain_driver.py` | 交易驱动 | 改:Task 8 deposit tx[0] round-trip(31→39) |
| `tools/op-e2e/run_all.sh` | 回归门 | 改:断言数注释 |
| `opstack-executor/tests/` | 引擎级测试(真实 EngineServiceImpl) | 改:Task 3/4/5 构造与校验对拍 |

---

## Phase A — 引擎 API 契约合规(核心)

### Task 0: 基线重建与 stale V4 用例对齐

**Files:**
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp`(`forkchoiceUpdatedV4`/`getPayloadV4`/`newPayloadV4`)

**Interfaces:**
- Consumes:`EngineRpcTestFixture`/`MockOpEngineService`/`CALL_ENGINE`。
- Produces:可验证基线。

- [ ] **Step 1: 重建测试二进制**
Run:`cd build && cmake --build . --target test-bcos-rpc 2>&1 | tail -5`
- [ ] **Step 2: 运行确认红**
Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest 2>&1 | tail -20`
Expected:三个 V4 用例红(mock 返回 VALID,断言 error==UnsupportedFork)。
- [ ] **Step 3: 改断言为 VALID(R2-A:仅此在作用域内可行)**
`forkchoiceUpdatedV4`:`response["result"]["payloadStatus"]["status"]=="VALID"` 且 `!response["result"].isMember("payloadId")`(mock 默认 nullopt);`getPayloadV4`:`response["result"].isMember("executionPayload")`;`newPayloadV4`:`response["result"]["status"]=="VALID"`。**不要**让 mock 抛 UnsupportedFork(端点无 catch、`CALL_ENGINE` 用 `task::wait` 不捕获 → `std::terminate`)。
> 若需保留 -38005 语义,须先加端点 catch-and-translate(把引擎异常映射为 `buildJsonError(..., EngineError::UnsupportedFork, ...)`),超出本任务范围,单独立案。
- [ ] **Step 4: 运行全绿 + Commit**
```bash
git add bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp
git commit -m "fix(rpc): align EngineRpcTest V4 cases with mock (VALID); rebuild baseline"
```

### Task 1: EngineRpcTest — FCU V3 captured-version + payloadId

**Files:**
- Modify: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp`

**Interfaces:**
- Consumes:`MockOpEngineService::m_state->capturedForkchoiceVersion`(`std::optional<int>`,EngineRpcTest.cpp:52,**mock 已在 `updateForkchoice` 记录** `:74`)、`m_state->forkchoiceUpdatedResult.payloadId`(`std::optional<std::string>`,Types.h:164/47)。
- Produces:用例 `forkchoiceUpdatedV3_captures_version_3`、`forkchoiceUpdatedV3_with_attrs_valid_has_payloadId`。
- ⚠️ **与 Task 4 协调(R2-D)**:Task 4 会让"attrs 缺 gasLimit → -38003";本任务的 mock 不强制,故两用例的 attrs **必须带 `gasLimit`** 以免与真实引擎语义脱节。

- [ ] **Step 1: 写测试(注意 optional 解引用,R2-A)**

```cpp
BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV3_captures_version_3)
{
    EngineRpcTestFixture f;
    Json::Value params(Json::arrayValue);
    Json::Value fcs; fcs["headBlockHash"] = "0x" + std::string(64, 'a');
    fcs["safeBlockHash"] = "0x" + std::string(64, 'a'); fcs["finalizedBlockHash"] = "0x" + std::string(64, 'a');
    Json::Value attrs; attrs["timestamp"] = "0x10"; attrs["prevRandao"] = "0x" + std::string(64, '0');
    attrs["suggestedFeeRecipient"] = "0x4200000000000000000000000000000000000011";
    attrs["gasLimit"] = "0x7a1200";   // Task 4 后缺它→-38003,故必带
    params.append(fcs); params.append(attrs);
    Json::Value response;
    CALL_ENGINE(forkchoiceUpdatedV3, params, response);
    BOOST_REQUIRE(f.mockService.m_state->capturedForkchoiceVersion.has_value());   // optional 须先 has_value
    BOOST_CHECK_EQUAL(*f.mockService.m_state->capturedForkchoiceVersion, 3);
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
    attrs["gasLimit"] = "0x7a1200";
    params.append(fcs); params.append(attrs);
    Json::Value response;
    CALL_ENGINE(forkchoiceUpdatedV3, params, response);
    BOOST_CHECK_EQUAL(response["result"]["payloadStatus"]["status"].asString(), "VALID");
    BOOST_CHECK(response["result"].isMember("payloadId"));   // combineForkchoiceUpdatedResult 仅 payloadId 有值时写
}
```

- [ ] **Step 2: 运行确认通过(编译修正后即绿,R2-A:mock 已记录版本)**
Run:`./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest/forkchoiceUpdatedV3_captures_version_3`
Expected:编译通过后 PASS(若红则检查 `combineForkchoiceUpdatedResult` 响应结构)。
- [ ] **Step 3: Commit**
```bash
git add bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp
git commit -m "test(rpc): engine FCU V3 captures version 3 + payloadId (attrs with gasLimit)"
```

### Task 2: a1_active.py — FCU V3 主路径 + capabilities(16→21)

**Files:**
- Modify: `tools/op-e2e/a1_active.py`(docstring:2 行 "false"→"true",R1-D/R2-C)

**Interfaces:**
- Consumes:`Rpc.call`/`check`/`eng`/`fcs`/`attrs`/`head_num`(作用域已确认,R2-C)。
- Produces:断言 FCU V3 VALID+payloadId、getPayload V4 head+1、caps 含 FCU V3/GP V4/NP V4。

- [ ] **Step 1: 在 `a1_active.py:92`(`pid = fc["payloadId"]`)与 `:94` 之间插入(R2-C:插入点与作用域确认安全)**

```python
# ---- op-node actual version: FCU V3 (Isthmus+), not V4 ----
attrs3 = dict(attrs)
attrs3["gasLimit"] = "0x7a1200"   # op-geth 必需;Task 3 采纳后生效
fc3 = eng.call("engine_forkchoiceUpdatedV3", [fcs, attrs3])
check("FCU V3 VALID + payloadId (op-node primary)",
      fc3["payloadStatus"]["status"] == "VALID" and fc3.get("payloadId") is not None, str(fc3))
pid3 = fc3["payloadId"]
pl3 = eng.call("engine_getPayloadV4", [pid3])   # Isthmus+ GetPayload 用 V4
check("getPayload V4 after FCU V3 builds head+1",
      pl3["executionPayload"].get("blockNumber") == hex(head_num + 1), str(pl3)[:120])

# ---- capabilities:op-node 选 FCU V3/GP V4/NP V4 ----
caps3 = eng.call("engine_exchangeCapabilities")
check("caps has FCU V3", "engine_forkchoiceUpdatedV3" in caps3, str(caps3))
check("caps has GetPayload V4", "engine_getPayloadV4" in caps3, str(caps3))
check("caps has NewPayload V4", "engine_newPayloadV4" in caps3, str(caps3))
```
> 共 **5 个新 check**(R2-C):FCU V3 VALID、getPayload V4 head+1、3 个 caps → **16→21 断言**。
> 现状:FCU V3 今天大概率绿(`updateForkchoice` OP 分支无版本门);真正的行为点靠 Task 3/4 的引擎改动。
- [ ] **Step 2: 运行确认**
Run:`cd tools/op-e2e && python3 a1_active.py` → 全绿。
- [ ] **Step 3: 修 docstring(R1-D/R2-C)**:`enable_single_node_consensus=false`→`true`(B3a config.genesis:12 实为 true;主动/被动区别是 `produce_empty_blocks=false` 与端口)。
- [ ] **Step 4: 全量 op-e2e + 更新 run_all.sh 注释(16→21)**
Run:`cd tools/op-e2e && bash run_all.sh` → ALL OP-E2E GREEN。
- [ ] **Step 5: Commit**
```bash
git add tools/op-e2e/a1_active.py tools/op-e2e/run_all.sh
git commit -m "test(e2e): a1_active FCU V3 opnode-primary + caps assert (16->21); fix docstring"
```

### Task 3: buildOpPayload 全 attrs 采纳 + getPayload 信封 PBBR/blobGasUsed(R2-D 核心 + R3-A/D B1/B2/B3)

**Files:**
- Modify: `bcos-framework/bcos-framework/engine/Types.h`(`PayloadAttributes` 增字段 + `GetPayloadData` 增 `parentBeaconBlockRoot`)
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp`(`parsePayloadAttributes` 全字段解析 + `combineGetPayloadResponse` 发射 PBBR)
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(`buildOpPayload` 消费 attrs + baseFee 热切换 + newPayload 回放检查)
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp`(B3:发射真实 PBBR)
- Test: `opstack-executor/tests/`(引擎级构造对拍 + 完整 getPayload→newPayload 回环)

**Interfaces:**
- Consumes:op-node FCU V3 attrs 完整字段集(R2-D 表 + R3-A 修正)。
- Produces:`buildOpPayload` 用 `attrs.transactions`(deposit 列表)**前置**到 mempool txs(honor `noTxPool`)、`attrs.parentBeaconBlockRoot` 写头、`attrs.eip1559Params`/`attrs.minBaseFee` 写 baseFee+extraData、`attrs.gasLimit` 写头 gasLimit;**getPayload 信封携带 `parentBeaconBlockRoot` 与 `blobGasUsed`(Isthmus==0)**,使 FISCO 自建块能被自己 `newPayload` 接受(B1/B2);`eth_getBlockByNumber` 发射真实 PBBR(B3);合成零 L1 deposit 仅作本地驱动(PBFT)回退。

- [ ] **Step 1: 写失败测试(引擎级,真实 EngineServiceImpl)**

构造 op-node 等价 Isthmus+ FCU V3 attrs(带 `transactions`=[L1-info deposit],`parentBeaconBlockRoot`,`eip1559Params`,[Jovian `minBaseFee`],`gasLimit`),**走完整 build→getPayload→newPayload 回环**,断言:
- `executionPayload.transactions[0] == attrs.transactions[0]`(当前是合成零 L1 deposit → 红);
- `executionPayload.parentBeaconBlockRoot == attrs.parentBeaconBlockRoot`(当前 getPayload 信封无此字段 → 红,B1);
- `executionPayload.gasLimit == attrs.gasLimit`(当前 ledger-config → 红);
- Isthmus(非 Jovian):`executionPayload.blobGasUsed == "0x0"`(当前 nullopt → 红,B2);
- `newPayload(executionPayload, [], executionPayload.parentBeaconBlockRoot)` 返回 **VALID**(当前因 B1 缺 PBBR 被拒 → 红);
- Holocene/Jovian:`blockHeader.extraData[1:9]`/`[9:17]` 编码 eip1559Params/minBaseFee,且**版本字节按 fork**(Isthmus `0x00`/Jovian `0x01`,R3-D)。

- [ ] **Step 2: 运行确认失败**
Run:`./build/opstack-executor/tests/opstack-executor-tests --gtest_filter=*AttrsAdoption* 2>&1 | tail -10`
- [ ] **Step 3: 实现**

(a) `Types.h` `PayloadAttributes` 增(R3-A 修正类型):`std::optional<std::vector<bcos::bytes>> transactions`(**vector**,每元素一条原始 EIP-2718 信封)、`std::optional<std::array<uint8_t,8>> eip1559Params`、`std::optional<uint64_t> minBaseFee`、`bool noTxPool{false}`、**`std::optional<uint64_t> gasLimit`**(新字段;`targetGasLimit` 是 V4 死字段,op-node V3 发 `gasLimit`,R3-A)。`GetPayloadData` 增 `std::optional<h256> parentBeaconBlockRoot`(B1)。
(b) `EngineHelper.cpp::parsePayloadAttributes`:读 `gasLimit`(`fromQuantity`,非 lexical_cast)、`transactions`(hex 数组→`vector<bytes>`,模式抄 `parseNewPayloadRequest:68-88`)、`eip1559Params`(fromHex+size==8 校验)、`minBaseFee`、`noTxPool`、`parentBeaconBlockRoot`(已有)。
(c) `EngineServiceImpl.h::buildOpPayload`:
- **tx 列表(R3-D 修正:前置而非替换)**:`rawTransactions` 以 `attrs.transactions` 为前缀(第一个是 L1-info deposit),后接 mempool sealed txs;`noTxPool==true` 时**跳过 mempool seal**(`:402-405` 加门)。`attrs.transactions` 缺时回退 `makeL1AttributesDeposit`(本地驱动)。
- PBBR:`:1658` 改为 `attrs.parentBeaconBlockRoot.value_or(h256{})`;并把它填进 `GetPayloadData.parentBeaconBlockRoot`(B1)。
- **baseFee 热切换(R3-A)**:extraData 组装提前到 baseFee 计算之前,或参数化 `calcOpBaseFee`(elasticity/denominator/minBaseFee 由 attrs 覆盖);Holocene 用 `attrs.eip1559Params`、Jovian 用 `attrs.minBaseFee`(替换 `:1671-1675` 硬编码 8/2/0)。**extraData 版本字节按 fork**(Isthmus `0x00`/Jovian `0x01`,替换 `:1666` 硬编码 `0x01`)。
- gasLimit:`:1617-1624` 优先 `attrs.gasLimit`,回退 ledger-config(R2-A `effectiveGasLimit` 公式)。
- **blobGasUsed(B2)**:非 Jovian 时 `ExecutionPayload.blobGasUsed` 置 `0`(非 nullopt,对齐 op-geth `worker.go:398`),Jovian 为 DA footprint。
- **newPayload 回放检查(R3-A)**:`EngineServiceImpl.h:1078` 的 `expectedBaseFee` 改用**子块 extraData** 参数(而非父块)+ 父块 gas 字段,对齐 op-geth `CalcBaseFee(config, parent, time, header.Extra)`,否则 Holocene 参数热切换后被自己 `newPayload` 拒。
(d) `combineGetPayloadResponse`(`EngineHelper.cpp:304-352`):发射 `parentBeaconBlockRoot`(从 `GetPayloadData`,B1)。
(e) `BlockResponse.cpp:128`(B3):发射 `blockHeader->parentBeaconBlockRoot()` 真实值,替换硬编码零。

- [ ] **Step 4: 运行确认通过 + 引擎层全量 + bcos-rpc 全量**
Run:`./build/opstack-executor/tests/opstack-executor-tests 2>&1 | tail -5 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=EngineRpcTest 2>&1 | tail -5`
- [ ] **Step 5: Commit**
```bash
git add bcos-framework/bcos-framework/engine/Types.h bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp engine/bcos-engine/EngineServiceImpl.h bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp opstack-executor/tests/
git commit -m "feat(engine): adopt all OP FCU-V3 attrs + getPayload PBBR/blobGasUsed + BlockResponse PBBR (B1/B2/B3)"
```

### Task 4: FCU attrs / newPayload 校验对拍 op-geth

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(`updateForkchoice` OP 分支 `-38003`)
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp`(`parsePayloadAttributes` 校验)
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp`(异常→error 翻译,若做 RPC 层断言)
- Test: `bcos-rpc/test/unittests/rpc/EngineRpcTest.cpp` + 引擎级测试

**Interfaces:**
- Consumes:op-geth `checkOptimismPayloadAttributes`(`api_optimism.go:40-65`)语义:gasLimit 非空→`-38003`、空 withdrawals(Canyon 后)、`ValidateHolocene1559Params`(eip1559Params 8B 非双零)。
- Produces:attrs 校验错误码与 op-geth 一致;newPayload 既有校验(`validateOpNewPayloadRequest`)被测试固化。

- [ ] **Step 1: 写失败测试(引擎级,R2-A/R2-D:不用 `BOOST_CHECK_THROW(CALL_ENGINE, JsonRpcException)`)**

- `updateForkchoice` attrs 缺 `gasLimit`(Isthmus+)→ 抛 `UnsupportedOpPayloadAttributes`(即 -38003;须经端点翻译或引擎级断言);
- attrs `eip1559Params` 长度≠8 或双零 → `-38003`(Holocene 校验);
- attrs `withdrawals` 非空(Canyon 后)→ `-38003`。
- newPayload:`validateOpNewPayloadRequest` 既有行为测试(withdrawalsRoot 必需、pre-Jovian blobGasUsed==0、extraData 9/17B、executionRequests 空、Isthmus V1/V2/V3→`-38005`)。

- [ ] **Step 2: 运行确认失败 → Step 3: 实现**

`updateForkchoice` OP 分支(head 推进后,R2-A `:372` 之后,`if constexpr (c_opMode)`):attrs 非空但 `gasLimit` nullopt(用 Task 3 新增的 `attrs.gasLimit`,**不是**死字段 `targetGasLimit`)、`eip1559Params` 非法、withdrawals 非空 → 抛 `UnsupportedOpPayloadAttributes`。
**错误码输出(R2-A/R3-D)**:`CALL_ENGINE` 走 `task::wait` 不重抛 → 断言 `-38003` **在 `handleForkchoiceUpdated` 加 catch 把引擎异常翻译为 `buildJsonError(..., EngineError::InvalidPayloadAttributes, ...)`(确定为必交付,非可选)**,或引擎级测试捕获真实 `UnsupportedOpPayloadAttributes`。**不可用 `BOOST_CHECK_THROW(CALL_ENGINE(...), JsonRpcException)`**(task::wait + bcos::Exception 类型不匹配)。
**⚠️ 防回归(R3-C)**:a1_active.py 的既有 FCU V4 调用(`:80-84` 基础 `attrs` 无 gasLimit,`:88` 期待 VALID)—— Task 4 落地后此检查会红。**须在 Task 2/4 依赖中给基础 `attrs` 加 `gasLimit="0x7a1200"`**(`:80-84` 基础 dict),使 FCU V4 与新 FCU V3 都继承;这对应 spec §7 P1"修 a1_active.py 主路径"。

- [ ] **Step 4: 运行通过 + 全量 → Step 5: Commit**
```bash
git commit -m "feat(engine): enforce op-geth parity FCU attrs validation (-38003 via endpoint translation) + pin newPayload checks"
```

### Task 5: buildOpPayload 构造对拍(头字段逐项,Phase A/B 内,不等到 harness)

**Files:**
- Test: `opstack-executor/tests/`(构造模式差分)

**Interfaces:**
- Consumes:Task 3 的全 attrs 采纳;op-geth `miner/payload_building_test.go`(`TestDeterministicPayloadId`)与 op-reth `PayloadId` parity 的思路。
- Produces:同 attrs 下 `buildOpPayload` 产出的头字段(gasLimit/extraData[1:9]/[9:17]/PBBR/tx 列表)与 op-geth 期望值逐项一致。

- [ ] **Step 1: 写构造对拍测试(引擎级,R3-D 扩为完整回环)**

固定一组 Isthmus+ attrs(含 deposits/eip1559Params/minBaseFee/PBBR/gasLimit),断言 `buildOpPayload` 产出的:
- `header.gasLimit == attrs.gasLimit`;
- `extraData[1:9]` == eip1559Params 编码、`extraData[9:17]` == minBaseFee 编码(对照 op-geth `ValidateOptimismExtraData` 的 9B/17B 布局),**版本字节按 fork**(Isthmus `0x00`/Jovian `0x01`);
- `header.parentBeaconBlockRoot == attrs.parentBeaconBlockRoot`;
- `transactions == attrs.transactions`(逐条 hex 相等);
- **完整 getPayload→newPayload 回环(R3-D)**:build→getPayload→`newPayload(envelope, [], envelope.ParentBeaconBlockRoot)` 返回 VALID —— 一次抓 B1(B2)(B3);
- 可加:`PayloadId` 与 op-geth `TestDeterministicPayloadId` 一致(需要时)。
- [ ] **Step 2: 运行失败 → Step 3: 依赖 Task 3 已实现(本任务主要是断言固化)→ Step 4: 全绿 → Step 5: Commit**
```bash
git commit -m "test(engine): buildOpPayload construction parity + getPayload->newPayload round-trip vs op-geth"
```

### Task 6: safe/finalized block-tag 正确路由

**Files:**
- Modify: `bcos-rpc/bcos-rpc/util.cpp`(`getBlockNumberByTag :43-44`,**自由函数**,R3-B 路径修正)
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.cpp`(`getBlockNumberByTag` 包装 `:934-941`,有 engineService 访问)
- Test: `bcos-rpc/test/unittests/rpc/`(BlockTag 相关)+ op-e2e

**Interfaces:**
- Consumes:引擎 `getSafeBlockNumber`/`getFinalizedBlockNumber`(EngineServiceImpl.h:465-475,**已存在**);op-node `L2BlockRefByLabel` 用 `eth_getBlockByNumber("safe"/"finalized", fullTx=true)`。
- Produces:`eth_getBlockByNumber("safe")`/`("finalized")` 返回引擎实际 safe/finalized 头,而非 latest;**nullopt 回退 latest**(FCU 前 safe/finalized 未设,内存态)。

- [ ] **Step 1: 写失败测试**

RPC 单测:mock engine 的 `getSafeBlockNumber`/`getFinalizedBlockNumber` 返回具体值(注意现有 mock 默认 nullopt,R3-B),`eth_getBlockByNumber("safe")` 应返回该块;当前别名 latest → 红。op-e2e:`eth_getBlockByNumber("safe", true)` 拿到非零 PBBR 块后,用 op-node `RPCBlock.Verify` 语义(或等价重算)校验块哈希一致(B3 关联)。

- [ ] **Step 2: 运行失败 → Step 3: 实现(R3-B)**

在 `EthEndpoint::getBlockNumberByTag`(`:934-941`)分支:`safe` → `engineService->getSafeBlockNumber()` 有值用之、nullopt 回退 latest;`finalized` 同理;`latest`/`pending`/`earliest` 保持。**不改**自由函数 `getBlockNumberByTag`(无 engine 访问);如需覆盖 `eth_newFilter`/`eth_getLogs`(FilterRequest.cpp:58,61),扩展自由函数签名带 safe/finalized optional 并更新两处调用(可选)。

- [ ] **Step 4: 运行通过 + 全量 → Step 5: Commit**
```bash
git commit -m "feat(rpc): route safe/finalized block tags to engine safe/finalized heads (not latest)"
```

---

## Phase B — eth_getBlockBy* deposit tx round-trip

### Task 7: TransactionResponse 下发 deposit 字段 + Web3ResponseTest(R2-B 已逐行核实)

**Files:**
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp`
- Modify: `bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp`

**Interfaces:**
- Consumes:`Web3Transaction.sourceHash/mint/isSystemTx`(Web3Transaction.h:130-134)、`TransactionType::Deposit==0x7e`(:46)、`combineTxResponse`(TransactionResponse.h:33-37)、`toQuantity(u256)`(DataConvertUtility.h:468)、`h256::hexPrefixed()`(FixedBytes.h:289)。
- Produces:deposit JSON 含 `sourceHash`/`mint`/`isSystemTx`(`isSystemTx` 仅 true 下发,op-geth omitempty)。

- [ ] **Step 1: 写失败测试(参照既有 `combineTxResponseDepositMinimalFields`,Web3ResponseTest.cpp:328-356;必须设 `extraTransactionHash`,否则 `tx.hash()` 抛 EmptyTransactionHash)**

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
    bcos::h256 arbitraryHash("0101010101010101010101010101010101010101010101010101010101010101");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());   // D4 workaround
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
- [ ] **Step 3: 实现(R1-B 代码,插入 EIP4844 分支后 `:102` 与 else 闭合 `:103` 之间)**

```cpp
if (web3Tx.type == TransactionType::Deposit)
{
    result["sourceHash"] = web3Tx.sourceHash.hexPrefixed();
    result["mint"] = toQuantity(web3Tx.mint);
    if (web3Tx.isSystemTx)
        result["isSystemTx"] = true;   // op-geth omitempty
}
```
- [ ] **Step 4: 运行通过 + 扩 `combineTxResponseDepositMinimalFields` 断言 3 新字段 → Step 5: Commit**
```bash
git add bcos-rpc/bcos-rpc/web3jsonrpc/model/TransactionResponse.cpp bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp
git commit -m "feat(rpc): emit OP deposit-tx fields (sourceHash/mint/isSystemTx) in combineTxResponse"
```

### Task 8: op-e2e — deposit tx[0] round-trip(R2-C 已核实插入点与计数)

**Files:**
- Modify: `tools/op-e2e/chain_driver.py`
- Modify: `tools/op-e2e/run_all.sh`(31→39)

**Interfaces:**
- Consumes:Task 7 字段;`chain_driver.py` 循环内 `blk_num`(:182,回执 blockNumber hex)与 `rpc.call`(:122-129)。
- Produces:断言 `eth_getBlockByNumber(blk_num, true)["transactions"][0]` 为 deposit 且 byHash round-trip 一致。

- [ ] **Step 1: 在 `chain_driver.py` `:193`(receipt cumulative>=used)后、循环内插入(R2-C:作用域确认)**

```python
# op-node derive.PayloadToBlockRef:this block's tx[0] must be a parseable deposit.
blk_full = rpc.call("eth_getBlockByNumber", [blk_num, True])   # 用回执 blk_num,非 "latest"(空块 IndexError)
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
> R2-C:`--txs 2` 默认 → 4 新断言×2=8 → **31→39**;`sourceHash` 加 `is not None` 避免 `None==None` tautology。
- [ ] **Step 2: 运行确认失败**
Run:`cd tools/op-e2e && python3 chain_driver.py` → 新断言红(`isSystemTx`/`sourceHash` 缺失)。
- [ ] **Step 3: 依赖 Task 7(若已合入则无额外代码)+ 更新 run_all.sh 注释 31→39**
- [ ] **Step 4: 全量 op-e2e → Step 5: Commit**
```bash
git add tools/op-e2e/chain_driver.py tools/op-e2e/run_all.sh
git commit -m "test(e2e): assert op-node block-ref deposit tx[0] round-trip (keyed by receipt block, 31->39)"
```

---

## Phase C — op-node 集成 harness(独立子系统,单独计划)

### Task 9: harness 组件拆解与骨架(依赖外部 infra)

> ⚠️ 依赖 optimism monorepo `op-e2e/actions` 或独立 L1 mock,建议单独计划。**前置:Task 3(全 attrs 采纳)必须先合入**,否则 harness 的确定性门必失败。

**组件(spec §7.1 + R1-C/R2-D):**
- (a) L1 mock:geth devnet 或 op-e2e L1 fixture,真实 `OptimismPortal` 发 `DepositEvent`;
- (b) L1 批量数据:op-batcher 或预播种;
- (c) op-node 派生:attrs 含 L1-info tx[0]+deposits+fork 升级 tx;
- (d) FCU **V3**→getPayload V4→newPayload V4→FCU 驱动 FISCO;
- (e) 确定性门:同 attrs 下 FISCO 块哈希与 op-geth 逐字节一致(Task 3/5 已把头字段对到 op-geth 期望);
- (f) deposit→L2 与 withdraw→MessagePasser→`eth_getProof`→finalize(**`eth_getProof` 是 op-node `L2EthClient` 硬依赖,预 Isthmus output root,跟踪为 P1**);
- (g) 重启与 reorg 韧性。
- [ ] **Step 1: 立项**(空桩 + 任务清单)→ **Step 2: 单独编写 harness 计划**。

---

## Self-Review(v4)

- **Round 3 合入核对**:Task 3 增 B1(getPayload 信封 PBBR)/B2(Isthmus blobGasUsed==0)/B3(BlockResponse PBBR)(R3-D);Task 3 修 `transactions` 类型为 `vector<bytes>`、`gasLimit` 新字段替代死字段 `targetGasLimit`、baseFee 热切换排序 + newPayload 回放检查用子块 extraData、`noTxPool` 接 mempool seal、extraData 版本字节按 fork(R3-A);Task 4 -38003 定为必交付(端点翻译)+ a1_active 基础 attrs 补 gasLimit 防回归(R3-C/D);Task 5 扩为完整 getPayload→newPayload 回环(R3-D);Task 6 修 util.cpp 路径 + nullopt 回退 + e2e ref 测试(R3-B/D);File Structure 补 EngineEndpoint.cpp/BlockResponse.cpp、修 util.cpp 路径。
- **Round 2 合入核对**:Task 3 升格为"全 attrs 采纳(核心)"(R2-D);新增 Task 5 构造对拍(R2-D)、Task 6 safe/finalized(R2-D);Task 0 改 VALID(R2-A);Task 1 修 optional + 协调 Task 4(R2-A/D);Task 2 16→21(R2-C);Task 7 全部 CONFIRMED(R2-B)。
- **Spec 覆盖**:P0 引擎契约(A)→Task 0-6;P0 deposit round-trip(B)→Task 7-8;P0 harness(C)→Task 9。**EF 语料扩 EEST(spec P0-并行)不在本计划范围**,单独轨道,Self-Review 明示以免"P0 全覆盖"失真(R3-C)。
- **占位符**:无 TBD/TODO;Task 4 的具体校验以 op-geth `checkOptimismPayloadAttributes` 为逐字段清单。
- **类型一致性**:`PayloadAttributes.transactions`(`vector<bytes>`)/`eip1559Params`/`minBaseFee`/`gasLimit`/`noTxPool`(新增)、`GetPayloadData.parentBeaconBlockRoot`(新增,B1)、`isSystemTx`/`combineTxResponse`/`fromQuantity`/`UnsupportedOpPayloadAttributes` 均与源码或 R3/R2 审查一致。
