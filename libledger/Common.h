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
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : common componens of dynamic group management
 * @file: GroupCommon.h
 * @author: catli
 * @date: 2020-03-16
 */
#pragma once
#include <libdevcore/Exceptions.h>

namespace dev
{
namespace ledger
{
DEV_SIMPLE_EXCEPTION(GenesisConfAlreadyExists);
DEV_SIMPLE_EXCEPTION(GroupConfAlreadyExists);

DEV_SIMPLE_EXCEPTION(GroupNotFound);
DEV_SIMPLE_EXCEPTION(GenesisConfNotFound);
DEV_SIMPLE_EXCEPTION(GroupConfNotFound);

DEV_SIMPLE_EXCEPTION(GroupIsRunning);
DEV_SIMPLE_EXCEPTION(GroupIsStopping);
DEV_SIMPLE_EXCEPTION(GroupAlreadyDeleted);
DEV_SIMPLE_EXCEPTION(GroupAlreadyStopped);

DEV_SIMPLE_EXCEPTION(UnknownGroupStatus);
}  // namespace ledger
}  // namespace dev