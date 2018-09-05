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
 * @file FakeEvmc.h
 * @author: jimmyshi
 * @date 2018-09-04
 */

#include <evmc/evmc.h>
#include <libdevcore/Address.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/SHA3.h>
#include <libethcore/EVMSchedule.h>
#include <libinterpreter/interpreter.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <memory>


namespace dev
{
namespace test
{
class FakeEvmc
{
public:
    explicit FakeEvmc(evmc_instance* _instance);
    virtual ~FakeEvmc() { delete m_context; }

    evmc_result execute(dev::eth::EVMSchedule const& schedule, bytes code, bytes data,
        Address myAddress, Address caller, u256 value, int64_t gas, int32_t depth, bool isCreate,
        bool isStaticCall);

    void destroy();
    // TODO:Support more function

private:
    struct evmc_instance* m_instance;
    struct evmc_context* m_context;

    inline evmc_address toEvmC(Address const& _addr)
    {
        return reinterpret_cast<evmc_address const&>(_addr);
    }

    inline evmc_uint256be toEvmC(h256 const& _h)
    {
        return reinterpret_cast<evmc_uint256be const&>(_h);
    }

    inline u256 fromEvmC(evmc_uint256be const& _n) { return fromBigEndian<u256>(_n.bytes); }

    inline Address fromEvmC(evmc_address const& _addr)
    {
        return reinterpret_cast<Address const&>(_addr);
    }

    inline evmc_revision toRevision(dev::eth::EVMSchedule const& _schedule)
    {
        if (_schedule.haveCreate2)
            return EVMC_CONSTANTINOPLE;
        if (_schedule.haveRevert)
            return EVMC_BYZANTIUM;
        if (_schedule.eip158Mode)
            return EVMC_SPURIOUS_DRAGON;
        if (_schedule.eip150Mode)
            return EVMC_TANGERINE_WHISTLE;
        if (_schedule.haveDelegateCall)
            return EVMC_HOMESTEAD;
        return EVMC_FRONTIER;
    }
};

}  // namespace test
}  // namespace dev
