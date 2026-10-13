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
 * @brief minimal argv parser shared by every subcommand: `--k v`, `--k=v`, boolean flags,
 *        positionals. Every command declares which flags are boolean so `--json 0x..` is not
 *        swallowed as a value, and which options take a value so a typo or a missing value is a
 *        usage error instead of a silent fallback.
 * @file Args.h
 */
#pragma once

#include "OpsError.h"
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace bcos::ops
{
class Args
{
public:
    /// _argv excludes the program name and the subcommand name(s) already consumed.
    /// _booleanFlags take no value (`--json`, `--json=true|false|1|0`). When _valueOptions is given
    /// it lists every other accepted name and an unknown name throws; without it (tests, nested
    /// dispatch) any other name is a value option. A value option with no value throws.
    Args(std::vector<std::string> _argv, std::set<std::string> const& _booleanFlags,
        std::optional<std::set<std::string>> const& _valueOptions = std::nullopt)
    {
        for (size_t i = 0; i < _argv.size(); ++i)
        {
            auto const& token = _argv[i];
            if (!isOption(token))
            {
                m_positionals.push_back(token);
                continue;
            }
            auto name = token.substr(token[1] == '-' ? 2 : 1);
            std::optional<std::string> inlineValue;
            if (auto eq = name.find('='); eq != std::string::npos)
            {
                inlineValue = name.substr(eq + 1);
                name.resize(eq);
            }
            if (_booleanFlags.contains(name))
            {
                if (!inlineValue || *inlineValue == "true" || *inlineValue == "1")
                {
                    m_flags.insert(name);
                }
                else if (*inlineValue != "false" && *inlineValue != "0")
                {
                    throw OpsError(c_exitUsage, "--" + name + " takes no value");
                }
                continue;
            }
            if (_valueOptions && !_valueOptions->contains(name))
            {
                throw OpsError(c_exitUsage, "unknown option --" + name);
            }
            if (!inlineValue && (i + 1 >= _argv.size() || isOption(_argv[i + 1])))
            {
                throw OpsError(c_exitUsage, "--" + name + " needs a value");
            }
            m_options[name] = inlineValue ? *inlineValue : _argv[++i];
        }
    }

    bool flag(std::string_view _name) const { return m_flags.contains(std::string(_name)); }
    std::optional<std::string> option(std::string_view _name) const
    {
        auto it = m_options.find(std::string(_name));
        if (it == m_options.end())
        {
            return std::nullopt;
        }
        return it->second;
    }
    std::string optionOr(std::string_view _name, std::string _default) const
    {
        return option(_name).value_or(std::move(_default));
    }
    template <typename T>
    T numberOr(std::string_view _name, T _default) const
    {
        auto value = option(_name);
        if (!value)
        {
            return _default;
        }
        try
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                return static_cast<T>(std::stod(*value));
            }
            else
            {
                return static_cast<T>(std::stoll(*value));
            }
        }
        catch (std::exception const&)
        {
            throw OpsError(
                c_exitUsage, "invalid number for --" + std::string(_name) + ": " + *value);
        }
    }
    std::vector<std::string> const& positionals() const { return m_positionals; }
    bool help() const { return flag("help") || flag("h"); }
    /// same options and flags, different positionals (sub-subcommand dispatch)
    Args withPositionals(std::vector<std::string> _positionals) const
    {
        Args copy = *this;
        copy.m_positionals = std::move(_positionals);
        return copy;
    }

private:
    // -5 is a value, not an option
    static bool isOption(std::string const& _token)
    {
        return _token.size() >= 2 && _token[0] == '-' &&
               !std::isdigit(static_cast<unsigned char>(_token[1]));
    }

    std::map<std::string, std::string> m_options;
    std::set<std::string> m_flags;
    std::vector<std::string> m_positionals;
};
}  // namespace bcos::ops
