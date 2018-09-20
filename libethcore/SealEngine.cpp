/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "SealEngine.h"
#include "Transaction.h"

using namespace std;
using namespace dev;
using namespace eth;

SealEngineRegistrar* SealEngineRegistrar::s_this = nullptr;
void SealEngineFace::verify(
    Strictness _s, BlockHeader const& _bi, BlockHeader const& _parent, bytesConstRef _block) const
{
    _bi.verify(_s, _parent, _block);

    if (_s != CheckNothingNew)
    {
        if (_bi.gasLimit() < chainParams().minGasLimit)
            BOOST_THROW_EXCEPTION(InvalidGasLimit() << RequirementError(
                                      bigint(chainParams().minGasLimit), bigint(_bi.gasLimit())));

        if (_bi.gasLimit() > chainParams().maxGasLimit)
            BOOST_THROW_EXCEPTION(InvalidGasLimit() << RequirementError(
                                      bigint(chainParams().maxGasLimit), bigint(_bi.gasLimit())));
    }

    if (_parent)
    {
        auto gasLimit = _bi.gasLimit();
        auto parentGasLimit = _parent.gasLimit();
        if (gasLimit < chainParams().minGasLimit || gasLimit > chainParams().maxGasLimit ||
            gasLimit <= parentGasLimit - parentGasLimit / chainParams().gasLimitBoundDivisor ||
            gasLimit >= parentGasLimit + parentGasLimit / chainParams().gasLimitBoundDivisor)
            BOOST_THROW_EXCEPTION(
                InvalidGasLimit()
                << errinfo_min(
                       (bigint)((bigint)parentGasLimit -
                                (bigint)(parentGasLimit / chainParams().gasLimitBoundDivisor)))
                << errinfo_got((bigint)gasLimit)
                << errinfo_max((bigint)((bigint)parentGasLimit +
                                        parentGasLimit / chainParams().gasLimitBoundDivisor)));
    }
}

void SealEngineFace::populateFromParent(BlockHeader& _bi, BlockHeader const& _parent) const
{
    _bi.populateFromParent(_parent);
}

void SealEngineFace::verifyTransaction(ImportRequirements::value _ir, Transaction const& _t,
    BlockHeader const& _header, u256 const& _gasUsed) const
{
    eth::EVMSchedule const& schedule = evmSchedule(_header.number());

    // Pre calculate the gas needed for execution
    if ((_ir & ImportRequirements::TransactionBasic) && _t.baseGasRequired(schedule) > _t.gas())
        BOOST_THROW_EXCEPTION(OutOfGasIntrinsic() << RequirementError(
                                  (bigint)(_t.baseGasRequired(schedule)), (bigint)_t.gas()));

    // Avoid transactions that would take us beyond the block gas limit.
    if (_gasUsed + (bigint)_t.gas() > _header.gasLimit())
        BOOST_THROW_EXCEPTION(BlockGasLimitReached() << RequirementErrorComment(
                                  (bigint)(_header.gasLimit() - _gasUsed), (bigint)_t.gas(),
                                  string("_gasUsed + (bigint)_t.gas() > _header.gasLimit()")));
}

SealEngineFace* SealEngineRegistrar::create(ChainOperationParams const& _params)
{
    SealEngineFace* ret = create(_params.sealEngineName);
    assert(ret && "Seal engine not found.");
    if (ret)
        ret->setChainParams(_params);
    return ret;
}
