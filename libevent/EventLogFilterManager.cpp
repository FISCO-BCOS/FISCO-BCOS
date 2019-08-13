
#include "EventLogFilterManager.h"
#include "Common.h"
#include <libethcore/CommonJS.h>
#include <libevent/EventLogFilterParams.h>
#include <libledger/LedgerManager.h>

using namespace std;
using namespace dev;
using namespace dev::event;
using namespace dev::eth;

// main loop thread
void EventLogFilterManager::doWork() {}

// start EventLogFilterManager
void EventLogFilterManager::start() {}

// stop EventLogFilterManager
void EventLogFilterManager::stop(){};

// add EventLogFilter to m_filters by client json request
void EventLogFilterManager::addEventLogFilter(const std::string& _json)
{
    EVENT_LOG(TRACE) << LOG_KV("json", _json);
}

void EventLogFilterManager::doAdd() {}

void EventLogFilterManager::doMatch() {}