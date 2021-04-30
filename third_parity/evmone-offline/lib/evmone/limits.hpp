// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.
#pragma once

/// The maximum EVM bytecode size allowed by the Ethereum spec.
constexpr auto max_code_size = 0x6000;

/// The maximum base cost of any EVM instruction.
/// The value comes from the cost of the CREATE instruction.
constexpr auto max_instruction_base_cost = 32000;

/// The maximum stack increase any instruction can cause.
/// Only instructions taking 0 arguments and pushing an item to the stack
/// can increase the stack height and only by 1.
constexpr auto max_instruction_stack_increase = 1;
