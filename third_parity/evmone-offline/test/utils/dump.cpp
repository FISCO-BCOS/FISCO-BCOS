// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2018-2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include "dump.hpp"
#include <evmc/instructions.h>
#include <evmone/analysis.hpp>
#include <test/utils/utils.hpp>
#include <iomanip>
#include <iostream>

void dump(const evmone::code_analysis& analysis)
{
    using namespace evmone;

    auto names = evmc_get_instruction_names_table(EVMC_BYZANTIUM);
    auto metrics = evmc_get_instruction_metrics_table(EVMC_BYZANTIUM);

    const block_info* block = nullptr;
    for (size_t i = 0; i < analysis.instrs.size(); ++i)
    {
        auto& instr = analysis.instrs[i];
        auto c = static_cast<uint8_t>((size_t)instr.fn);
        auto name = names[c];
        auto gas_cost = metrics[c].gas_cost;
        if (!name)
            name = "XX";

        if (c == OPX_BEGINBLOCK)
        {
            block = &instr.arg.block;

            const auto get_jumpdest_offset = [&analysis](size_t index) noexcept
            {
                for (size_t t = 0; t < analysis.jumpdest_targets.size(); ++t)
                {
                    if (static_cast<size_t>(analysis.jumpdest_targets[t]) == index)
                        return analysis.jumpdest_offsets[t];
                }
                return int32_t{-1};
            };

            std::cout << "┌ ";
            const auto offset = get_jumpdest_offset(i);
            if (offset >= 0)
                std::cout << std::setw(2) << offset;
            else
            {
                std::cout << "  ";
                name = "BEGINBLOCK";
                gas_cost = 0;
            }

            std::cout << " " << std::setw(11) << block->gas_cost << " " << block->stack_req << " "
                      << block->stack_max_growth << "\n";
        }

        std::cout << "│ " << std::setw(10) << std::left << name << std::setw(4) << std::right
                  << gas_cost;

        if (c >= OP_PUSH1 && c <= OP_PUSH8)
            std::cout << '\t' << instr.arg.small_push_value;

        std::cout << '\n';
    }
}
