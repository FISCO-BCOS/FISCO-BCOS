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
 * @brief JSON vs table rendering. Non-TTY stdout defaults to JSON; --json forces it.
 * @file Output.h
 */
#pragma once

#include <json/json.h>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ops
{
/// true when stdout is a terminal (the caller passes the stream it will write to)
bool stdoutIsTty();
/// --json wins; otherwise JSON when stdout is not a TTY
bool wantJson(bool _jsonFlag);
void printJson(std::ostream& _out, Json::Value const& _value);
/// two-column table: label padded to the longest label
void printRows(std::ostream& _out, std::vector<std::pair<std::string, std::string>> const& _rows);
std::string abridged(std::string const& _hex, size_t _keep = 4);
}  // namespace bcos::ops
