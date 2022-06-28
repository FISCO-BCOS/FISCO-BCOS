#pragma once
#include "DmcStepRecorder.h"
#include "Executive.h"
#include "ExecutivePool.h"
#include "ExecutorManager.h"
#include "GraphKeyLock.h"
#include <bcos-framework/interfaces/protocol/Block.h>
#include <tbb/concurrent_set.h>
#include <tbb/concurrent_unordered_map.h>
#include <string>


using namespace bcos::scheduler;

namespace bcos::test
{
struct DmcExecutorFixture
{
    DmcExecutorFixture()
    {
    }
};
BOOST_FIXTURE_TEST_SUITE(TestDmcExecutor, DmcExecutorFixture)

BOOST_AUTO_TEST_CASE(Test)
{
    
}
}; //namespace bcos::test