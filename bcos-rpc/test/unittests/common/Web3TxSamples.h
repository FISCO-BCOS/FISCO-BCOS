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
 * @file Web3TxSamples.h
 * @author: kyonGuo
 * @date 2026/9/9
 */

#pragma once

#include <string_view>

namespace bcos::test
{
// A pre-EIP-155 transaction: it claims no chain, so the chainId rule stands down whatever the
// node is configured with, and what is left is the pool's own answer.
// Sender 0x7ee79be7871ff709d67baadbf1a45bbb65bd3f8b, value 8921810000000000000.
inline constexpr std::string_view c_unprotectedRawTx =
    "0xf86c808504a817c800825208945dc98fe6cd853f7f5a44399cfb1c60682d5d62ef887bd0a2ecdb872000801ca0"
    "e90ef078b60e3a186fae6071c92dbfec1256f423f5a40cd5cba69ca423eb4e44a028e14398b1a1059388cbc5eb03"
    "cfcb6f9493bdd1efd6a17e54dcabbb2eaace16";
inline constexpr std::string_view c_unprotectedSender = "7ee79be7871ff709d67baadbf1a45bbb65bd3f8b";
inline constexpr std::string_view c_unprotectedTxHash =
    "0xf6ecaffaf808cdfe1d9ef02ec461f2ab5674f72f9c6f954743e0c2d74608b751";
}  // namespace bcos::test
