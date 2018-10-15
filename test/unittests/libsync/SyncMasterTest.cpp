/*
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
 */

/**
 * @brief : sync master test
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#include "FakeSyncMaster.h"
#include <libsync/SyncMaster.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;

namespace dev
{
namespace test
{
class SyncFixture : public TestOutputHelperFixture
{
public:
    SyncFixture()
      : fakeBlockChain(), fakeTxPool(66), fakeSyncMaster(fakeBlockChain, fakeTxPool, 66){};
    FakeBlockChain fakeBlockChain;
    FakeTxPool fakeTxPool;
    SyncMaster fakeSyncMaster;
};

BOOST_FIXTURE_TEST_SUITE(SyncMasterTest, SyncFixture)

BOOST_AUTO_TEST_CASE(ProtocolIdTest)
{
    cout << "ProtocolId " << fakeSyncMaster.protocolId() << endl;
    BOOST_CHECK(fakeSyncMaster.protocolId() == 66);

    fakeSyncMaster.setProtocolId(55);
    cout << "ProtocolId change to" << fakeSyncMaster.protocolId() << endl;
    BOOST_CHECK(fakeSyncMaster.protocolId() == 55);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev