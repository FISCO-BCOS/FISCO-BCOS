// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <evmc/evmc.hpp>
#include <evmone/evmone.h>
#include <gtest/gtest.h>

TEST(evmone, info)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    EXPECT_STREQ(vm.name(), "evmone");
    EXPECT_STREQ(vm.version(), PROJECT_VERSION);
    EXPECT_TRUE(vm.is_abi_compatible());
}

TEST(evmone, capabilities)
{
    auto vm = evmc_create_evmone();
    EXPECT_EQ(vm->get_capabilities(vm), evmc_capabilities_flagset{EVMC_CAPABILITY_EVM1});
    vm->destroy(vm);
}
