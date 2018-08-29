/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief Construct a new boost auto test case object for Worker
 *
 * @file Worker.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libdevcore/Worker.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace std;

namespace dev
{
namespace test
{
class TestWorkerImpl : public Worker
{
public:
    TestWorkerImpl() : Worker("TestWorkerImpl", 10) {}
    void run() { startWorking(); }
    void stop() { stopWorking(); }

protected:
    virtual void startedWorking() { cout << "start..." << endl; }
    virtual void doWork()
    {
        cout << "count:" << count << endl;
        count++;
    }
    virtual void doneWorking() { cout << "end..." << endl; }

private:
    int count = 0;
};

BOOST_FIXTURE_TEST_SUITE(Worker, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testWorker)
{
    TestWorkerImpl workerImpl;
    workerImpl.run();
    usleep(1000 * 10 * 5);
    workerImpl.stop();
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
