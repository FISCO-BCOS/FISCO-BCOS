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
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief : test_VRFBasedPBFT.cpp
 * @author: yujiechen
 * @date: 2020-07-07
 */
#include "test_VRFBasedrPBFT.h"
#include <libprecompiled/WorkingSealerManagerPrecompiled.h>

using namespace dev::test;
using namespace dev::precompiled;

BOOST_FIXTURE_TEST_SUITE(VRFBasedrPBFTTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testVRFBasedrPBFTSealer)
{
    // create fixture with 10 sealers
    auto fixture = std::make_shared<VRFBasedrPBFTFixture>(10);
    auto fakedSealer = fixture->vrfBasedrPBFTSealer();
    auto engine = fixture->vrfBasedrPBFT();

    // engine notify the sealer to rotate workingSealers
    engine->setShouldRotateSealers(true);
    fakedSealer->hookAfterHandleBlock();

    // check the first transaction is the WorkingSealerManagerPrecompiled
    auto rotatingTx = (*(fakedSealer->mutableSealing().block->transactions()))[0];
    // check from
    BOOST_CHECK(rotatingTx->from() == toAddress(engine->keyPair().pub()));
    // check to
    BOOST_CHECK(rotatingTx->to() == WORKING_SEALER_MGR_ADDRESS);
    // check transaction signature
    BOOST_CHECK(rotatingTx->sender() == toAddress(engine->keyPair().pub()));
    // check transaction type
    BOOST_CHECK(rotatingTx->type() == dev::eth::Transaction::Type::MessageCall);
    // check input
    auto const& txData = rotatingTx->data();
    ContractABI abi;
    std::string vrfPublicKey;
    std::string blockHashStr;
    std::string vrfProof;
    string interface = WSM_METHOD_ROTATE_STR;

    auto selector = getFuncSelectorByFunctionName(interface);
    auto functionSelector = getParamFunc(ref(rotatingTx->data()));
    BOOST_CHECK(functionSelector == selector);

    auto paramData = getParamData(ref(txData));
    BOOST_CHECK_NO_THROW(abi.abiOut(paramData, vrfPublicKey, blockHashStr, vrfProof));
    // check params
    auto blockNumber = engine->blockChain()->number();
    auto blockHash = engine->blockChain()->numberHash(blockNumber);
    // check vrf input
    BOOST_CHECK(h256(blockHashStr) == blockHash);
    // check vrf publicKey
    BOOST_CHECK(fakedSealer->vrfPublicKey() == vrfPublicKey);
    // check vrfProof
    BOOST_CHECK(
        curve25519_vrf_verify(vrfPublicKey.c_str(), blockHashStr.c_str(), vrfProof.c_str()) == 0);
}
BOOST_AUTO_TEST_SUITE_END()