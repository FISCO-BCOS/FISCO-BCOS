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
 * @brief jwt auth config
 * @file JwtConfig.h
 * @date 2026.05.19
 */

#pragma once

#include <memory>
#include <string>

namespace bcos::rpc
{
class JwtConfig
{
public:
    using Ptr = std::shared_ptr<JwtConfig>;
    using ConstPtr = std::shared_ptr<const JwtConfig>;

    JwtConfig() = default;
    ~JwtConfig() = default;

    const std::string& secretFile() const { return m_secretFile; }
    void setSecretFile(std::string _secretFile) { m_secretFile = std::move(_secretFile); }

    int64_t clockSkewSecs() const { return m_clockSkewSecs; }
    void setClockSkewSecs(int64_t _clockSkewSecs) { m_clockSkewSecs = _clockSkewSecs; }

    const std::string& allowedAlgorithms() const { return m_allowedAlgorithms; }
    void setAllowedAlgorithms(std::string _allowedAlgorithms)
    {
        m_allowedAlgorithms = std::move(_allowedAlgorithms);
    }

private:
    std::string m_secretFile;
    int64_t m_clockSkewSecs{60};
    std::string m_allowedAlgorithms{"HS256"};
};
}  // namespace bcos::rpc
