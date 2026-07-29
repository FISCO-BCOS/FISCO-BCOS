# Task 3 报告:EthBlockHeader + OpDepositEncode(bcos-codec)+ 金值单测

## 状态:**首次提交未编译验证;统一编译验证阶段追加修复,已编译通过**

原始提交按用户执行协议:开发期跳过 FISCO 编译/测试运行——代码与测试全部照写照提交,
但不 `cmake --build`、不 `ctest`。TDD 的"先红后绿"以"测试先写并提交"替代。自审改为
"逐字段对照 golden 数据静态走查编码逻辑",记录见下文,并额外用独立 Python 复刻同一
套字段映射/RLP 规则,对 Task 2 的全部 33 条 golden + 39 笔 deposit 逐字节比对(见
"静态走查 + 独立复刻验证"一节)——这是无编译条件下能拿到的最强正确性证据,但不等价
于 ctest 绿灯,不构成"测试通过"的断言。

**统一编译验证阶段(见文末"编译验证修复记录"一节)**:协调者报告了一个真实编译错误
(命名遮蔽),本轮已定位、修复、并实际跑了 `cmake --build`(而非静态走查)确认
`codec` target 与 `EthBlockHeaderTest.cpp.o` 编译通过——上面"未编译验证"的表述对
**首次提交时点**仍然如实,但当前代码状态已经过真实编译器验证,不再是纯静态审查结论。

## 实现要点

### `bcos-codec/bcos-codec/rlp/EthBlockHeader.{h,cpp}`

- `EthBlockHeader` 21 个 public 字段,类型按 brief 逐字段对齐(`bcos::h256`/`u256`/
  `uint64_t`/`bcos::bytes`/`bcos::Address`),另外 3 个协议常量字段(`ommersHash`/
  `difficulty`/`nonce`)也是**真实成员**而非硬编码进 `encode()`——测试侧用
  post-merge/PoS 协议常量(keccak256(rlp([]))/0/8 零字节)填充,不是"自算自证",
  因为向量语料本就不携带这三个字段,op-geth 自身对这三者也是硬编码。
- `encode()`:单次调用既有 `RLPEncode.h` 的可变参数 `encode(bytes&, Args...)`,
  21 个字段按 spec §5.1 顺序一次性传入,产出单个顶层 RLP list——字段顺序是唯一的
  正确性关键点,已用手工 RLP 走查 + Python 复刻双重核对(见下文)。
- `hash()` = `keccak256Hash(ref(encode()))`,复用 `bcos-crypto/hash/Keccak256.h`
  的 `bcos::crypto::keccak256Hash(bytesConstRef)`——`codec` target 已 `PUBLIC`
  链接 `bcos-crypto`(`bcos-codec/CMakeLists.txt:33`,原有),不需要新增依赖边。
- 字段内部单纯声明为 public,不做校验(如 blobGasUsed 在 Isthmus 必须为 0 之类的
  不变式是调用方职责,注释里已标注该分工,避免误读成"本结构自带校验")。

### `bcos-codec/bcos-codec/rlp/OpDepositEncode.{h,cpp}`

- `OpDepositFields`(8 字段,对齐 op-geth `core/types/deposit_tx.go` DepositTx /
  RLP 顺序):`sourceHash`(h256)、`from`(Address)、`to`(`std::optional<Address>`,
  nullopt = contract-creation)、`mint`(u256,默认 0——nil 与 0 在 RLP 层字节不可区分,
  故未用 optional 包装)、`value`(u256)、`gas`(uint64_t)、`isSystemTransaction`
  (bool)、`data`(bytes)。
- `encodeDepositEnvelope()`:`0x7E` 前缀 + 8 字段一次 RLP list 调用;`to` 的
  optional 分支单独处理(有值传 `Address`,无值传空 `bytesConstRef{}`——两者 RLP
  形状不同,不能用"零地址代表缺席"这种化简,已在头注释里显式记录这条陷阱)。
- **修正记录(自审中发现并修正)**:最初以为 39 笔 deposit 全部 `to` 有值(据
  `"to" in dep` 的 Python 扫描误判),写下"无 contract-creation deposit 样本"的
  注释。用独立 Python 复刻脚本跑全量对比时才发现 `isthmus_contract_create`/
  `jovian_contract_create` 的 tx index 1 里 `_op_deposit.to` 是 **JSON `null`**
  (键存在但值为 null,不是键缺失)。C++ 测试原代码 `dep.contains("to")` 单独判断
  会在这两条上把 `null` 误当字符串传给 `asAddress()`,编译期不报错但运行期会因
  nlohmann::json 类型不匹配抛异常——已改为 `dep.contains("to") &&
  !dep.at("to").is_null()`,并补一条专门测试
  `OpDepositEncode.ContractCreationDepositNilTo` 钉住这条路径,同时更正了三处
  沿用旧结论的注释。这是本任务唯一一处"写完之后被自己的走查证伪"的地方。

### CMake

- `bcos-codec/CMakeLists.txt`:新增 `aux_source_directory(bcos-codec/rlp SRC_LIST)`
  ——此前 `bcos-codec/rlp/` 只有头文件(`Common.h`/`RLPEncode.h`/`RLPDecode.h`),
  没有被任何 `aux_source_directory`/`GLOB` 规则纳入,新增的两个 `.cpp` 不加这行不会
  被编译进 `codec` target。
- `bcos-evm/test/CMakeLists.txt`:
  - `EthBlockHeaderTest.cpp` 加入 `if(TARGET bcos-framework)` 门控块(同
    `Storage2LedgerTest.cpp` 等)——不是因为它依赖 `bcos-framework` 本身,而是
    `bcos-codec` 的 `codec` target 只在完整 CMake 树(根 `CMakeLists.txt` 先
    `add_subdirectory(bcos-codec)` 后 `add_subdirectory(bcos-evm)`)下存在,standalone
    构建(`bcos-evm/build`,独立 `vcpkg.json`)没有这条依赖边,而这个条件与
    `bcos-framework` 目标存在性恰好重合,故复用同一个门控块,并在其中追加
    `target_link_libraries(... PRIVATE codec)`。
  - 新增两个编译期路径宏 `OP_T8N_CASES_DIR`/`OP_T8N_GOLDEN_ENGINE_DIR`(与既有
    `OP_T8N_VECTORS_DIR` 同構),分别指向 `t8n/cases`、`t8n/golden/engine`。

### clang-format

按 spec §4.6(a) 移植纪律,本仓 `.git/hooks/pre-commit` 会在提交时对本次改动的
`.h/.cpp` 跑 `clang-format -style=file:.clang-format --dry-run --Werror` 门禁
(不达标直接拒绝提交,即使随后会原地格式化)。提交前手动跑了同一条命令:初次对新增
5 个文件(4 个 `bcos-codec` 源 + 1 个测试文件)有若干处不合规(主要是单行函数体、
多参数调用换行位置),已用 `clang-format -style=file:.clang-format -i` 原地格式化
并重新 `--dry-run --Werror` 确认 CLEAN。CMakeLists.txt 改动不在 hook 的
`\.[ch](pp)?$` 匹配范围内,无需处理。

## 静态走查 + 独立复刻验证(无编译条件下的自审记录)

### (1)手工 RLP 结构走查(逐字节解剖一条 golden)

取 `isthmus_transfer_basic.golden.json` 的 `encodedHeaderHex`
(`0xf90267a045daac1c...e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`,
1236 hex 字符 = 618 字节),用一次性 Python RLP 解码脚本(标准 RLP 长度前缀规则,
不依赖任何项目代码)拆出顶层 list 的 21 个子项,逐项核对:

| # | 字段(spec §5.1 顺序) | 解出字节 | 核对依据 |
|---|---|---|---|
| 0 | parentHash | 32B `45daac1c...` | == 向量 `env.parentHash` |
| 1 | ommersHash | 32B `1dcc4de8...` | == keccak256(rlp([])) 已知常量 |
| 2 | feeRecipient | 20B `4200...11` | == 向量 `env.currentCoinbase` |
| 3 | stateRoot | 32B `0789f0af...` | == 向量 `_op_expected.header.stateRoot` |
| 4 | transactionsRoot | 32B `fa426233...` | == `golden.transactionsRoot`(独立字段,交叉印证顺序无误) |
| 5 | receiptsRoot | 32B `fc08bae9...` | == 向量 `_op_expected.header.receiptsRoot` |
| 6 | logsBloom | 256B 全零 | 长度精确匹配 h2048;头字节 `b90100` = 长字符串前缀 + 0x0100=256 |
| 7 | difficulty | 空串(=0) | PoS 常量 |
| 8 | number | 1B `01` = 1 | == 向量 `env.currentNumber` |
| 9 | gasLimit | 3B `989680` = 10,000,000 | == 向量 `env.currentGasLimit`(= genesis.gasLimit) |
| 10 | gasUsed | 2B `acd0` = 44,240 | == 向量 `_op_expected.header.gasUsed` |
| 11 | timestamp | 2B `03f2` = 1010 | == 向量 `env.currentTimestamp`(genesis.timestamp=1000+10) |
| 12 | extraData | 9B `000000003200000006` | == `golden.extraData`(Isthmus 9B,version=0x00,denom=0x32=50,elasticity=6,与 genesis 的 eip1559Denominator/Elasticity 一致) |
| 13 | prevRandao | 32B 全零 | == 向量 `env.currentRandom`("0x0") |
| 14 | nonce | 8B 全零 | PoS 常量 |
| 15 | baseFeePerGas | 4B `3a699d00` | == 向量 `env.currentBaseFee` |
| 16 | withdrawalsRoot | 32B `56e81f17...` | == 向量 `_op_expected.header.withdrawalsRoot`,且等于已知的"空 trie 根"常量 |
| 17 | blobGasUsed | 空串(=0) | == 向量 `_op_expected.header.blobGasUsed`("0x0") |
| 18 | excessBlobGas | 空串(=0) | == `golden.excessBlobGas`("0x0") |
| 19 | parentBeaconBlockRoot | 32B `0b0b0b...0b` | == 向量 `env.parentBeaconBlockRoot`,also == case json 的 `parentBeaconBlockRoot` |
| 20 | requestsHash | 32B `e3b0c442...` | == sha256("") 已知常量,== 向量 `_op_expected.header.requestsHash` |

21 个字段、字段顺序、每字段的编码规则(定长值用 FixedBytes 字节串编码、变长值用
scalar/字符串编码)全部与 `EthBlockHeader::encode()` 的实现一一对应,且
`transactionsRoot`(golden 独立来源)、`parentBeaconBlockRoot`(向量与 case json
双来源)、`requestsHash`(已知数学常量)三处有交叉印证,不是单一数据源的自洽循环。

同样对 `isthmus_deposit_mint.golden.json` 的 `rawTransactions[1]`(带 mint/value
的 deposit)做了逐字段 RLP 解码,确认 8 字段顺序为
`[sourceHash, from, to, mint, value, gas, isSystemTransaction, data]`,mint
(`1bc16d674ec80000`)在 value(`de0b6b3a7640000`)之前,与 `OpDepositFields`/
`encodeDepositEnvelope()` 的字段顺序一致。

### (2)独立 Python 复刻:全量 33 条 + 39 笔逐字节比对

手工走查只能覆盖一条样本,不足以担保 33 条金值全部字段来源(env/`_op_expected.header`
的具体取值)在类型转换、大小端、u64 溢出边界等细节上都正确。为此额外写了一份**独立于
C++ 实现**的 Python RLP 编码器(标准 RLP 规则,未复用/未参照本次新写的 C++
代码),复刻与 `EthBlockHeaderTest.cpp::buildHeader`/`buildDepositFields` 完全相同的
字段映射规则,对 Task 2 golden 目录的全部 33 条向量 + `cases/*.in.json` 的全部
206 笔交易(39 笔 deposit + 167 笔非 deposit)跑了一遍:

```
ids: 33
header-encode pass: 33/33
block-hash pass: 33/33
deposit count: 39 (expect 39)
deposit pass: 39/39
```

脚本:`scratchpad/simulate_eth_header_rlp.py`(会话临时文件,未入库,不属于本次
7 个提交文件之列)。这个复刻在过程中揪出了上面"修正记录"提到的 `to` 字段 JSON
`null` 误判问题(Python 版最初直接对 `None.startswith` 崩溃,追查后发现是数据
本身有 2 条 null-to 样本,不是脚本 bug)——这个发现同步修正回了正式的 C++ 测试
文件(`buildDepositFields` 的 `is_null()` 判断)。

这不是"跑通了 C++ 代码"(仍然未编译),而是"字段映射规则的另一套独立实现,在同一批
golden 数据上给出与手工走查一致、且覆盖全量的结果",是本任务无编译条件下能拿到的
最强正确性证据的补充。

### 已知未覆盖

- `blobGasUsed`(Isthmus 应恒为 0)/`excessBlobGas` 的**不变式校验**不在本结构职责
  内(spec 已注明是调用方——engine newPayload 分支——的职责,Task 5b/6 覆盖)。
- `EthBlockHeader`/`OpDepositFields` 尚未实际编译,可能存在 clang++ 的 concept
  匹配、模板推导等编译期问题未被发现(RLPEncode.h 现有的 `uint64Encode`/
  `uint256Encode` 单测已验证过 `encode(bytes&, UnsignedByte auto)` 对 `uint64_t`/
  `u256` 的行为,但本任务新增的 `bool→uint8_t` 转换、`FixedBytes<N>` 从
  `bcos::fromHex()` 返回值构造、21/8 参数的可变参数模板展开等具体调用点未经编译器
  验证)。

## 测试清单(**未编译验证**)

文件:`bcos-evm/test/opstack/EthBlockHeaderTest.cpp`(新增,进
`bcos-evm/test/CMakeLists.txt` 的 `if(TARGET bcos-framework)` 门控块)

套件 `EthBlockHeader`(4 例):
1. `IsthmusSingleTxTransferBasic` —— Step 1 TDD 三选样之一
2. `JovianMultiTxTransferMulti` —— Step 1 TDD 三选样之二
3. `DepositOnlyIsthmus` —— Step 1 TDD 三选样之三
4. `AllThirtyThreeGoldenVectors` —— Step 3,33 条全量 `encode()`+`hash()` 双断言

套件 `OpDepositEncode`(4 例):
1. `IsthmusTransferBasicDepositEnvelope` —— Step 1 TDD deposit 腿
2. `DepositWithMintAndValue` —— mint/value 非默认值分支
3. `ContractCreationDepositNilTo` —— `to`=nullopt 分支(真实 golden 样本,非合成)
4. `AllDepositTransactionsAcrossThirtyThreeGoldenVectors` —— Step 3,33 条向量的
   全部 39 笔 deposit 逐字节重建比对,闭合 Task 2 自检 (b) 的 deposit 半部

共 8 个 `TEST`,两个套件名钉死为 `EthBlockHeader`/`OpDepositEncode`(裁定 C6)。

## Commit

```
feat(bcos-codec): EthBlockHeader RLP/keccak + OP deposit envelope 编码(33 条 op-geth 金值锚定,extraData 原样发射)
```

精确路径(7 个文件,无宽路径 `add -A`):
- `bcos-codec/CMakeLists.txt`(modified,新增 `aux_source_directory(bcos-codec/rlp ...)`)
- `bcos-codec/bcos-codec/rlp/EthBlockHeader.h`(new)
- `bcos-codec/bcos-codec/rlp/EthBlockHeader.cpp`(new)
- `bcos-codec/bcos-codec/rlp/OpDepositEncode.h`(new)
- `bcos-codec/bcos-codec/rlp/OpDepositEncode.cpp`(new)
- `bcos-evm/test/CMakeLists.txt`(modified,新增源文件 + `codec` 链接 + 2 个路径宏)
- `bcos-evm/test/opstack/EthBlockHeaderTest.cpp`(new)

`ports/`、`vectors/` 零触碰(本任务未涉及这两个目录,`git status` 已核对无宽路径
误吸)。

## 编译验证修复记录(统一编译验证阶段,追加提交)

### 错误 1(协调者报告,命名遮蔽)

原文:

```
bcos-codec/bcos-codec/rlp/EthBlockHeader.cpp:34:12: error: too many arguments to function call, expected 0, have 22
note: 'encode' declared here — bcos::bytes EthBlockHeader::encode() const  (:27)
```

**根因**:`EthBlockHeader::encode() const` 成员函数体内调用自由函数
`encode(out, parentHash, ..., requestsHash)`(22 个实参)时用的是**无限定名**
`encode`。C++ 非限定名查找规则:在成员函数体内,查找从当前作用域(该成员函数所属的
类作用域)开始——类自身就有一个叫 `encode` 的成员(正在定义的这个 0 参函数本身),
一旦在类作用域找到同名实体,查找立即停止,**不会**继续去外层命名空间作用域找
`RLPEncode.h` 里声明的自由函数重载集,也不会触发 ADL(ADL 只在普通查找完全找不到
类成员/块作用域函数声明时才补充生效)。于是 22 参调用被绑定到 0 参的成员
`EthBlockHeader::encode()` 本身,直接编译期报错"expected 0, have 22"。这不是重载
决议失败(没有产生候选集竞争),而是**名字在候选集组建之前就已经被类作用域遮蔽掉**——
`RLPEncode.h` 里的自由 `encode` 重载集根本没进入本次查找的候选范围。

先前的静态走查/Python 复算之所以都没发现:两者都只验证"字段顺序/类型/编码规则"这一
层语义,不检查 C++ 名字查找/作用域规则(Python 复刻是另一门语言,没有"成员函数与自由
函数同名互相遮蔽"这个概念可比对);UNITY_BUILD 把 `bcos-codec` 全部源文件揉进一个
翻译单元编译,才第一次真正调用 clang 前端对这份调用做名字消解,暴露出来。

**修法**:把该调用改成完全限定名 `bcos::codec::rlp::encode(...)`——从命名空间根
（`bcos::codec::rlp`)开始的限定查找不经过类作用域链,直接命中 `RLPEncode.h` 声明的
自由函数重载集,消除遮蔽。`hash()` 里 `auto encoded = encode();` 保持无限定名不变——
这里就是要调用自身的成员 `encode()`,语义上不需要改。

同类排查:`OpDepositEncode.cpp` 的 `encodeDepositEnvelope()` 是**自由函数**、且
函数名与 `encode` 不同名,不存在类作用域遮蔽的可能——但既然协调者明确要求同类问题
一并检查,该文件里两处 `encode(out, ...)` 调用同样改成了完全限定的
`bcos::codec::rlp::encode(...)`,纯防御性加固(消除"以后 `encodeDepositEnvelope`
被改造成某个类的成员"这类假设性风险),并在代码注释里写明了"这里其实不会遮蔽,但
仍限定"的理由,避免误导后续读者以为这里也曾经报错。

### 错误 2(修复错误 1 后,构建暴露的第二个真实错误,同样定位并修复)

第一次 `cmake --build build --target bcos-evm-opstack-tests -j8` 在修好错误 1 后
重新触发,`codec` target 报出:

```
/bcos-utilities/bcos-utilities/DataConvertUtility.h:305:14: error: inline function 'bcos::toCompactBigEndian' is not defined [-Werror,-Wundefined-inline]
  305 | inline bytes toCompactBigEndian(byte _val, unsigned _min = 0);
note: used here (bcos-codec/bcos-codec/rlp/RLPEncode.h:72)
```

**根因**:`bcos-codec/rlp/` 目录此前只有头文件,从未被任何 `.cpp` 编译进
`codec` 库自身的翻译单元——`RLPEncode.h` 的模板此前只在**另一个可执行文件**
(`bcos-codec/test/unittests/RLPTest.cpp`)里被实例化过。本任务新增
`EthBlockHeader.cpp`/`OpDepositEncode.cpp` 后,`RLPEncode.h` 第一次在 `codec`
库自己的 UNITY_BUILD 翻译单元里被真正实例化。`OpDepositEncode.cpp` 里
`isSystemTransaction ? 1 : 0` 原写作 `const uint8_t`(= `bcos::byte`)存入
`isSystemTransactionByte`,传给 `RLPEncode.h` 的
`encode(bytes&, UnsignedByte auto const& b)` 模板,内部调用
`toCompactBigEndian(b)`——当 `T == byte` 时,重载决议在"完全匹配的非模板重载"
(`bcos-utilities/DataConvertUtility.h:305` 的
`inline bytes toCompactBigEndian(byte, unsigned = 0);`,只有声明没有内联定义,真正
定义体在 `DataConvertUtility.cpp:101`,不属于 `codec` 的翻译单元)与"完全匹配的模板
特化"(同文件 line 293,头内完整定义)之间,按 C++ 决议规则**优先选非模板重载**——
选中了那个只声明未在本 TU 内定义、又被标了 `inline` 的非模板函数,触发
`-Wundefined-inline`(`-Werror` 下即报错)。这是 `bcos-utilities` 头文件的一个
既存边界缺陷(`inline` 修饰的函数只有单一 out-of-line 定义在另一个库自己的 `.cpp`
里,不满足"inline 函数应在所有使用它的翻译单元内可见其定义"的通常预期),此前没有
任何代码在 `codec` 库自身范围内以 `byte`/`uint8_t` 类型实参调这条路径,所以从未
暴露;不在本任务文件范围内(`bcos-utilities` 属另一模块),按协调者"只修
EthBlockHeader.cpp/OpDepositEncode.cpp 相关的"要求,选择在调用侧规避而非改
`bcos-utilities`。

**修法**:`OpDepositEncode.cpp` 里 `isSystemTransactionByte` 的类型从 `uint8_t`
改为 `uint32_t`——只有类型宽度变化,数值语义(0/1)与最终 RLP 字节完全不变(
`RLPEncode.h` 的标量 encode 只按值比较 0/128 阈值,与 C++ 操作数宽度无关),但
`T == uint32_t` 时不存在与之完全匹配的非模板重载,重载决议直接选中头内完整定义的
模板,不再 odr-use 那条只声明未定义的 `byte` 重载。

### 编译输出(实际跑过,非静态推断)

```
$ cmake --build build --target bcos-evm-opstack-tests -j8
...
[ 34%] Building CXX object bcos-codec/CMakeFiles/codec.dir/Unity/unity_0_cxx.cxx.o
[ 65%] Linking CXX static library libcodec.a
[ 65%] Built target codec
...
[ 96%] Building CXX object bcos-evm/test/CMakeFiles/bcos-evm-opstack-tests.dir/opstack/EthBlockHeaderTest.cpp.o
...
```

`codec` target(本任务的主体交付物)与 `EthBlockHeaderTest.cpp.o`(本任务新增测试)
均编译通过、无警告无错误。`bcos-evm-opstack-tests` 整体链接目标随后失败,但失败点
全部在 `EngineVersionGateTest.cpp`/`EngineOpBranchTest.cpp`/
`EngineNewPayloadGateTest.cpp` 三个文件(`SchedulerType::ExecuteResult` 缺失、
`NewPayloadRequest::executionRequests` 缺失、`std::string→bytesConstRef` 隐式转换
缺失)——这些是后续任务(OpSchedulerImpl/EngineServiceImpl OP 分支,7-task 计划里
排在 Task 3 之后)引入的接口缺口,与 `EthBlockHeader`/`OpDepositEncode` 无关,不在
本任务修复范围内(协调者原话:"那些不归你")。用
`cmake --build build --target codec -j8` 单独确认 `codec` target 干净重编亦通过。

修复后重新跑了一次 `clang-format -style=file:.clang-format --dry-run --Werror`
(pre-commit hook 会做的同一检查),确认改动后的两个 `.cpp` 仍然 CLEAN。
