// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2018-2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#ifndef EVMONE_H
#define EVMONE_H

#include <evmc/evmc.h>
#include <evmc/utils.h>

#if __cplusplus
extern "C" {
#endif

EVMC_EXPORT struct evmc_vm* evmc_create_evmone(void) EVMC_NOEXCEPT;

#if __cplusplus
}
#endif

#endif  // EVMONE_H
