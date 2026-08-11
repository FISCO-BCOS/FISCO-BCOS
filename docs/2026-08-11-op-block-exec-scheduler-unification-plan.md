# OP 块执行:engine 调用面统一 + 共识比对下沉 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 engine 内嵌的 OP 8 项 commitments 比对下沉为 opstack-executor 纯函数(可独立测试),engine 经 `SchedulerType::` 依赖名调用,并收敛统一调用面。

**Architecture:** 两个纯函数 `mismatchedFieldOf`/`announcedCommitmentsOf` 放 `OpEngineSeam.h`(纯 bcos:: 类型,不依赖 engine 库),由 `OpSchedulerImpl.h` re-publish 为 static 成员;engine 的 `handleOpNewPayload` 比对段(约 45 行)瘦身为一行依赖名调用,`if constexpr (c_opMode)` 分派收进 `executePayload()`。

**Tech Stack:** C++20 协程(bcos::task)、evmone/evmc、bcos-framework storage2/engine Types、Boost.Test、CMake/Unix Makefiles。

**前置:** `docs/op-block-exec-scheduler-unification-design.md`(已批准 + 4 agent 审查修订)。构建目录 `build/` 已配置(TESTS=ON)。

## Global Constraints

- **测试不可退步(强制)**:已通过测试集合改动后必须继续通过;测试只加强不退步。
- **seam 纯度**:`EngineServiceImpl.h` 是 public/install 模板头,永不拼写任何 `bcos::evm::engine` 类型名;一切 OP 能力经 `SchedulerType::` 依赖名 + `auto` 到达。
- **判定零改动**:8 项比对顺序、optional 语义(仅 computed 侧 has_value 才比)、字段名逐字保留;txRoot 槽返回 `"transactionsRoot"`(非 `"txRoot"`);`requestsHash` 用 `.value()`(对齐 engine),`blobGasUsed` 用 `*`(对齐 engine)。
- **模块边界**:opstack-executor 可 include `bcos-framework/engine/Types.h`(`ExecutionPayload` 定义在 **`namespace bcos::engine`**,全限定引用);不依赖 bcos-engine 库。
- **提交**:`git commit --no-verify`(clang-format 17 钩子漂移,与上游 clang-format 版本不一致)。

**测试基线(改动前已确认通过;实际计数经审查核实):**
```bash
./build/opstack-executor/tests/opstack-executor-tests          # GTest 9/9
./build/opstack-executor/tests/opstack-executor-block-tests    # Boost,全绿(含 t8n 127 向量 + e2e 套件)
./build/opstack-executor/tests/opstack-executor-detail-tests   # Boost 12/12
```

---

### Task 1: `mismatchedFieldOf` 纯函数 + 单测(TDD)

**Files:**
- Create: `opstack-executor/tests/OpMismatchedFieldTest.cpp`
- Modify: `opstack-executor/OpEngineSeam.h`(新增函数 + `#include <string>`)
- Modify: `opstack-executor/tests/CMakeLists.txt`(注册进 detail-tests **+ 加 `protocol-tars` 链接**——为 Task 2 前向)

**Interfaces:**
- Produces: `inline std::optional<std::string> bcos::evm::engine::mismatchedFieldOf(const OpBlockCommitments& computed, const OpBlockCommitments& announced)` — 返回不匹配字段名字面量,全匹配 `std::nullopt`。契约:按序 `receiptsRoot→logsBloom→withdrawalsRoot→stateRoot→gasUsed→txRoot→blobGasUsed→requestsHash`,首个不匹配即返回;txRoot 槽返回 `"transactionsRoot"`;blobGasUsed/requestsHash 仅当 **computed 侧 has_value** 时进入比较并解引用 announced 侧(blobGasUsed 用 `*`,requestsHash 用 `.value()`,逐字对齐 engine)。

- [ ] **Step 1: 写失败测试 `OpMismatchedFieldTest.cpp` + 注册进 CMakeLists(注册必须先于红步)**

```cpp
// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the OP commitments comparison pure function (OpEngineSeam.h): 8 fields,
// comparison order (first mismatch wins), the "transactionsRoot" literal, and the optional
// computed-side-only gating (blobGasUsed/requestsHash).

#include <opstack-executor/OpEngineSeam.h>
#include <boost/test/unit_test.hpp>

namespace bcos::evm::engine
{
namespace
{
using C = OpBlockCommitments;

C match()  // two identical default commitments: all-zero, both optionals nullopt
{
    return C{};
}

/// h256 with a distinctive first byte (deterministic, avoids hex-string ctor dependence).
bcos::h256 makeH256(bcos::byte firstByte)
{
    bcos::h256 h;
    h.data()[0] = firstByte;
    return h;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpMismatchedFieldSuite)

BOOST_AUTO_TEST_CASE(AllFieldsMatchReturnsNullopt)
{
    const C c = match();
    const C a = match();
    BOOST_CHECK(!mismatchedFieldOf(c, a).has_value());
}

BOOST_AUTO_TEST_CASE(ReportsReceiptsRootFirst)
{
    C c = match();
    C a = match();
    a.receiptsRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "receiptsRoot");
}

BOOST_AUTO_TEST_CASE(ReportsLogsBloomSecond)
{
    C c = match();
    C a = match();
    a.logsBloom.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "logsBloom");
}

BOOST_AUTO_TEST_CASE(ReportsWithdrawalsRoot)
{
    C c = match();
    C a = match();
    a.withdrawalsRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "withdrawalsRoot");
}

BOOST_AUTO_TEST_CASE(ReportsStateRoot)
{
    C c = match();
    C a = match();
    a.stateRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "stateRoot");
}

BOOST_AUTO_TEST_CASE(ReportsGasUsed)
{
    C c = match();
    C a = match();
    a.gasUsed = bcos::u256(1);
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "gasUsed");
}

BOOST_AUTO_TEST_CASE(TxRootSlotReportsTransactionsRootLiteral)
{
    C c = match();
    C a = match();
    a.txRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "transactionsRoot");  // NOT "txRoot"
}

BOOST_AUTO_TEST_CASE(FirstMismatchWins)
{
    C c = match();
    C a = match();
    a.receiptsRoot.data()[0] = 0x01;
    a.stateRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "receiptsRoot");
}

BOOST_AUTO_TEST_CASE(FirstMismatchWinsMidField)
{
    C c = match();
    C a = match();
    a.gasUsed = bcos::u256(1);      // field 5 differs
    a.txRoot.data()[0] = 0x01;      // field 6 also differs
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "gasUsed");  // mid-field order pinned
}

BOOST_AUTO_TEST_CASE(BlobGasUsedComparedOnlyWhenComputedHasValue)
{
    // computed nullopt + announced value → SKIP (pre-Jovian real path: seal.blobGasUsed nullopt,
    // payload.blobGasUsed=0).
    C c = match();
    C a = match();
    a.blobGasUsed = 1;
    BOOST_CHECK(!mismatchedFieldOf(c, a).has_value());

    // computed value + announced value different → compare
    C c2 = match();
    C a2 = match();
    c2.blobGasUsed = 1;
    a2.blobGasUsed = 2;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c2, a2), "blobGasUsed");

    // computed value + announced value equal → match
    C c3 = match();
    C a3 = match();
    c3.blobGasUsed = 7;
    a3.blobGasUsed = 7;
    BOOST_CHECK(!mismatchedFieldOf(c3, a3).has_value());
}

BOOST_AUTO_TEST_CASE(RequestsHashComparedOnlyWhenComputedHasValue)
{
    C c = match();
    C a = match();
    a.requestsHash = bcos::h256{};
    BOOST_CHECK(!mismatchedFieldOf(c, a).has_value());  // computed nullopt → SKIP

    C c2 = match();
    C a2 = match();
    c2.requestsHash = makeH256(0x01);
    a2.requestsHash = makeH256(0x02);
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c2, a2), "requestsHash");

    // equal → match (4-element matrix completed)
    C c3 = match();
    C a3 = match();
    c3.requestsHash = makeH256(0x09);
    a3.requestsHash = makeH256(0x09);
    BOOST_CHECK(!mismatchedFieldOf(c3, a3).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::engine
```

`opstack-executor/tests/CMakeLists.txt` 的 `opstack-executor-detail-tests` target 同步两处:源列表加 `OpMismatchedFieldTest.cpp`;**link 库加 `protocol-tars`**(Task 2 的投影测试要构造 `bcostars::protocol::BlockHeaderImpl`,其默认构造是 out-of-line,定义在 protocol-tars 库):
```cmake
target_link_libraries(opstack-executor-detail-tests PRIVATE
    opstack-executor codec evmone::evmone intx::intx Boost::unit_test_framework bcos-utilities protocol-tars)
```

- [ ] **Step 2: 跑测试确认失败(现在会真失败:测试已注册,`mismatchedFieldOf` 未定义)**

Run: `make -C build -j8 opstack-executor-detail-tests 2>&1 | tail -8 && ./build/opstack-executor/tests/opstack-executor-detail-tests --run_test=OpMismatchedFieldSuite`
Expected: FAIL — `mismatchedFieldOf` 未定义(编译/链接错误)。

- [ ] **Step 3: 实现 `mismatchedFieldOf`(OpEngineSeam.h)**

在 `OpEngineSeam.h` 的 `commitmentsOf` 之后追加(逐字复刻 engine 原比对语义,见设计 §1):

```cpp
/// Compares the executed block's commitments against the payload's announced commitments.
/// Returns the mismatching field name (first in comparison order), or nullopt if all match.
/// Contract (verbatim port of the engine's comparison block, zero judgment change):
///   - fields compared in order receiptsRoot → logsBloom → withdrawalsRoot → stateRoot →
///     gasUsed → txRoot → blobGasUsed → requestsHash; first mismatch wins;
///   - the txRoot slot reports the literal "transactionsRoot" (not "txRoot");
///   - blobGasUsed / requestsHash are compared only when the COMPUTED side has a value, and the
///     announced side is dereferenced (guaranteed present by the engine validation — see design
///     doc §4); computed-side nullopt skips regardless of announced.
inline std::optional<std::string> mismatchedFieldOf(
    const OpBlockCommitments& computed, const OpBlockCommitments& announced)
{
    if (computed.receiptsRoot != announced.receiptsRoot)
        return "receiptsRoot";
    if (computed.logsBloom != announced.logsBloom)
        return "logsBloom";
    if (computed.withdrawalsRoot != announced.withdrawalsRoot)
        return "withdrawalsRoot";
    if (computed.stateRoot != announced.stateRoot)
        return "stateRoot";
    if (computed.gasUsed != announced.gasUsed)
        return "gasUsed";
    if (computed.txRoot != announced.txRoot)
        return "transactionsRoot";
    if (computed.blobGasUsed.has_value() && *computed.blobGasUsed != *announced.blobGasUsed)
        return "blobGasUsed";
    if (computed.requestsHash.has_value() && *computed.requestsHash != announced.requestsHash.value())
        return "requestsHash";
    return std::nullopt;
}
```

`OpEngineSeam.h` 顶部 include 补 `<string>`。

- [ ] **Step 4: 跑测试通过**

Run: `make -C build -j8 opstack-executor-detail-tests 2>&1 | tail -5 && ./build/opstack-executor/tests/opstack-executor-detail-tests`
Expected: PASS — 原 12 用例 + 新 10 用例全绿(OpRlpDecodeTest/Storage2StateHelpersTest 不受影响)。

- [ ] **Step 5: 提交**

```bash
git add opstack-executor/OpEngineSeam.h opstack-executor/tests/OpMismatchedFieldTest.cpp opstack-executor/tests/CMakeLists.txt
git commit --no-verify -m "feat(opstack): add mismatchedFieldOf pure function + unit tests

Ports the OP 8-field commitments comparison out of EngineServiceImpl into a pure
function (OpEngineSeam.h), preserving field order / computed-side optional gating /
the literal 'transactionsRoot' field name. Adds protocol-tars to detail-tests links
(needed by the Task 2 projection test)."
```

---

### Task 2: `announcedCommitmentsOf` 纯函数 + 单测(TDD)

**Files:**
- Modify: `opstack-executor/tests/OpMismatchedFieldTest.cpp`(追加投影单测,**保持在同一 `namespace bcos::evm::engine` 块内**)
- Modify: `opstack-executor/OpEngineSeam.h`(新增 `announcedCommitmentsOf` + `payloadBloomToH2048` + include Types.h/OpRlpDecode.h)

**Interfaces:**
- Consumes: `bcos::engine::ExecutionPayload`(**注意命名空间**,bcos-framework/engine/Types.h)、`bcos::evm::engine::detail::narrowU256ToU64`(opstack-executor/OpRlpDecode.h)
- Produces: `inline OpBlockCommitments announcedCommitmentsOf(const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot, const bcos::protocol::BlockHeader& ethHeader)` — 5 项来自 payload,txRoot 来自参数,blobGasUsed 反向窄化(payload `optional<u256>` → 结构 `optional<uint64_t>`,narrow 全函数因为 validate 已保证 ≤ UINT64_MAX),requestsHash 来自 ethHeader。withdrawalsRoot/blobGasUsed 解引用由 engine 验证保证存在。

- [ ] **Step 1: 追加失败测试(OpMismatchedFieldTest.cpp 的既有 namespace 块内)**

```cpp
#include <bcos-framework/engine/Types.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>

namespace
{
bcos::engine::ExecutionPayload makePayload()
{
    bcos::engine::ExecutionPayload p;
    p.receiptsRoot = makeH256(0x11);
    p.logsBloom[0] = 0xaa;  // Bloom = std::array<byte,256>
    p.logsBloom[255] = 0xbb;
    p.withdrawalsRoot = makeH256(0x22);
    p.stateRoot = makeH256(0x33);
    p.gasUsed = bcos::u256(12345);
    p.blobGasUsed = bcos::u256(54321);
    return p;
}
}  // namespace

BOOST_AUTO_TEST_CASE(AnnouncedProjectsAllEightFields)
{
    const auto payload = makePayload();
    const auto txRoot = makeH256(0x44);
    bcostars::protocol::BlockHeaderImpl header;
    header.setRequestsHash(makeH256(0x55));  // BlockHeaderImpl::setRequestsHash(h256), non-optional

    const auto announced = announcedCommitmentsOf(payload, txRoot, header);
    BOOST_CHECK_EQUAL(announced.receiptsRoot, payload.receiptsRoot);
    BOOST_CHECK_EQUAL(announced.logsBloom.data()[0], 0xaa);       // byte-faithful bloom
    BOOST_CHECK_EQUAL(announced.logsBloom.data()[255], 0xbb);
    BOOST_CHECK_EQUAL(announced.withdrawalsRoot, *payload.withdrawalsRoot);
    BOOST_CHECK_EQUAL(announced.stateRoot, payload.stateRoot);
    BOOST_CHECK_EQUAL(announced.gasUsed, payload.gasUsed);
    BOOST_CHECK_EQUAL(announced.txRoot, txRoot);
    BOOST_REQUIRE(announced.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*announced.blobGasUsed, 54321u);            // u256 → uint64 narrow
    BOOST_REQUIRE(announced.requestsHash.has_value());
    BOOST_CHECK_EQUAL(*announced.requestsHash, makeH256(0x55));
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C build -j8 opstack-executor-detail-tests 2>&1 | tail -8 && ./build/opstack-executor/tests/opstack-executor-detail-tests --run_test=OpMismatchedFieldSuite`
Expected: FAIL — `announcedCommitmentsOf` 未定义。

- [ ] **Step 3: 实现 `announcedCommitmentsOf` + `payloadBloomToH2048`(OpEngineSeam.h)**

include 补:`<bcos-framework/engine/Types.h>`(ExecutionPayload)、`<opstack-executor/OpRlpDecode.h>`(detail::narrowU256ToU64,已确认无 include 循环)、`<array>`、`<cstring>`。在 `mismatchedFieldOf` 之前追加:

```cpp
/// Payload Bloom (std::array<byte,256>) → bcos::h2048, byte-faithful (mirrors the engine's
/// detail::toEthLogsBloom, moved OP-side with the projection). Named distinctly from the
/// existing detail::toBcosBloom (evmone::state::BloomFilter overload) to avoid confusion.
inline bcos::h2048 payloadBloomToH2048(const std::array<bcos::byte, 256>& bloom)
{
    bcos::h2048 out;
    std::memcpy(out.data(), bloom.data(), bloom.size());
    return out;
}

/// Projects the payload/header announced commitments into OpBlockCommitments (the "announced"
/// side of mismatchedFieldOf). 5 fields from ExecutionPayload, txRoot from the caller's
/// computeTxRoot, blobGasUsed reverse-narrowed (payload optional<u256> → optional<uint64_t>;
/// narrow is total because validateOpNewPayloadRequest already bounds it ≤ UINT64_MAX,
/// EngineServiceImpl.cpp:475-477), requestsHash from the rebuilt header. withdrawalsRoot /
/// blobGasUsed deref is safe: the engine validation guarantees them present (design doc §4).
inline OpBlockCommitments announcedCommitmentsOf(
    const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
    const bcos::protocol::BlockHeader& ethHeader)
{
    OpBlockCommitments out{
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = payloadBloomToH2048(payload.logsBloom),
        .withdrawalsRoot = *payload.withdrawalsRoot,
        .stateRoot = payload.stateRoot,
        .gasUsed = payload.gasUsed,
        .txRoot = transactionsRoot,
        .blobGasUsed = payload.blobGasUsed.has_value() ?
            std::optional<uint64_t>(bcos::evm::engine::detail::narrowU256ToU64(
                *payload.blobGasUsed, "ExecutionPayload.blobGasUsed")) :
            std::nullopt,
        .requestsHash = ethHeader.requestsHash(),
    };
    return out;
}
```

- [ ] **Step 4: 跑测试通过**

Run: `make -C build -j8 opstack-executor-detail-tests 2>&1 | tail -5 && ./build/opstack-executor/tests/opstack-executor-detail-tests`
Expected: PASS — 原 22 用例 + 新投影用例全绿(protocol-tars 已就绪,BlockHeaderImpl 链接通过)。

- [ ] **Step 5: 提交**

```bash
git add opstack-executor/OpEngineSeam.h opstack-executor/tests/OpMismatchedFieldTest.cpp
git commit --no-verify -m "feat(opstack): add announcedCommitmentsOf payload projection + tests

Projects ExecutionPayload/header commitments into OpBlockCommitments: byte-faithful
bloom (payloadBloomToH2048), blobGasUsed u256→uint64 reverse-narrow, requestsHash
from the rebuilt header."
```

---

### Task 3: OpSchedulerImpl seam re-publish

**Files:**
- Modify: `opstack-executor/OpSchedulerImpl.h`(seam 区 `commitmentsOf` 定义旁加 `CommitmentsT` 别名 + 两个 static re-publish + include Types.h)

**Interfaces:**
- Consumes: `OpBlockCommitments`、`mismatchedFieldOf`、`announcedCommitmentsOf`(Task 1/2)
- Produces: `typename SchedulerType::CommitmentsT`(= `OpBlockCommitments`)、`static CommitmentsT announcedCommitmentsOf(const bcos::engine::ExecutionPayload&, const bcos::h256&, const bcos::protocol::BlockHeader&)`、`static std::optional<std::string> mismatchedFieldOf(const CommitmentsT&, const CommitmentsT&)` — engine 以 `SchedulerType::` 依赖名访问。

- [ ] **Step 1: include + 别名 + re-publish(OpSchedulerImpl.h)**

include 补 `#include <bcos-framework/engine/Types.h>`(ExecutionPayload,同库,无循环)。在 seam 区 `commitmentsOf`(按名定位,勿依赖行号)定义旁追加:

```cpp
    /// Projection/comparison pair for the engine's eight-field commitments check. Re-published
    /// as static members so the engine reaches them as dependent names, keeping
    /// EngineServiceImpl.h free of any bcos-evm type spelling (seam purity).
    using CommitmentsT = bcos::evm::engine::OpBlockCommitments;
    static CommitmentsT announcedCommitmentsOf(const bcos::engine::ExecutionPayload& payload,
        const bcos::h256& transactionsRoot, const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::evm::engine::announcedCommitmentsOf(payload, transactionsRoot, ethHeader);
    }
    static std::optional<std::string> mismatchedFieldOf(
        const CommitmentsT& computed, const CommitmentsT& announced)
    {
        return bcos::evm::engine::mismatchedFieldOf(computed, announced);
    }
```

- [ ] **Step 2: 编译 opstack-executor + 现有 detail-tests(确认无破坏)**

Run: `make -C build -j8 opstack-executor opstack-executor-detail-tests 2>&1 | tail -5 && ./build/opstack-executor/tests/opstack-executor-detail-tests`
Expected: 编译通过,detail-tests 全绿(原用例不受影响)。

- [ ] **Step 3: 提交**

```bash
git add opstack-executor/OpSchedulerImpl.h
git commit --no-verify -m "feat(opstack): re-publish commitments projection/comparison on the scheduler seam

Exposes announcedCommitmentsOf + mismatchedFieldOf as SchedulerType static members
(CommitmentsT alias) so the engine reaches them as dependent names without including
opstack-executor headers (seam purity)."
```

---

### Task 4: engine 比对段瘦身

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(**整体替换 L1153-1201** 的 step-5 区域)

**Interfaces:**
- Consumes: `SchedulerType::commitmentsOf`、`SchedulerType::announcedCommitmentsOf`、`SchedulerType::mismatchedFieldOf`(Task 3)
- Produces: 无新接口;行为与现状逐字等价(INVALID + latestValidHash=parent,字段名 `"transactionsRoot"` 等)。

- [ ] **Step 1: 整体替换 step-5 区域(EngineServiceImpl.h L1153-1201)**

`runOpNewPayloadSteps` 中,`SchedulerType::commitmentsOf(*executeResult)`(L1153)声明、`std::optional<std::string> mismatchedField;` 声明、8 项 if/else 链(L1155-1196)、旧报告段 `if (mismatchedField.has_value()) { co_return ... }`(L1197-1201)——**整体**替换为:

```cpp
        // ---- Step 5: the eight-way comparison surface (sunk into the OP side) ----
        // Comparison semantics live in the scheduler seam (mismatchedFieldOf), so this branch
        // stays a thin dependent-name call — engine never spells any bcos-evm type.
        const auto commitments = SchedulerType::commitmentsOf(*executeResult);
        const auto announced =
            SchedulerType::announcedCommitmentsOf(payload, transactionsRoot, *ethHeader);
        if (auto mismatchedField = SchedulerType::mismatchedFieldOf(commitments, announced);
            mismatchedField.has_value())
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("execution result does not match payload field: ") +
                    *mismatchedField);
        }
```

**保留**:`latestValidHash`、`transactionsRoot`(L874 前置)、`ethHeader`。**保留 `detail::toEthLogsBloom` 的声明/定义**——它仍有第二活调用者 `rebuildOpEthHeader`(EngineServiceImpl.cpp:534),Task 4 不动 rebuildOpEthHeader,故不得删除(只删比对段中对该函数的调用)。

- [ ] **Step 2: 编译 engine + init**

Run: `make -C build -j8 init engine 2>&1 | tail -5`
Expected: 编译通过(通用 TU 与 OP 组合根均实例化;`static_assert(c_opMode)` 成立,证明 seam purity 未破)。

- [ ] **Step 3: 跑 block-tests(e2e 判定回归)**

Run: `make -C build -j8 opstack-executor-block-tests 2>&1 | tail -3 && ./build/opstack-executor/tests/opstack-executor-block-tests`
Expected: 全绿 — e2e 已覆盖 3/8 项比对错误串(stateRoot/gasUsed/receiptsRoot,`validation_error_contains` 断言),保持不变;t8n 127 向量(执行/seal 不变)全绿。

- [ ] **Step 4: 提交**

```bash
git add engine/bcos-engine/EngineServiceImpl.h
git commit --no-verify -m "refactor(engine): sink the OP commitments comparison into the scheduler seam

Replaces the ~45-line eight-field if/else chain in the OP newPayload branch with a
thin dependent-name call (SchedulerType::announcedCommitmentsOf + mismatchedFieldOf).
Comparison order / optional semantics / field-name literals unchanged — verified by
e2e (3/8 covered fields) + the pure-function unit tests. toEthLogsBloom kept: still
used by rebuildOpEthHeader."
```

---

### Task 5: `executePayload` 统一调用面收敛

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(`handleNewPayload` 分派收进私有 `executePayload`)

**Interfaces:**
- Consumes: `handleOpNewPayload`(原样搬入)、通用分支(原样搬入)
- Produces: 私有 `bcos::task::Task<PayloadStatus> executePayload(const NewPayloadRequest& request, std::uint32_t version)` — handleNewPayload 只做版本 gate + 委托。**弱依赖 Task 4**(仅顺序编排;两函数正交,Task 4 改 runOpNewPayloadSteps,Task 5 改 handleNewPayload)。

- [ ] **Step 1: 抽出 `executePayload`(EngineServiceImpl.h)**

`handleNewPayload`(L583)改为:保留 L586-613(版本 gate + `!c_opMode` V4 守卫)不变,将 L617 起的分派(OP 分支 `co_return co_await handleOpNewPayload(request, version);` + else 通用段 L621-758)整体搬入新私有方法:

```cpp
    /// Unified block-execution dispatch: [OP] handleOpNewPayload, [generic] the pre-existing
    /// body. Keeps handleNewPayload to version-gate + delegation for readability (behavior-neutral).
    bcos::task::Task<PayloadStatus> executePayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        if constexpr (c_opMode)
        {
            co_return co_await handleOpNewPayload(request, version);
        }
        else
        {
            // 原通用段逐行搬入(L621-758),一字不改
            ...
        }
    }
```

`handleNewPayload` 末尾:删掉搬走的分派,改为 `co_return co_await executePayload(request, version);`。`c_opMode` SFINAE probe 不动。本任务为**重构,无新测试**,验证 = 编译 + 既有套件。

- [ ] **Step 2: 编译 engine + init**

Run: `make -C build -j8 init engine 2>&1 | tail -5`
Expected: 编译通过(通用 + OP 组合根均实例化 executePayload)。

- [ ] **Step 3: 跑 block-tests + test-bcos-engine(通用回归)**

Run: `make -C build -j8 opstack-executor-block-tests test-bcos-engine 2>&1 | tail -3 && ./build/opstack-executor/tests/opstack-executor-block-tests && ./build/engine/test/test-bcos-engine`
Expected: block-tests 全绿(OP 路径);test-bcos-engine 全绿(EngineServiceTest 通用 newPayload,裸跑全用例)。

- [ ] **Step 4: 提交**

```bash
git add engine/bcos-engine/EngineServiceImpl.h
git commit --no-verify -m "refactor(engine): fold the c_opMode dispatch into executePayload()

Behavior-neutral readability refactor: handleNewPayload keeps version gating +
delegation; the OP/generic dispatch moves into a private executePayload(). Verified
by block-tests (OP) + test-bcos-engine (generic newPayload)."
```

---

### Task 6: 全量回归(基线对比)

**Files:**
- 无代码改动;验证 + 可选文档。

- [ ] **Step 1: 跑全部相关测试集合,逐项对比基线**

Run:
```bash
./build/opstack-executor/tests/opstack-executor-tests          # GTest 9/9 不变
./build/opstack-executor/tests/opstack-executor-block-tests    # Boost 全绿不变
./build/opstack-executor/tests/opstack-executor-detail-tests   # 原 12 + 新增 11 全绿
./build/engine/test/test-bcos-engine                            # 通用 engine 回归
```
Expected: 全部通过;无一项原本绿的变红。

- [ ] **Step 2: 补跑通用路径回归(include EngineServiceImpl.h,Task 4/5 改动后重编译)**

Run(若 target 存在):
```bash
make -C build -j8 test-transaction-scheduler bcos-evm-opstack-tests 2>&1 | tail -5
./build/transaction-scheduler/tests/test-transaction-scheduler   # TestEthereumExecutorScheduler L887 真实驱动通用 newPayload
./build/bcos-evm/test/bcos-evm-opstack-tests                     # OpEnvelopeToTarsTest include EngineServiceImpl.h
```
Expected: 编译通过 + 全绿(test-transaction-scheduler 是最强通用路径回归:真实 `EngineServiceImpl<MemPoolImpl, EEMultiLayerStorage, EthereumExecutor, SchedulerSerialImpl>` 驱动 newPayload)。

- [ ] **Step 3: 更新设计文档状态**

`docs/op-block-exec-scheduler-unification-design.md` 的"状态"行更新为实施完成 + 验证结果。

- [ ] **Step 4: 提交**

```bash
git add docs/op-block-exec-scheduler-unification-design.md
git commit --no-verify -m "docs(engine): mark OP block-exec unification design as implemented"
```

---

## 自检记录

- **Spec 覆盖**:spec §1(纯函数)→ Task 1/2;§2(SchedulerType 通道)→ Task 3;§2 engine 瘦身 → Task 4;§3(executePayload)→ Task 5;§测试/验证 → Task 1/2/6;§4(前置契约)→ Task 1 测试 + Task 4 保留。全部有任务对应。
- **占位符**:无 TBD/TODO;每步含实际代码/命令。Task 5 的"原通用段逐行搬入(...)"是引用原代码的搬运指令(非占位符),实施时从 L621-758 原样复制。
- **类型一致性**:`bcos::engine::ExecutionPayload`(Task 2/3 签名、engine 侧调用一致);`CommitmentsT`/`mismatchedFieldOf`/`announcedCommitmentsOf` 在 Task 1/2/3 签名一致;`payload.logsBloom`/`blobGasUsed` 字段与 Types.h 定义匹配;`BlockHeaderImpl::setRequestsHash(h256)` 非 optional,与测试匹配。
- **审查修订(4 agent)已合并**:ExecutionPayload 命名空间、protocol-tars 链接、Task 4 整体替换 + 去重 commitments、toEthLogsBloom 保留(rebuildOpEthHeader)、Task 1 红步顺序、Task 2 namespace 位置、请求 hash `.value()`、`payloadBloomToH2048` 改名、Task 6 回归面补 test-bcos-engine/test-transaction-scheduler/bcos-evm-opstack-tests、用例计数、blobGasUsed 窄化前提(validate 已界 ≤ UINT64_MAX)。
