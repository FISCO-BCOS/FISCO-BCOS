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
 * @brief: command dispatcher implementation
 * @file: CommandDispatcher.cpp
 */

#include "CommandDispatcher.h"
#include <boost/algorithm/string.hpp>
#include <algorithm>

using namespace bcos::console;

void CommandDispatcher::addCommand(CommandInfo info)
{
    auto index = m_commands.size();
    m_commands.emplace_back(std::move(info));

    auto& cmd = m_commands.back();
    // Register primary name (lowercase for case-insensitive matching)
    auto lowerName = boost::to_lower_copy(cmd.name);
    m_nameIndex[lowerName] = index;

    // Register aliases
    for (auto const& alias : cmd.aliases)
    {
        auto lowerAlias = boost::to_lower_copy(alias);
        // Only insert if not already mapped (first registration wins)
        if (!m_nameIndex.contains(lowerAlias))
        {
            m_nameIndex[lowerAlias] = index;
        }
    }
}

void CommandDispatcher::addCategory(CommandCategory category)
{
    m_categories.emplace_back(std::move(category));
}

CommandInfo const* CommandDispatcher::findCommand(std::string_view name) const
{
    auto lower = boost::to_lower_copy(std::string(name));
    auto it = m_nameIndex.find(lower);
    if (it != m_nameIndex.end())
    {
        return &m_commands[it->second];
    }
    return nullptr;
}

std::vector<std::string> CommandDispatcher::completions(std::string_view prefix) const
{
    std::vector<std::string> result;
    auto lowerPrefix = boost::to_lower_copy(std::string(prefix));

    for (auto const& [name, idx] : m_nameIndex)
    {
        if (name.starts_with(lowerPrefix))
            result.emplace_back(m_commands[idx].name);
    }
    // Deduplicate
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool CommandDispatcher::hasCommand(std::string_view name) const
{
    return m_nameIndex.contains(boost::to_lower_copy(std::string(name)));
}
