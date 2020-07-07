/**
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
 * along with FISCO-BCOS.  If not, see <http:www.gnu.org/licenses/>
 * (c) 2019-2021 fisco-dev contributors.
 *
 * @file Common.h
 * @author: octopuswang
 * @date 2019-08-13
 */

#pragma once

#include <libdevcore/Log.h>
#include <libethcore/LogEntry.h>
#include <libethcore/TransactionReceipt.h>

#define EVENT_LOG(LEVEL) LOG(LEVEL) << "[EVENT]"

namespace dev
{
namespace event
{
using MatchedLogEntry = std::pair<eth::LogEntry, eth::TransactionReceipt>;
using MatchedLogEntries = std::vector<MatchedLogEntry>;

enum ResponseCode
{
    SUCCESS = 0,
    PUSH_COMPLETED = 1,
    INVALID_PARAMS = -41000,
    INVALID_REQUEST = -41001,
    GROUP_NOT_EXIST = -41002,
    INVALID_REQUEST_RANGE = -41003,
    INVALID_RESPONSE = -41004,
    REQUST_TIMEOUT = -41005,
    SDK_PERMISSION_DENIED = -41006,
};
enum filter_status
{
    GROUP_ID_NOT_EXIST,
    CALLBACK_FAILED,
    ERROR_STATUS,
    STATUS_PUSH_COMPLETED,
    WAIT_FOR_MORE_BLOCK,
    WAIT_FOR_NEXT_LOOP,
    REMOTE_PEERS_ACCESS_DENIED,
    CHECK_VALID = 5000,
};
}  // namespace event
}  // namespace dev
