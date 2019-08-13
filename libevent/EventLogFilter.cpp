#include "EventLogFilter.h"
#include <libethcore/CommonJS.h>
using namespace dev;
using namespace dev::eth;
using namespace dev::event;

// filter individual log to see if the requirements are met
bool EventLogFilter::matches(LogEntry const& _log)
{
    EVENT_LOG(TRACE) << LOG_KV("address", _log.address);
    return false;
}