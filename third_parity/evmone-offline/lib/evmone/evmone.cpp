// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2018-2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

/// @file
/// EVMC instance and entry point of evmone is defined here.
/// The file name matches the evmone.h public header.

#include "execution.hpp"
#include <evmone/evmone.h>

extern "C" {
EVMC_EXPORT evmc_vm* evmc_create_evmone() noexcept
{
    static constexpr auto destroy = [](evmc_vm*) noexcept {};
    static constexpr auto get_capabilities = [](evmc_vm*) noexcept
    {
        return evmc_capabilities_flagset{EVMC_CAPABILITY_EVM1};
    };

    static auto instance = evmc_vm{
        EVMC_ABI_VERSION,
        "evmone",
        PROJECT_VERSION,
        destroy,
        evmone::execute,
        get_capabilities,
        /* set_option(): */ nullptr,
    };
    return &instance;
}
}
