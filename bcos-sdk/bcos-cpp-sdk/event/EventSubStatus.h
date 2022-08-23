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
 * @file EventSubError.h
 * @author: octopus
 * @date 2021-09-01
 */

#pragma once
namespace bcos
{
namespace cppsdk
{
namespace event
{
enum StatusCode
{
    Success = 0,
    EndOfPush = 1,  // push completed
};
}  // namespace event
}  // namespace cppsdk
}  // namespace bcos