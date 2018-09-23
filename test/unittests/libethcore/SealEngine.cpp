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
 * @brief
 *
 * @file SealEngine.cpp of Ethcore
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libethcore/EVMSchedule.h>
#include <libethcore/SealEngine.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace test
{
class NoProof : public SealEngineFace
{
public:
    static void init() {}
    void generateSeal(BlockHeader const& _bi) override
    {
        bytes ret;
        _bi.encode(ret);
        if (m_onSealGenerated)
            m_onSealGenerated(ret);
    }
    void onSealGenerated(std::function<void(bytes const&)> const& _f) override
    {
        m_onSealGenerated = _f;
    }
    EVMSchedule const& evmSchedule(u256 const& _blockNumber) const override
    {
        return FiscoBcosSchedule;
    }

protected:
    std::function<void(bytes const& s)> m_onSealGenerated;
};

BOOST_FIXTURE_TEST_SUITE(EthcoreSealEngineTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testEthcoreSealEngine)
{
    ETH_REGISTER_SEAL_ENGINE(NoProof);
    auto engine = SealEngineRegistrar::create("NoProof");
    BOOST_REQUIRE(engine != nullptr);  // because existed

    SealEngineRegistrar::unregisterSealEngine("NoProof");
    auto sealEngine = SealEngineRegistrar::create("NoProof");
    BOOST_REQUIRE(sealEngine == nullptr);

    SealEngineRegistrar::registerSealEngine<NoProof>("NoProof");
    sealEngine = SealEngineRegistrar::create("NoProof");
    BOOST_REQUIRE(sealEngine != nullptr);

    SealEngineRegistrar::unregisterSealEngine("NoProof");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev