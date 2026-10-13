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
 * @file JsonAt.h
 * @brief jsoncpp .at() equivalent shared by the t8n suites (the ONE throwing copy).
 */

#pragma once

#include <json/json.h>
#include <stdexcept>
#include <string>

namespace opstack_test
{
/// nlohmann's .at(key) throws on a missing member; jsoncpp's operator[] silently
/// fabricates a null. Every required-field access in the t8n suites goes through jAt so
/// a missing field is a named failure, never a silent null read.
///
/// key is `const char*` (not `const std::string&`) deliberately: a string literal
/// argument would materialize a temporary std::string, and GCC-14's
/// -Wdangling-reference flags any reference-returning call that receives a class-type
/// temporary — even though the returned reference aliases `v`, never `key` (false
/// positive). A `const char*` argument creates no temporary, so the warning cannot
/// fire. Callers with a std::string key pass .c_str().
///
/// This is the hard-failing variant (throws): use it for single-value reads. The
/// provenance sweep keeps its own SOFT variant on purpose — it walks ~100 corpus files
/// and must record every missing field in one pass instead of aborting on the first.
inline const Json::Value& jAt(const Json::Value& v, const char* key)
{
    if (!v.isMember(key))
    {
        throw std::invalid_argument(std::string("missing required field: ") + key);
    }
    return v[key];
}
}  // namespace opstack_test
