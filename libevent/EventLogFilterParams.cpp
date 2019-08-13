
#include "EventLogFilterParams.h"
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>

using namespace dev;
using namespace dev::eth;
using namespace dev::event;

std::pair<bool, EventLogFilterParams::Ptr> EventLogFilterParams::buildEventLogFilterParamsObject(
    const std::string& _json)
{
    EVENT_LOG(TRACE) << LOG_BADGE("buildEventLogFilterParamsObject") << LOG_KV("json", _json);
    return std::make_pair<bool, EventLogFilterParams::Ptr>(false, nullptr);
}
