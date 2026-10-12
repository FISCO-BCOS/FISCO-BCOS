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
 * @brief the node's [ethereum].mode as the ops commands see it: none (FISCO consortium chain,
 *        PBFT) or one of the EL modes (el, opstack-el), in which PBFT does not run and the
 *        web3 listener is the transaction entry (spec §5.2.4)
 * @file Mode.h
 */
#pragma once

#include "bcos-ops/OpsError.h"
#include <string>
#include <string_view>

namespace bcos::ops
{
inline bool isElMode(std::string_view _mode)
{
    return _mode == "el" || _mode == "opstack-el";
}

/// throws OpsError{1} on anything but none / el / opstack-el (NodeConfig rejects the rest too)
inline std::string checkedMode(std::string _mode)
{
    if (_mode != "none" && !isElMode(_mode))
    {
        throw OpsError(c_exitUsage, "--mode expects none, el or opstack-el, got: " + _mode);
    }
    return _mode;
}
}  // namespace bcos::ops
