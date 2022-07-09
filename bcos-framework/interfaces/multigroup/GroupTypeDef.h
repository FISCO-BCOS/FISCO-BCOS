/**
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
 * @brief define the basic type of the GroupManager
 * @file GroupTypeDef.h
 * @author: yujiechen
 * @date 2021-09-16
 */
#pragma once
#include <bcos-framework/interfaces/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <memory>

#define GROUP_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("GROUP")

namespace bcos
{
namespace group
{
DERIVE_BCOS_EXCEPTION(InvalidGroupInfo);
DERIVE_BCOS_EXCEPTION(InvalidChainNodeInfo);
}  // namespace group
}  // namespace bcos
