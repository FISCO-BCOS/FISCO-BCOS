#pragma once

// 共享测试头：OpBlockHarnessTest.cpp 与 OpBlockExecuteTest.cpp 原各自持有一份逐字节相同的
// L1 attributes 打包 helper（M-B2 plan Task 3 Step 1，纯搬移，函数体零改动）。
// inline 避免多 TU ODR 冲突。

#include <cstddef>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <vector>

namespace bcos::evm::opstack::testhelpers
{
/// 造一个 32 字节大端 word：在 [byteOff, byteOff+len) 放入 value 的低 len 字节。
inline evmc::bytes32 wordWith(size_t byteOff, uint64_t value, size_t len)
{
    evmc::bytes32 w{};
    for (size_t i = 0; i < len; ++i)
    {
        w.bytes[byteOff + len - 1 - i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
    return w;
}

inline evmc::bytes32 slotKey(uint8_t slot)
{
    evmc::bytes32 k{};
    k.bytes[31] = slot;
    return k;
}

inline evmc::bytes32 packUint256Low8(uint64_t value)
{
    return wordWith(24, value, 8);
}

/// slot3 打包：base_fee_scalar @ bytes[16,20)，blob_base_fee_scalar @ [20,24)。
inline evmc::bytes32 packSlot3(uint32_t baseScalar, uint32_t blobScalar)
{
    evmc::bytes32 w = wordWith(16, baseScalar, 4);
    const auto blob = wordWith(20, blobScalar, 4);
    for (size_t i = 0; i < 32; ++i)
    {
        w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | blob.bytes[i]);
    }
    return w;
}

/// slot8 打包：operator_fee_scalar @ bytes[20,24)，operator_fee_constant @ [24,32)。
inline evmc::bytes32 packSlot8(uint32_t opScalar, uint64_t opConst)
{
    evmc::bytes32 w = wordWith(20, opScalar, 4);
    const auto c = wordWith(24, opConst, 8);
    for (size_t i = 0; i < 32; ++i)
    {
        w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | c.bytes[i]);
    }
    return w;
}

inline void writeWord(std::vector<uint8_t>& data, size_t offset, const evmc::bytes32& word)
{
    for (size_t i = 0; i < 32; ++i)
    {
        data[offset + i] = word.bytes[i];
    }
}

inline std::vector<uint8_t> packL1AttributesData(uint64_t l1BaseFee, uint32_t baseScalar,
    uint32_t blobScalar, uint64_t blobBaseFee, uint32_t opScalar, uint64_t opConst)
{
    std::vector<uint8_t> data(128, 0);
    writeWord(data, 0, packUint256Low8(l1BaseFee));
    writeWord(data, 32, packSlot3(baseScalar, blobScalar));
    writeWord(data, 64, packUint256Low8(blobBaseFee));
    writeWord(data, 96, packSlot8(opScalar, opConst));
    return data;
}

inline evmc::bytes toBytes(const std::vector<uint8_t>& v)
{
    return evmc::bytes{v.begin(), v.end()};
}
}  // namespace bcos::evm::opstack::testhelpers
