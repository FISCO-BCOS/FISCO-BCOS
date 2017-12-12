#pragma once

#include <evm.h>

#ifdef _MSC_VER
#ifdef evmjit_EXPORTS
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __declspec(dllimport)
#endif

#else
#define EXPORT __attribute__ ((visibility ("default")))
#endif

#if __cplusplus
extern "C" {
#endif

/// Get EVMJIT's EVM-C interface.
///
/// @return  EVMJIT's function table.
EXPORT struct evm_interface evmjit_get_interface(void);

#if __cplusplus
}
#endif
