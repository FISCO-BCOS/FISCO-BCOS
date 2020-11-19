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
#include <libutilities/Exceptions.h>

namespace bcos
{
namespace ledger
{
DERIVE_BCOS_EXCEPTION(GenesisConfAlreadyExists);
DERIVE_BCOS_EXCEPTION(GroupConfAlreadyExists);

DERIVE_BCOS_EXCEPTION(GroupNotFound);
DERIVE_BCOS_EXCEPTION(GenesisConfNotFound);
DERIVE_BCOS_EXCEPTION(GroupConfNotFound);

DERIVE_BCOS_EXCEPTION(GroupIsRunning);
DERIVE_BCOS_EXCEPTION(GroupIsStopping);
DERIVE_BCOS_EXCEPTION(GroupAlreadyDeleted);
DERIVE_BCOS_EXCEPTION(GroupAlreadyStopped);

DERIVE_BCOS_EXCEPTION(UnknownGroupStatus);
}  // namespace ledger
}  // namespace bcos
