/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief SYNC_LOG (transaction sync, badge [SYNC]) follows the SYNC channel, not TXPOOL
 * @file TxSyncLogChannelTest.cpp
 */
#include "bcos-txpool/sync/utilities/Common.h"
#include <bcos-framework/testutils/LogCapture.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(TxSyncLogChannelTest)

BOOST_AUTO_TEST_CASE(syncBadgedLinesFollowTheSyncChannel)
{
    setModuleLogLevel(LogModule::TXPOOL, LogLevel::TRACE);
    setModuleLogLevel(LogModule::SYNC, LogLevel::ERROR);
    {
        LogCapture capture;
        SYNC_LOG(INFO) << LOG_DESC("TxSyncChannelProbeMuted");
        BOOST_CHECK(capture.text().find("TxSyncChannelProbeMuted") == std::string::npos);
    }
    setModuleLogLevel(LogModule::TXPOOL, LogLevel::ERROR);
    setModuleLogLevel(LogModule::SYNC, LogLevel::INFO);
    {
        LogCapture capture;
        SYNC_LOG(INFO) << LOG_DESC("TxSyncChannelProbeShown");
        BOOST_CHECK(capture.text().find("[SYNC]TxSyncChannelProbeShown") != std::string::npos);
    }
    resetModuleLogLevel(LogModule::TXPOOL);
    resetModuleLogLevel(LogModule::SYNC);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
