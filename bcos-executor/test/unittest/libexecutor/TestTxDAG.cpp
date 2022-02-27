/**
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
 */
/**
 * @brief : unitest for TxDAG
 * @author: jimmyshi
 * @date: 2022-01-19
 */

#if 0
#include "../../../src/dag/TxDAG.h"
#include "../../../src/dag/TxDAG2.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/test/unit_test.hpp>
#include <utility>
#include <vector>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor::critical;

namespace bcos
{
namespace test
{
BOOST_AUTO_TEST_SUITE(TestTxDAG)

CriticalFieldsInterface::Ptr makeCriticalsString(int _totalTx)
{
    vector<bytes> originMap = {bytes{111}, bytes{222}, bytes{33, 3}, bytes{44, 4}, bytes{55, 5},
        bytes{66, 6}, bytes{77, 7}, bytes{88, 8}, bytes{99, 9}, bytes{101}, bytes{102}, bytes{103},
        bytes{104}, bytes{105}, bytes{106}, bytes{107}, bytes{108}, bytes{109}, bytes{120},
        bytes{121}, bytes{122}, bytes{123}, bytes{124}, bytes{125}};
    CriticalFields::Ptr criticals = make_shared<CriticalFields>(_totalTx);
    for (int i = 0; i < _totalTx; i++)
    {
        int rand1 = random() % originMap.size();
        int rand2 = random() % originMap.size();
        vector<bytes> critical = {originMap[rand1], originMap[rand2]};
        criticals->put(i, make_shared<CriticalFields::CriticalField>(std::move(critical)));
        /*
        stringstream ss;
        ss << i;
        res.push_back({string(ss.str())});
         */
    }
    return criticals;
}

CriticalFieldsInterface::Ptr makeFixedCriticals(int _totalTx, int _criticalNum)
{
    CriticalFields::Ptr criticals = make_shared<CriticalFields>(_totalTx);
    for (int i = 0; i < _totalTx; i++)
    {
        uint32_t c = i % _criticalNum;
        vector<bytes> critical = {bytes{static_cast<uint8_t>(c & 0xff),
            static_cast<uint8_t>(c & 0xff00 >> 8), static_cast<uint8_t>(c & 0xff0000 >> 16),
            static_cast<uint8_t>(c & 0xff000000 >> 24)}};
        criticals->put(i, make_shared<CriticalFields::CriticalField>(std::move(critical)));
    }
    /*
    stringstream ss;
    ss << i;
    res.push_back({string(ss.str())});
     */
    return criticals;
}

CriticalFieldsInterface::Ptr makeCriticals(
    int _totalTx, std::function<vector<bytes>(ID)> _id2CriticalFunc)
{
    CriticalFields::Ptr criticals = make_shared<CriticalFields>(_totalTx);
    for (int i = 0; i < _totalTx; i++)
    {
        criticals->put(i, make_shared<CriticalFields::CriticalField>(_id2CriticalFunc(i)));
    }
    return criticals;
}

void testTxDAG(
    CriticalFieldsInterface::Ptr criticals, shared_ptr<TxDAGInterface> _txDag, string name)
{
    auto startTime = utcSteadyTime();
    cout << endl << name << " test start" << endl;
    _txDag->init(criticals, [&](ID id) {
        if (id % 100000 == 0)
        {
            std::cout << " [" << id << "] ";
        }
    });
    auto initTime = utcSteadyTime();
    try
    {
        _txDag->run(8);
    }
    catch (exception& e)
    {
        std::cout << "Exception" << boost::diagnostic_information(e) << std::endl;
    }
    auto endTime = utcSteadyTime();
    cout << endl
         << name << " cost(ms): initDAG=" << initTime - startTime << " run=" << endTime - initTime
         << " total=" << endTime - startTime << endl;
}


void runDagTest(shared_ptr<TxDAGInterface> _txDag, int _total,
    std::function<vector<bytes>(ID)> _id2CriticalFunc, std::function<void(ID)> _beforeRunCheck,
    std::function<void(ID)> _afterRunCheck)
{
    // ./test-bcos-executor --run_test=TestTxDAG/TestRun
    CriticalFieldsInterface::Ptr criticals = makeCriticals(_total, _id2CriticalFunc);

    _txDag->init(criticals, [&](ID id) {
        _beforeRunCheck(id);
        if (id % 1000 == 0)
        {
            std::cout << " [" << id << "] ";
        }
        _afterRunCheck(id);
    });

    try
    {
        _txDag->run(8);
    }
    catch (exception& e)
    {
        std::cout << "Exception" << boost::diagnostic_information(e) << std::endl;
    }
}

void txDagTest(shared_ptr<TxDAGInterface> txDag)
{
    int total = 1000;
    ID criticalNum = 2;
    vector<int> runnings(criticalNum, -1);

    auto id2CriticalFun = [&](ID id) -> vector<bytes> {
        uint32_t c = id % criticalNum;
        return {bytes{static_cast<uint8_t>(c & 0xff), static_cast<uint8_t>(c & 0xff00 >> 8),
            static_cast<uint8_t>(c & 0xff0000 >> 16), static_cast<uint8_t>(c & 0xff000000 >> 24)}};
    };
    auto beforeRunCheck = [&](ID id) {
        BOOST_CHECK_MESSAGE(runnings[id % criticalNum] == -1,
            "conflict at beginning: " << id << "-" << id % criticalNum << "-"
                                      << runnings[id % criticalNum]);
        runnings[id % criticalNum] = id;
    };
    auto afterRunCheck = [&](ID id) {
        BOOST_CHECK_MESSAGE(runnings[id % criticalNum] != -1,
            "conflict at ending: " << id << "-" << id % criticalNum << "-"
                                   << runnings[id % criticalNum]);
        runnings[id % criticalNum] = -1;
    };

    runDagTest(txDag, total, id2CriticalFun, beforeRunCheck, afterRunCheck);
}

void txDagDeepTreeTest(shared_ptr<TxDAGInterface> txDag)
{
    int total = 1000;
    ID slotNum = 2;
    ID valueNum = 3;  // values num under a slot
    map<int, ID> runnings;

    auto id2CriticalFun = [&](ID id) -> vector<bytes> {
        ID slot = id % slotNum;
        ID value = id % (slotNum * valueNum);

        if (value / slotNum == 0)
        {
            // return only slot
            return {
                bytes{static_cast<uint8_t>(slot & 0xff), static_cast<uint8_t>(slot & 0xff00 >> 8),
                    static_cast<uint8_t>(slot & 0xff0000 >> 16),
                    static_cast<uint8_t>(slot & 0xff000000 >> 24)}};
        }
        else
        {
            return {
                bytes{static_cast<uint8_t>(slot & 0xff), static_cast<uint8_t>(slot & 0xff00 >> 8),
                    static_cast<uint8_t>(slot & 0xff0000 >> 16),
                    static_cast<uint8_t>(slot & 0xff000000 >> 24),
                    static_cast<uint8_t>(value & 0xff), static_cast<uint8_t>(value & 0xff00 >> 8),
                    static_cast<uint8_t>(value & 0xff0000 >> 16),
                    static_cast<uint8_t>(value & 0xff000000 >> 24)}};
        }
    };

    auto beforeRunCheck = [&](ID id) {
        if (id == 0)
        {
            return;
        }

        auto critical = id2CriticalFun(id);
        if (critical[0].size() == 1)
        {
            // only has slot
            ID slot = critical[0][0];
            for (ID i = 0; i < valueNum; i++)
            {
                ID conflictValue = i * slotNum + slot;
                ID unfinishedId = runnings[conflictValue];
                BOOST_CHECK_MESSAGE(unfinishedId == 0,
                    "conflict at beginning, id: " << id << " unfinishedId: " << unfinishedId);
                runnings[conflictValue] = id;  // update to my id
            }
        }
        else
        {
            ID slot = critical[0][0];
            ID unfinishedId = runnings[slot];
            BOOST_CHECK_MESSAGE(unfinishedId == 0,
                "parent conflict at beginning, id: " << id << " unfinishedId: " << unfinishedId);

            ID value = critical[0][1];
            unfinishedId = runnings[value];
            BOOST_CHECK_MESSAGE(unfinishedId == 0,
                "myself conflict at beginning, id: " << id << " unfinishedId: " << unfinishedId);
            runnings[value] = id;  // update to my id
        }
    };
    auto afterRunCheck = [&](ID id) {
        if (id == 0)
        {
            return;
        }

        auto critical = id2CriticalFun(id);
        if (critical[0].size() == 1)
        {
            // only has slot
            ID slot = critical[0][0];
            for (ID i = 0; i < valueNum; i++)
            {
                ID conflictValue = i * slotNum + slot;
                ID unfinishedId = runnings[conflictValue];
                BOOST_CHECK_MESSAGE(unfinishedId == id,
                    "conflict at ending, id: " << id << " unfinishedId: " << unfinishedId);
                runnings[conflictValue] = 0;  // update to 0
            }
        }
        else
        {
            ID slot = critical[0][0];
            ID unfinishedId = runnings[slot];
            BOOST_CHECK_MESSAGE(unfinishedId == 0,
                "parent conflict at ending, id: " << id << " unfinishedId: " << unfinishedId);

            ID value = critical[0][1];
            unfinishedId = runnings[value];
            BOOST_CHECK_MESSAGE(unfinishedId == id,
                "myself conflict at ending, id: " << id << " unfinishedId: " << unfinishedId);
            runnings[value] = 0;  // update to my id
        }
    };

    runDagTest(txDag, total, id2CriticalFun, beforeRunCheck, afterRunCheck);
}

BOOST_AUTO_TEST_CASE(TestRun1)
{
    // ./test-bcos-executor --run_test=TestTxDAG/TestRun1
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG>();
    txDagTest(txDag);
}

BOOST_AUTO_TEST_CASE(TestRun2)
{
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG2>();
    txDagTest(txDag);
}

BOOST_AUTO_TEST_CASE(TestRun3)
{
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG>();
    txDagDeepTreeTest(txDag);
}

BOOST_AUTO_TEST_CASE(TestRun4)
{
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG2>();
    txDagDeepTreeTest(txDag);
}
}
}  // namespace test
}  // namespace bcos
#endif
