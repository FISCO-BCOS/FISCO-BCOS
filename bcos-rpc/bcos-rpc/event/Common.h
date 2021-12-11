/**
 * @file Common.h
 * @author: octopuswang
 * @date 2019-08-13
 */

#pragma once

#include <bcos-framework/libutilities/Log.h>

// The largest number of topic in one event log
#define EVENT_LOG_TOPICS_MAX_INDEX (4)

#define EVENT_REQUEST(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][REQUEST]"
#define EVENT_RESPONSE(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][RESPONSE]"
#define EVENT_TASK(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][TASK]"
#define EVENT_SUB(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][SUB]"
#define EVENT_MATCH(LEVEL) BCOS_LOG(LEVEL) << "[EVENT][MATCH]"

namespace bcos
{
namespace event
{
enum MessageType
{
    // ------------event begin ---------

    EVENT_SUBSCRIBE = 0x120,    // 288
    EVENT_UNSUBSCRIBE = 0x121,  // 289
    EVENT_LOG_PUSH = 0x122      // 290

    // ------------event end ---------
};
enum EP_STATUS_CODE
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
    NONEXISTENT_EVENT = -41007,
};

}  // namespace event
}  // namespace bcos
