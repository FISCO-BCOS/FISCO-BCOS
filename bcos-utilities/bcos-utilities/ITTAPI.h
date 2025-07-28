/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief itt api
 * @file ITTAPI.h
 * @author: xingqiangbai
 * @date 2023-03-06
 */

#pragma once
#include "ittnotify.h"

namespace bcos::ittapi
{

struct Report
{
    Report(const __itt_domain* const domain, __itt_string_handle* name) : m_domain(domain)
    {
        __itt_task_begin(domain, __itt_null, __itt_null, name);
    }
    Report(const Report&) = delete;
    Report(Report&&) = default;
    Report& operator=(const Report&) = delete;
    Report& operator=(Report&&) = default;
    ~Report() noexcept { release(); }
    void release() noexcept
    {
        if (m_domain != nullptr)
        {
            __itt_task_end(m_domain);
            m_domain = nullptr;
        }
    }

    const __itt_domain* m_domain;
};

// baseline scheduler
struct ITT_DOMAINS
{
    static ITT_DOMAINS& instance()
    {
        static ITT_DOMAINS ittDomains;
        return ittDomains;
    }

    const __itt_domain* ITT_DOMAIN_STORAGE = __itt_domain_create("storage");
    const __itt_domain* ITT_DOMAIN_CONSENSUS = __itt_domain_create("consensus");
    const __itt_domain* ITT_DOMAIN_SCHEDULER = __itt_domain_create("scheduler");
    const __itt_domain* ITT_DOMAIN_EXECUTOR = __itt_domain_create("executor");

    const __itt_domain* SEALER = __itt_domain_create("sealer");
    __itt_string_handle* SUBMIT_PROPOSAL = __itt_string_handle_create("submitProposal");

    const __itt_domain* PBFT = __itt_domain_create("pbft");
    __itt_string_handle* PRE_PREPARE_MSG = __itt_string_handle_create("prePrepareMsg");
    __itt_string_handle* PREPARE_MSG = __itt_string_handle_create("prepareMsg");
    __itt_string_handle* COMMIT_MSG = __itt_string_handle_create("commitMsg");

    const __itt_domain* TXPOOL = __itt_domain_create("txpool");
    __itt_string_handle* BROADCAST_TX = __itt_string_handle_create("broadcastTx");
    __itt_string_handle* SUBMIT_TX = __itt_string_handle_create("submitTx");
    __itt_string_handle* BATCH_FETCH_TXS = __itt_string_handle_create("batchFetchTxs");
    __itt_string_handle* BATCH_REMOVE_TXS = __itt_string_handle_create("batchRemoveTxs");
    __itt_string_handle* FILL_BLOCK = __itt_string_handle_create("fillBlock");

    const __itt_domain* STORAGE2 = __itt_domain_create("storage2");
    __itt_string_handle* MERGE_BACKEND = __itt_string_handle_create("mergeBackend");
    __itt_string_handle* MERGE_CACHE = __itt_string_handle_create("mergeCache");

    const __itt_domain* BASELINE_SCHEDULER = __itt_domain_create("baselineScheduler");
    __itt_string_handle* GET_TRANSACTIONS = __itt_string_handle_create("getTransactions");
    __itt_string_handle* EXECUTE_BLOCK = __itt_string_handle_create("executeBlock");
    __itt_string_handle* FINISH_EXECUTE = __itt_string_handle_create("finishExecute");
    __itt_string_handle* COMMIT_BLOCK = __itt_string_handle_create("commitBlock");
    __itt_string_handle* NOTIFY_RESULTS = __itt_string_handle_create("notifyResults");

    const __itt_domain* BASE_SCHEDULER = __itt_domain_create("baseScheduler");
    __itt_string_handle* SET_BLOCK = __itt_string_handle_create("setBlock");
    __itt_string_handle* MERGE_STATE = __itt_string_handle_create("mergeState");
    __itt_string_handle* STORE_TRANSACTION_RECEIPTS =
        __itt_string_handle_create("storeTransactionsAndReceipts");

    const __itt_domain* SERIAL_SCHEDULER = __itt_domain_create("serialScheduler");
    __itt_string_handle* SERIAL_EXECUTE = __itt_string_handle_create("serialExecute");
    __itt_string_handle* STAGE_1 = __itt_string_handle_create("stage1");
    __itt_string_handle* STAGE_2 = __itt_string_handle_create("stage2");
    __itt_string_handle* STAGE_3 = __itt_string_handle_create("stage3");
    __itt_string_handle* STAGE_4 = __itt_string_handle_create("stage4");
    __itt_string_handle* STAGE_5 = __itt_string_handle_create("stage5");
    __itt_string_handle* STAGE_6 = __itt_string_handle_create("stage6");
    __itt_string_handle* STAGE_7 = __itt_string_handle_create("stage7");

    const __itt_domain* PARALLEL_SCHEDULER = __itt_domain_create("parallelScheduler");
    __itt_string_handle* PARALLEL_EXECUTE = __itt_string_handle_create("parallelExecute");
    __itt_string_handle* SINGLE_PASS = __itt_string_handle_create("singlePass");
    __itt_string_handle* DETECT_RAW = __itt_string_handle_create("detectRAW");
    __itt_string_handle* EXECUTE_CHUNK1 = __itt_string_handle_create("executeChunk1");
    __itt_string_handle* EXECUTE_CHUNK2 = __itt_string_handle_create("executeChunk2");
    __itt_string_handle* EXECUTE_CHUNK3 = __itt_string_handle_create("executeChunk3");
    __itt_string_handle* RELEASE_CONFLICT = __itt_string_handle_create("releaseConflict");
    __itt_string_handle* MERGE_RWSET = __itt_string_handle_create("mergeRWSet");
    __itt_string_handle* MERGE_CHUNK = __itt_string_handle_create("mergeChunk");
    __itt_string_handle* MERGE_LAST_CHUNK = __itt_string_handle_create("mergeLastChunk");

    const __itt_domain* TRANSACTION = __itt_domain_create("transaction");
    __itt_string_handle* VERIFY_TRANSACTION = __itt_string_handle_create("verifyTransaction");
};
}  // namespace bcos::ittapi
