#pragma once
#include <bcos-utilities/LogStream.h>

#define LIGHTNODE_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LIGHTNODE")
#define TRANSACTIONPOOL_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTIONPOOL")
// token-identical to bcos-framework/ledger/Ledger.h, which this header meets in the same TUs:
// a different body there is a macro redefinition error
#define LEDGER_LOG(LEVEL) BCOS_MODULE_LOG(LEDGER, LEVEL) << LOG_BADGE("LEDGER")
