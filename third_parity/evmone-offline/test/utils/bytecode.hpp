// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.
#pragma once

#include <evmc/instructions.h>
#include <test/utils/utils.hpp>
#include <algorithm>

struct bytecode;

inline bytecode push(uint64_t n);

struct bytecode : bytes
{
    bytecode() noexcept = default;

    bytecode(bytes b) : bytes(std::move(b)) {}

    bytecode(evmc_opcode opcode) : bytes{uint8_t(opcode)} {}

    template <typename T,
        typename = typename std::enable_if_t<std::is_convertible_v<T, std::string_view>>>
    bytecode(T hex) : bytes{from_hex(hex)}
    {}

    bytecode(uint64_t n) : bytes{push(n)} {}
};

inline bytecode operator+(bytecode a, bytecode b)
{
    return static_cast<bytes&>(a) + static_cast<bytes&>(b);
}

inline bytecode& operator+=(bytecode& a, bytecode b)
{
    return a = a + b;
}

inline bool operator==(const bytecode& a, const bytecode& b) noexcept
{
    return static_cast<const bytes&>(a) == static_cast<const bytes&>(b);
}

inline std::ostream& operator<<(std::ostream& os, const bytecode& c)
{
    return os << hex(c);
}

inline bytecode operator*(int n, bytecode c)
{
    auto out = bytecode{};
    while (n-- > 0)
        out += c;
    return out;
}

inline bytecode operator*(int n, evmc_opcode op)
{
    return n * bytecode{op};
}


inline bytecode push(bytes_view data)
{
    if (data.empty())
        throw std::invalid_argument{"push data empty"};
    if (data.size() > (OP_PUSH32 - OP_PUSH1 + 1))
        throw std::invalid_argument{"push data too long"};
    return evmc_opcode(data.size() + OP_PUSH1 - 1) + bytes{data};
}

inline bytecode push(std::string_view hex_data)
{
    return push(from_hex(hex_data));
}


inline bytecode push(uint64_t n)
{
    auto data = bytes{};
    for (; n != 0; n >>= 8)
        data.push_back(uint8_t(n));
    std::reverse(data.begin(), data.end());
    if (data.empty())
        data.push_back(0);
    return push(data);
}

inline bytecode dup1(bytecode c)
{
    return c + OP_DUP1;
}

inline bytecode add(bytecode a, bytecode b)
{
    return b + a + OP_ADD;
}

inline bytecode add(bytecode a)
{
    return a + OP_ADD;
}

inline bytecode mul(bytecode a, bytecode b)
{
    return b + a + OP_MUL;
}

inline bytecode not_(bytecode a)
{
    return a + OP_NOT;
}

inline bytecode iszero(bytecode a)
{
    return a + OP_ISZERO;
}

inline bytecode eq(bytecode a, bytecode b)
{
    return b + a + OP_EQ;
}

inline bytecode byte(bytecode a, bytecode n)
{
    return a + n + OP_BYTE;
}

inline bytecode mstore(bytecode index)
{
    return index + OP_MSTORE;
}

inline bytecode mstore(bytecode index, bytecode value)
{
    return value + index + OP_MSTORE;
}

inline bytecode mstore8(bytecode index)
{
    return index + OP_MSTORE8;
}

inline bytecode mstore8(bytecode index, bytecode value)
{
    return value + index + OP_MSTORE8;
}

inline bytecode jump(bytecode target)
{
    return target + OP_JUMP;
}

inline bytecode jumpi(bytecode target, bytecode condition)
{
    return condition + target + OP_JUMPI;
}

inline bytecode ret(bytecode index, bytecode size)
{
    return size + index + OP_RETURN;
}

inline bytecode ret_top()
{
    return mstore(0) + ret(0, 0x20);
}

inline bytecode ret(bytecode c)
{
    return c + ret_top();
}

inline bytecode sha3(bytecode index, bytecode size)
{
    return size + index + OP_SHA3;
}

inline bytecode calldataload(bytecode index)
{
    return index + OP_CALLDATALOAD;
}

inline bytecode sstore(bytecode index, bytecode value)
{
    return value + index + OP_SSTORE;
}

inline bytecode sload(bytecode index)
{
    return index + OP_SLOAD;
}

template <evmc_opcode kind>
struct call_instruction
{
private:
    bytecode m_address = 0;
    bytecode m_gas = 0;
    bytecode m_value = 0;
    bytecode m_input = 0;
    bytecode m_input_size = 0;
    bytecode m_output = 0;
    bytecode m_output_size = 0;

public:
    explicit call_instruction(bytecode address) : m_address{std::move(address)} {}

    auto& gas(bytecode g)
    {
        m_gas = std::move(g);
        return *this;
    }


    template <evmc_opcode k = kind>
    typename std::enable_if<k == OP_CALL || k == OP_CALLCODE, call_instruction&>::type value(
        bytecode v)
    {
        m_value = std::move(v);
        return *this;
    }

    auto& input(bytecode index, bytecode size)
    {
        m_input = std::move(index);
        m_input_size = std::move(size);
        return *this;
    }

    auto& output(bytecode index, bytecode size)
    {
        m_output = std::move(index);
        m_output_size = std::move(size);
        return *this;
    }

    operator bytecode() const
    {
        auto code = m_output_size + m_output + m_input_size + m_input;
        if constexpr (kind == OP_CALL || kind == OP_CALLCODE)
            code += m_value;
        code += m_address + m_gas + kind;
        return code;
    }
};

inline call_instruction<OP_DELEGATECALL> delegatecall(bytecode address)
{
    return call_instruction<OP_DELEGATECALL>{std::move(address)};
}

inline call_instruction<OP_STATICCALL> staticcall(bytecode address)
{
    return call_instruction<OP_STATICCALL>{std::move(address)};
}

inline call_instruction<OP_CALL> call(bytecode address)
{
    return call_instruction<OP_CALL>{std::move(address)};
}

inline call_instruction<OP_CALLCODE> callcode(bytecode address)
{
    return call_instruction<OP_CALLCODE>{std::move(address)};
}


inline std::string hex(evmc_opcode opcode) noexcept
{
    return hex(static_cast<uint8_t>(opcode));
}

inline std::string to_name(evmc_opcode opcode, evmc_revision rev = EVMC_MAX_REVISION) noexcept
{
    const auto names = evmc_get_instruction_names_table(rev);
    if (const auto name = names[opcode]; name)
        return name;

    return "UNDEFINED_INSTRUCTION:" + hex(opcode);
}

inline std::string decode(bytes_view bytecode, evmc_revision rev)
{
    auto s = std::string{"bytecode{}"};
    const auto names = evmc_get_instruction_names_table(rev);
    for (auto it = bytecode.begin(); it != bytecode.end(); ++it)
    {
        const auto opcode = *it;
        if (const auto name = names[opcode]; name)
        {
            s += std::string{" + OP_"} + name;

            if (opcode >= OP_PUSH1 && opcode <= OP_PUSH32)
            {
                const auto push_data_start = it + 1;
                const auto push_data_size =
                    std::min(static_cast<std::size_t>(opcode - OP_PUSH1 + 1),
                        static_cast<std::size_t>(bytecode.end() - push_data_start));
                if (push_data_size != 0)
                {
                    s += " + \"" + hex({&*push_data_start, push_data_size}) + '"';
                    it += push_data_size;
                }
            }
        }
        else
            s += " + \"" + hex(opcode) + '"';
    }

    return s;
}