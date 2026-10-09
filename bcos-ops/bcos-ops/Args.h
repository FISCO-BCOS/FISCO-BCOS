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
 *        swallowed as a value.
 * @file Args.h
 */
#pragma once

#include "OpsError.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ops
{
class Args
{
public:
    /// _argv excludes the program name and the subcommand name(s) already consumed
    Args(std::vector<std::string> _argv, std::set<std::string> _booleanFlags)
    {
        for (size_t i = 0; i < _argv.size(); ++i)
        {
            auto const& token = _argv[i];
            if (token.size() < 2 || token[0] != '-' ||
                std::isdigit(static_cast<unsigned char>(token[1])))  // -5 is a value
            {
                m_positionals.push_back(token);
                continue;
            }
            auto name = token.substr(token[1] == '-' ? 2 : 1);
            auto eq = name.find('=');
            if (eq != std::string::npos)
            {
                m_options[name.substr(0, eq)] = name.substr(eq + 1);
                continue;
            }
            if (_booleanFlags.contains(name) || i + 1 >= _argv.size() ||
                (_argv[i + 1].size() > 1 && _argv[i + 1][0] == '-' &&
                    !std::isdigit(static_cast<unsigned char>(_argv[i + 1][1]))))
            {
                m_flags.insert(name);
                continue;
            }
            m_options[name] = _argv[++i];
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
    std::map<std::string, std::string> m_options;
    std::set<std::string> m_flags;
    std::vector<std::string> m_positionals;
};
}  // namespace bcos::ops
