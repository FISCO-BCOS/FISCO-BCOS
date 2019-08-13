#pragma once

#include <json/json.h>
#include <libdevcore/Log.h>
#include <libdevcore/easylog.h>
#include <libethcore/LogEntry.h>
#include <libethcore/TransactionReceipt.h>

#define EVENT_LOG(LEVEL) LOG(LEVEL) << "[EVENT]"