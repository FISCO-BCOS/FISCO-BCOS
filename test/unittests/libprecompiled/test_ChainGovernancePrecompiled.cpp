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
 * @brief: unit test for Authority
 *
 * @file test_ChainGovernancePrecompiled.cpp
 * @author: xingqiangbai
 * @date 20190326
 */

#include "../libstorage/MemoryStorage.h"
#include "../libstorage/MemoryStorage2.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ChainGovernancePrecompiled.h>
#include <libprecompiled/PermissionPrecompiled.h>
#include <libprecompiled/PrecompiledResult.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_ChainGovernancePrecompiled
{
struct ChainGovernancePrecompiledFixture
{
    ChainGovernancePrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        auto memStorage = std::make_shared<MemoryStorage2>();
        tableFactory = std::make_shared<MemoryTableFactory2>();
        tableFactory->setStateStorage(memStorage);
        context = std::make_shared<dev::blockverifier::ExecutiveContext>();
        context->setMemoryTableFactory(tableFactory);
        context->setBlockInfo(blockInfo);
        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        chainGovernancePrecompiled =
            std::make_shared<dev::precompiled::ChainGovernancePrecompiled>();
        chainGovernancePrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~ChainGovernancePrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    MemoryTableFactory2::Ptr tableFactory;
    Precompiled::Ptr chainGovernancePrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(test_ChainGovernancePrecompiled, ChainGovernancePrecompiledFixture)

BOOST_AUTO_TEST_CASE(grant_revoke_CM)
{
    eth::ContractABI abi;
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hex());
    auto result = acTable->select(SYS_ACCESS_TABLE, condition);
    BOOST_TEST(result->size() == 1);

    // insert again
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_COMMITTEE_MEMBER_EXIST);
    auto entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // grantOperator member1
    in = abi.abiIn("grantOperator(address)", member1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_COMMITTEE_MEMBER_CANNOT_BE_OPERATOR);
    entries = acTable->select(SYS_TABLES, acTable->newCondition());
    BOOST_TEST(entries->size() == 0);

    // grantOperator member2
    Address member2("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    in = abi.abiIn("grantOperator(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1u);
    entries = acTable->select(SYS_TABLES, acTable->newCondition());
    BOOST_TEST(entries->size() == 1);

    // grantCommitteeMember member2 failed
    in = abi.abiIn("grantCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_OPERATOR_CANNOT_BE_COMMITTEE_MEMBER);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // listOperators
    in = abi.abiIn("listOperators()");
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    std::string retStr;
    abi.abiOut(&out->execResult(), retStr);

    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 1);

    // revokeOperator member2
    in = abi.abiIn("revokeOperator(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_TABLES, acTable->newCondition());
    BOOST_TEST(entries->size() == 0);

    // grantCommitteeMember member2
    in = abi.abiIn("grantCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // grantCommitteeMember member3
    Address member3("0x420f853b49838bd3e9466c85a4cc3428c960dde3");
    in = abi.abiIn("grantCommitteeMember(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    // member1 grant member2 again, but should not work
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 3u);

    // revokeCommitteeMember member3
    in = abi.abiIn("revokeCommitteeMember(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // revokeCommitteeMember member2
    in = abi.abiIn("revokeCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // expired votes is deleted, so member vote will not make member2 to be revoke
    blockInfo.hash = h256(0);
    blockInfo.number = 10001;
    context->setBlockInfo(blockInfo);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // listCommitteeMembers
    in = abi.abiIn("listCommitteeMembers()");
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    retStr = "";
    abi.abiOut(&out->execResult(), retStr);

    Json::Value committeeMembers;
    BOOST_TEST(reader.parse(retStr, committeeMembers) == true);
    BOOST_TEST(committeeMembers.size() == 2);
}

BOOST_AUTO_TEST_CASE(updateCommitteeMemberWeight)
{
    eth::ContractABI abi;
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hex());
    auto entries = acTable->select(SYS_ACCESS_TABLE, condition);
    BOOST_TEST(entries->size() == 1);

    // grantCommitteeMember member2
    Address member2("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    in = abi.abiIn("grantCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // updateCommitteeMemberWeight member1 2
    Address member3("0x420f853b49838bd3e9466c85a4cc3428c960dde3");
    in = abi.abiIn("updateCommitteeMemberWeight(address,int256)", member1, 2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member3);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST);

    // queryCommitteeMemberWeight member1 2
    in = abi.abiIn("queryCommitteeMemberWeight(address)", member1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 2);

    // grantCommitteeMember member3
    in = abi.abiIn("grantCommitteeMember(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 3u);
}

BOOST_AUTO_TEST_CASE(updateThreshold)
{
    eth::ContractABI abi;
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hex());
    auto entries = acTable->select(SYS_ACCESS_TABLE, condition);
    BOOST_TEST(entries->size() == 1);

    // grantCommitteeMember member2
    Address member2("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    in = abi.abiIn("grantCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // updateThreshold 0.4
    Address member3("0x420f853b49838bd3e9466c85a4cc3428c960dde3");
    in = abi.abiIn("updateThreshold(int256)", 40);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member3);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST);

    in = abi.abiIn("updateThreshold(int256)", 101);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_THRESHOLD);
    in = abi.abiIn("updateThreshold(int256)", -1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_THRESHOLD);

    // queryThreshold member1 2
    in = abi.abiIn("queryThreshold()");
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 40);

    // revokeCommitteeMember member2
    in = abi.abiIn("revokeCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 1u);
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(chainGovernancePrecompiled->toString() == "ChainGovernancePrecompiled");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_ChainGovernancePrecompiled
