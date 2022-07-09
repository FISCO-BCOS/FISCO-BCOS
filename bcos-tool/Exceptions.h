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
 * @brief exceptions for tool
 * @file Exceptions.h
 * @author: yujiechen
 * @date: 2021-03-16
 */
#pragma once
#include <bcos-utilities/Exceptions.h>
namespace bcos
{
namespace tool
{
DERIVE_BCOS_EXCEPTION(LedgerConfigFetcherException);
DERIVE_BCOS_EXCEPTION(InvalidConfig);
DERIVE_BCOS_EXCEPTION(InvalidVersion);
}  // namespace tool
}  // namespace bcos
