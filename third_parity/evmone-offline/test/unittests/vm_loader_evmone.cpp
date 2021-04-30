// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include "vm_loader.hpp"
#include <evmone/evmone.h>

evmc::VM& get_vm() noexcept
{
    static auto vm = evmc::VM{evmc_create_evmone()};
    return vm;
}
