/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief : Common.h for Grouped PBFT Engine
 * @file: Common.h
 * @author: yujiechen
 * @date: 2019-05-28
 */
#pragma once
#include <libconsensus/Common.h>
#include <libconsensus/pbft/Common.h>

namespace dev
{
namespace consensus
{
#define GPBFTENGINE_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("GROUPPBFT")
#define GPBFTSEALER_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("GROUPSEALER")
typedef int64_t ZONETYPE;
}  // namespace consensus
}  // namespace dev