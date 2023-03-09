/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief itt api
 * @file ITTAPI.h
 * @author: xingqiangbai
 * @date 2023-03-06
 */

#include "ITTAPI.h"

namespace bcos
{
const __itt_domain* const ITT_DOMAIN_STORAGE = __itt_domain_create("storage");
const __itt_domain* const ITT_DOMAIN_CONSENSUS = __itt_domain_create("consensus");
const __itt_domain* const ITT_DOMAIN_SCHEDULER = __itt_domain_create("scheduler");
const __itt_domain* const ITT_DOMAIN_EXECUTOR = __itt_domain_create("executor");
}  // namespace bcos