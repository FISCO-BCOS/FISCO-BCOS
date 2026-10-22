/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Buffers.h
 * @brief C-compatible input/output buffer structs and status codes shared by the
 * crypto backends. Layout-identical replacement for wedpr-crypto's
 * WedprUtilities.h so cross-boundary code stays ABI-compatible. Deliberately
 * shares wedpr's include guard: the definitions are identical, so whichever
 * header is included first wins and the other becomes a no-op.
 */
#ifndef _WEDPR_UTILITIES_H_
#define _WEDPR_UTILITIES_H_

#include <cstdint>

extern "C" {

struct CInputBuffer
{
    const char* data;
    uintptr_t len;
};

struct COutputBuffer
{
    char* data;
    uintptr_t len;
};

const int8_t WEDPR_ERROR = -1;
const int8_t WEDPR_SUCCESS = 0;
}  // extern "C"

#endif  // _WEDPR_UTILITIES_H_
