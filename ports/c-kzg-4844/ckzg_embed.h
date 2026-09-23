#pragma once

#include "setup/setup.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Loads the mainnet trusted setup that is embedded into libckzg at build time.
 * Equivalent to load_trusted_setup_file() on the upstream trusted_setup.txt.
 */
C_KZG_RET load_trusted_setup_embedded(KZGSettings *out, uint64_t precompute);

#ifdef __cplusplus
}
#endif
