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
#include <libprecompiled/ChainGovernancePrecompiled.h>
#include <libprecompiled/PermissionPrecompiled.h>
#include <libprecompiled/PrecompiledResult.h>
#include <libprotocol/ContractABICodec.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::storagestate;
using namespace bcos::precompiled;

namespace test_ChainGovernancePrecompiled
{
struct ChainGovernancePrecompiledFixture
{
    ChainGovernancePrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        auto memStorage = std::make_shared<MemoryStorage2>();
        tableFactory = std::make_shared<MemoryTableFactory>();
        tableFactory->setStateStorage(memStorage);
        context = std::make_shared<bcos::blockverifier::ExecutiveContext>();
        context->setMemoryTableFactory(tableFactory);
        context->setBlockInfo(blockInfo);
        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        chainGovernancePrecompiled =
            std::make_shared<bcos::precompiled::ChainGovernancePrecompiled>();
        chainGovernancePrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~ChainGovernancePrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    MemoryTableFactory::Ptr tableFactory;
    Precompiled::Ptr chainGovernancePrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(test_ChainGovernancePrecompiled, ChainGovernancePrecompiledFixture)

BOOST_AUTO_TEST_CASE(grant_revoke_CM)
{
    protocol::ContractABICodec abi;
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hexPrefixed());
    auto result = acTable->select(SYS_ACCESS_TABLE, condition);
    BOOST_TEST(result->size() == 1);
    condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hexPrefixed());
    auto entries = acTable->select(SYS_CONFIG, condition);
    BOOST_TEST(entries->size() == 1);
    entries = acTable->select(SYS_CONSENSUS, condition);
    BOOST_TEST(entries->size() == 1);

    // insert again
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_COMMITTEE_MEMBER_EXIST);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
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

    // queryVotesOfMember member2
    in = abi.abiIn("queryVotesOfMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    string votes;
    abi.abiOut(&out->execResult(), votes);
    BOOST_TEST(reader.parse(votes, retJson) == true);
    BOOST_TEST(retJson["revoke"].size() == 0);
    BOOST_TEST(retJson["grant"].size() == 0);
    BOOST_TEST(retJson["update_weight"].size() == 0);

    // grantCommitteeMember member2
    in = abi.abiIn("grantCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);
    condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member2.hexPrefixed());
    entries = acTable->select(SYS_CONFIG, condition);
    BOOST_TEST(entries->size() == 1);
    entries = acTable->select(SYS_CONSENSUS, condition);
    BOOST_TEST(entries->size() == 1);

    // grantCommitteeMember member3
    Address member3("0x420f853b49838bd3e9466c85a4cc3428c960dde3");
    in = abi.abiIn("grantCommitteeMember(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);

    auto in2 = abi.abiIn("queryVotesOfMember(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in2), member1);
    abi.abiOut(&out->execResult(), votes);
    BOOST_TEST(reader.parse(votes, retJson) == true);
    BOOST_TEST(retJson["revoke"].size() == 0);
    BOOST_TEST(retJson["grant"].size() == 1);
    BOOST_TEST(retJson["grant"][(Json::ArrayIndex)0]["block_limit"].asString() == "10000");
    BOOST_TEST(retJson["grant"][(Json::ArrayIndex)0]["voter"].asString() == member1.hexPrefixed());
    BOOST_TEST(retJson["update_weight"].size() == 0);

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

    // queryVotesOfMember member3
    in2 = abi.abiIn("queryVotesOfMember(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in2), member1);
    abi.abiOut(&out->execResult(), votes);
    BOOST_TEST(reader.parse(votes, retJson) == true);
    BOOST_TEST(retJson["revoke"].size() == 1);
    BOOST_TEST(retJson["revoke"][(Json::ArrayIndex)0]["block_limit"].asString() == "10000");
    BOOST_TEST(retJson["revoke"][(Json::ArrayIndex)0]["voter"].asString() == member1.hexPrefixed());
    BOOST_TEST(retJson["grant"].size() == 0);
    BOOST_TEST(retJson["update_weight"].size() == 0);

    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);
    condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member3.hexPrefixed());
    entries = acTable->select(SYS_CONFIG, condition);
    BOOST_TEST(entries->size() == 0);
    entries = acTable->select(SYS_CONSENSUS, condition);
    BOOST_TEST(entries->size() == 0);

    // member3 revokeOperator member3
    in = abi.abiIn("revokeOperator(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member3);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST_PERMISSION_DENIED);
    // member3 revokeOperator member3
    in = abi.abiIn("grantOperator(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member3);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST_PERMISSION_DENIED);

    // revokeCommitteeMember member2
    in = abi.abiIn("revokeCommitteeMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    entries = acTable->select(SYS_ACCESS_TABLE, acTable->newCondition());
    BOOST_TEST(entries->size() == 2u);

    // queryVotesOfMember member2
    in2 = abi.abiIn("queryVotesOfMember(address)", member2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in2), member2);
    abi.abiOut(&out->execResult(), votes);
    BOOST_TEST(reader.parse(votes, retJson) == true);
    BOOST_TEST(retJson["revoke"].size() == 1);
    BOOST_TEST(retJson["revoke"][(Json::ArrayIndex)0]["block_limit"].asString() == "10000");
    BOOST_TEST(retJson["revoke"][(Json::ArrayIndex)0]["voter"].asString() == member1.hexPrefixed());
    BOOST_TEST(retJson["grant"].size() == 0);
    BOOST_TEST(retJson["update_weight"].size() == 0);

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

BOOST_AUTO_TEST_CASE(grant_first_committee)
{
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto entry = acTable->newEntry();
    entry->setField(SYS_AC_TABLE_NAME, SYS_ACCESS_TABLE);
    entry->setField(SYS_AC_ADDRESS, member1.hexPrefixed());
    entry->setField(SYS_AC_ENABLENUM, to_string(0));
    int count = acTable->insert(SYS_TABLES, entry, make_shared<AccessOptions>(Address(), false));
    BOOST_TEST(count == 1);

    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_OPERATOR_CANNOT_BE_COMMITTEE_MEMBER);
}

BOOST_AUTO_TEST_CASE(updateCommitteeMemberWeight)
{
    protocol::ContractABICodec abi;
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hexPrefixed());
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
    BOOST_TEST(ret == CODE_INVALID_REQUEST_PERMISSION_DENIED);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    // queryVotesOfMember member1
    auto in2 = abi.abiIn("queryVotesOfMember(address)", member1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in2), member2);
    string votes;
    abi.abiOut(&out->execResult(), votes);
    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(votes, retJson) == true);
    BOOST_TEST(retJson["revoke"].size() == 0);
    BOOST_TEST(retJson["grant"].size() == 0);
    BOOST_TEST(retJson["update_weight"].size() == 1);
    BOOST_TEST(retJson["update_weight"][(Json::ArrayIndex)0]["block_limit"].asString() == "10000");
    BOOST_TEST(
        retJson["update_weight"][(Json::ArrayIndex)0]["voter"].asString() == member1.hexPrefixed());

    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_CURRENT_VALUE_IS_EXPECTED_VALUE);

    // queryCommitteeMemberWeight member1 2
    in = abi.abiIn("queryCommitteeMemberWeight(address)", member1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    bool ok = false;
    abi.abiOut(&out->execResult(), ok, ret);
    BOOST_TEST(ret == 2);
    BOOST_TEST(ok == true);

    // queryCommitteeMemberWeight member3
    in = abi.abiIn("queryCommitteeMemberWeight(address)", member3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    ok = true;
    abi.abiOut(&out->execResult(), ok, ret);
    BOOST_TEST(ret == CODE_COMMITTEE_MEMBER_NOT_EXIST);
    BOOST_TEST(ok == false);

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
    protocol::ContractABICodec abi;
    Address member1("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    bytes in = abi.abiIn("grantCommitteeMember(address)", member1);
    auto out = chainGovernancePrecompiled->call(context, bytesConstRef(&in));
    s256 ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    auto acTable = tableFactory->openTable(SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, member1.hexPrefixed());
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
    BOOST_TEST(ret == CODE_INVALID_REQUEST_PERMISSION_DENIED);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 0);
    // queryVotesOfMember member2
    auto in2 = abi.abiIn("queryVotesOfThreshold()", member1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in2), member2);
    string votes;
    abi.abiOut(&out->execResult(), votes);
    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(votes, retJson) == true);
    BOOST_TEST(retJson.size() == 1);
    BOOST_TEST(retJson["0.400000"].size() == 1);
    BOOST_TEST(retJson["0.400000"][(Json::ArrayIndex)0]["block_limit"].asString() == "10000");
    BOOST_TEST(
        retJson["0.400000"][(Json::ArrayIndex)0]["voter"].asString() == member1.hexPrefixed());

    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_CURRENT_VALUE_IS_EXPECTED_VALUE);

    // updateThreshold 0.02
    in = abi.abiIn("updateThreshold(int256)", 2);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member3);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_INVALID_REQUEST_PERMISSION_DENIED);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member1);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);

    in = abi.abiIn("updateThreshold(int256)", 3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == 1);
    in = abi.abiIn("updateThreshold(int256)", 3);
    out = chainGovernancePrecompiled->call(context, bytesConstRef(&in), member2);
    ret = 0;
    abi.abiOut(&out->execResult(), ret);
    BOOST_TEST(ret == CODE_CURRENT_VALUE_IS_EXPECTED_VALUE);

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
    BOOST_TEST(ret == 3);

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
