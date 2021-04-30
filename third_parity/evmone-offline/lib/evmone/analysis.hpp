// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.
#pragma once

#include "limits.hpp"
#include <evmc/evmc.hpp>
#include <evmc/instructions.h>
#include <evmc/utils.h>
#include <intx/intx.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace evmone
{
using uint256 = intx::uint256;

using bytes32 = std::array<uint8_t, 32>;

using bytes = std::basic_string<uint8_t>;

/// The stack for 256-bit EVM words.
///
/// This implementation reserves memory inplace for all possible stack items (1024),
/// so this type is big. Make sure it is allocated on heap.
struct evm_stack
{
    /// The maximum number of stack items.
    static constexpr auto limit = 1024;

    /// The pointer to the top item, or below the stack bottom if stack is empty.
    uint256* top_item;

    /// The storage allocated for maximum possible number of items.
    /// This is also the pointer to the bottom item.
    /// Items are aligned to 256 bits for better packing in cache lines.
    alignas(sizeof(uint256)) uint256 storage[limit];

    /// Default constructor. Sets the top_item pointer to below the stack bottom.
    [[clang::no_sanitize("bounds")]] evm_stack() noexcept : top_item{storage - 1} {}

    /// The current number of items on the stack.
    int size() noexcept { return static_cast<int>(top_item + 1 - storage); }

    /// Returns the reference to the top item.
    uint256& top() noexcept { return *top_item; }

    /// Returns the reference to the stack item on given position from the stack top.
    uint256& operator[](int index) noexcept { return *(top_item - index); }

    /// Pushes an item on the stack. The stack limit is not checked.
    void push(const uint256& item) noexcept { *++top_item = item; }

    /// Returns an item popped from the top of the stack.
    uint256 pop() noexcept { return *top_item--; }
};

/// The EVM memory.
///
/// At this point it is a wrapper for std::vector<uint8_t> with initial allocation of 4k.
/// Some benchmarks has been done to confirm 4k is ok-ish value.
/// Also std::basic_string<uint8_t> has been tried but not faster and we don't want SSO
/// if initial_capacity is used.
/// In future, transition to std::realloc() + std::free() planned.
class evm_memory
{
    /// The initial memory allocation.
    static constexpr size_t initial_capacity = 4 * 1024;

    std::vector<uint8_t> m_memory;

public:
    evm_memory() noexcept { m_memory.reserve(initial_capacity); }

    evm_memory(const evm_memory&) = delete;
    evm_memory& operator=(const evm_memory&) = delete;

    uint8_t& operator[](size_t index) noexcept { return m_memory[index]; }

    [[nodiscard]] size_t size() const noexcept { return m_memory.size(); }

    void resize(size_t new_size) { m_memory.resize(new_size); }
};

struct instruction;

struct block_info
{
    /// The total base gas cost of all instructions in the block.
    /// This cannot overflow, see the static_assert() below.
    uint32_t gas_cost = 0;

    static_assert(
        max_code_size * max_instruction_base_cost < std::numeric_limits<decltype(gas_cost)>::max(),
        "Potential block_info::gas_cost overflow");

    /// The stack height required to execute the block.
    /// This MAY overflow.
    int16_t stack_req = 0;

    /// The maximum stack height growth relative to the stack height at block start.
    /// This cannot overflow, see the static_assert() below.
    int16_t stack_max_growth = 0;

    static_assert(max_code_size * max_instruction_stack_increase <
                      std::numeric_limits<decltype(stack_max_growth)>::max(),
        "Potential block_info::stack_max_growth overflow");
};
// static_assert(sizeof(block_info) == 8);

struct execution_state
{
    evmc_status_code status = EVMC_SUCCESS;
    int64_t gas_left = 0;

    evm_stack stack;
    evm_memory memory;

    size_t output_offset = 0;
    size_t output_size = 0;

    /// The gas cost of the current block.
    ///
    /// This is only needed to correctly calculate the "current gas left" value.
    uint32_t current_block_cost = 0;

    struct code_analysis* analysis = nullptr;
    bytes return_data;
    const evmc_message* msg = nullptr;
    const uint8_t* code = nullptr;
    size_t code_size = 0;

    evmc::HostContext host;

    evmc_revision rev = {};

    /// Terminates the execution with the given status code.
    const instruction* exit(evmc_status_code status_code) noexcept
    {
        status = status_code;
        return nullptr;
    }
};

union instruction_argument
{
    int64_t number;
    const intx::uint256* push_value;
    uint64_t small_push_value;
    block_info block{};
};
static_assert(
    sizeof(instruction_argument) == sizeof(uint64_t), "Incorrect size of instruction_argument");

/// The pointer to function implementing an instruction execution.
using instruction_exec_fn = const instruction* (*)(const instruction*, execution_state&);

/// The evmone intrinsic opcodes.
///
/// These intrinsic instructions may be injected to the code in the analysis phase.
/// They contain additional and required logic to be executed by the interpreter.
enum intrinsic_opcodes
{
    /// The BEGINBLOCK instruction.
    ///
    /// This instruction is defined as alias for JUMPDEST and replaces all JUMPDEST instructions.
    /// It is also injected at beginning of basic blocks not being the valid jump destination.
    /// It checks basic block execution requirements and terminates execution if they are not met.
    OPX_BEGINBLOCK = OP_JUMPDEST
};

struct op_table_entry
{
    instruction_exec_fn fn;
    int16_t gas_cost;
    int8_t stack_req;
    int8_t stack_change;
};

using op_table = std::array<op_table_entry, 256>;

struct instruction
{
    instruction_exec_fn fn = nullptr;
    instruction_argument arg;

    explicit constexpr instruction(instruction_exec_fn f) noexcept : fn{f}, arg{} {};
};

struct code_analysis
{
    std::vector<instruction> instrs;

    /// Storage for large push values.
    std::vector<intx::uint256> push_values;

    /// The offsets of JUMPDESTs in the original code.
    /// These are values that JUMP/JUMPI receives as an argument.
    /// The elements are sorted.
    std::vector<int32_t> jumpdest_offsets;

    /// The indexes of the instructions in the generated instruction table
    /// matching the elements from jumdest_offsets.
    /// This is value to which the next instruction pointer must be set in JUMP/JUMPI.
    std::vector<int32_t> jumpdest_targets;
};

inline int find_jumpdest(const code_analysis& analysis, int offset) noexcept
{
    const auto begin = std::begin(analysis.jumpdest_offsets);
    const auto end = std::end(analysis.jumpdest_offsets);
    const auto it = std::lower_bound(begin, end, offset);
    return (it != end && *it == offset) ?
               analysis.jumpdest_targets[static_cast<size_t>(it - begin)] :
               -1;
}

EVMC_EXPORT code_analysis analyze(evmc_revision rev, const uint8_t* code, size_t code_size,
    evmc_host_context* host_context) noexcept;

EVMC_EXPORT const op_table& get_op_table(evmc_revision rev) noexcept;

}  // namespace evmone
