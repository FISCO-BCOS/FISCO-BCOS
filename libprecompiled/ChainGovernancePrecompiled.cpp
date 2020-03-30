/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ChainGovernancePrecompiled.h
 *  @author xingqiangbai
 *  @date 20190324
 */
#include "ChainGovernancePrecompiled.h"
#include "libstorage/Table.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const char* const CGP_METHOD_GRANT_CM = "grantCommitteeMember(address)";
const char* const CGP_METHOD_REVOKE_CM = "revokeCommitteeMember(address)";
const char* const CGP_METHOD_LIST_CM = "listCommitteeMembers()";
const char* const CGP_METHOD_UPDATE_CM_WEIGHT = "updateCommitteeMemberWeight(address,int256)";
const char* const CGP_METHOD_UPDATE_CM_THRESHOLD = "updateThreshold(int256)";
const char* const CGP_METHOD_QUERY_CM_THRESHOLD = "queryThreshold()";
const char* const CGP_METHOD_QUERY_CM_WEIGHT = "queryCommitteeMemberWeight(address)";

const char* const CGP_METHOD_GRANT_OP = "grantOperator(address)";
const char* const CGP_METHOD_REVOKE_OP = "revokeOperator(address)";
const char* const CGP_METHOD_LIST_OP = "listOperators()";

const char* const CGP_COMMITTEE_TABLE = "_sys_committee_votes_";
const char* const CGP_COMMITTEE_TABLE_KEY = "key";
const char* const CGP_COMMITTEE_TABLE_VALUE = "value";
const char* const CGP_COMMITTEE_TABLE_ORIGIN = "origin";
const char* const CGP_COMMITTEE_TABLE_BLOCKLIMIT = "block_limit";
const char* const CGP_WEIGTH_SUFFIX = "_weight";
const char* const CGP_UPDATE_WEIGTH_SUFFIX = "_update_weight";
const char* const CGP_AUTH_THRESHOLD = "auth_threshold";
const char* const CGP_UPDATE_AUTH_THRESHOLD = "update_auth_threshold";

#define CHAIN_GOVERNANCE_LOG(LEVEL) PRECOMPILED_LOG(LEVEL) << "[ChainGovernance]"

ChainGovernancePrecompiled::ChainGovernancePrecompiled()
{
    name2Selector[CGP_METHOD_GRANT_CM] = getFuncSelector(CGP_METHOD_GRANT_CM);
    name2Selector[CGP_METHOD_REVOKE_CM] = getFuncSelector(CGP_METHOD_REVOKE_CM);
    name2Selector[CGP_METHOD_LIST_CM] = getFuncSelector(CGP_METHOD_LIST_CM);
    name2Selector[CGP_METHOD_UPDATE_CM_WEIGHT] = getFuncSelector(CGP_METHOD_UPDATE_CM_WEIGHT);
    name2Selector[CGP_METHOD_UPDATE_CM_THRESHOLD] = getFuncSelector(CGP_METHOD_UPDATE_CM_THRESHOLD);
    name2Selector[CGP_METHOD_GRANT_OP] = getFuncSelector(CGP_METHOD_GRANT_OP);
    name2Selector[CGP_METHOD_REVOKE_OP] = getFuncSelector(CGP_METHOD_REVOKE_OP);
    name2Selector[CGP_METHOD_LIST_OP] = getFuncSelector(CGP_METHOD_LIST_OP);
    name2Selector[CGP_METHOD_QUERY_CM_THRESHOLD] = getFuncSelector(CGP_METHOD_QUERY_CM_THRESHOLD);
    name2Selector[CGP_METHOD_QUERY_CM_WEIGHT] = getFuncSelector(CGP_METHOD_QUERY_CM_WEIGHT);
}

string ChainGovernancePrecompiled::toString()
{
    return "ChainGovernancePrecompiled";
}

PrecompiledExecResult::Ptr ChainGovernancePrecompiled::call(
    ExecutiveContext::Ptr _context, bytesConstRef _param, Address const&, Address const&)
{
    CHAIN_GOVERNANCE_LOG(TRACE) << LOG_DESC("call") << LOG_KV("param", toHex(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    dev::eth::ContractABI abi;
    bytes out;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    if (func == name2Selector[CGP_METHOD_GRANT_CM])
    {  // grantCommitteeMember(address user) public returns (int256);
    }
    else if (func == name2Selector[CGP_METHOD_REVOKE_CM])
    {  // revokeCommitteeMember(address user) public returns (int256);
    }
    else if (func == name2Selector[CGP_METHOD_UPDATE_CM_WEIGHT])
    {  // updateCommitteeMemberWeight(address user, int256 weight)
    }
    else if (func == name2Selector[CGP_METHOD_UPDATE_CM_THRESHOLD])
    {  // function updateThreshold(int256 threshold) public returns (int256);
    }
    else if (func == name2Selector[CGP_METHOD_LIST_CM])
    {  // listCommitteeMembers();
        auto resultJson = queryCommitteeMembers(_context, SYS_ACCESS_TABLE);
        callResult->setExecResult(abi.abiIn("", resultJson));
    }
    else if (func == name2Selector[CGP_METHOD_GRANT_OP])
    {  // grantOperator(address)
    }
    else if (func == name2Selector[CGP_METHOD_REVOKE_OP])
    {  // revokeOperator(address)
    }
    else if (func == name2Selector[CGP_METHOD_LIST_OP])
    {  // listOperators()
        auto resultJson = queryCommitteeMembers(_context, SYS_TABLES);
        callResult->setExecResult(abi.abiIn("", resultJson));
    }
    else if (func == name2Selector[CGP_METHOD_QUERY_CM_THRESHOLD])
    {  // queryThreshold() public view returns (int256);
        auto committeeTable = getCommitteeTable(_context);
        auto entries = committeeTable->select(CGP_AUTH_THRESHOLD, committeeTable->newCondition());
        auto entry = entries->get(0);
        auto threshold =
            boost::lexical_cast<double>(entry->getField(CGP_COMMITTEE_TABLE_VALUE)) * 100;
        CHAIN_GOVERNANCE_LOG(INFO) << LOG_DESC("queryThreshold") << LOG_KV("return", threshold);
        callResult->setExecResult(abi.abiIn("", s256(threshold)));
    }
    else if (func == name2Selector[CGP_METHOD_QUERY_CM_WEIGHT])
    {  // queryCommitteeMemberWeight(address user) public view returns (int256);
        Address user;
        abi.abiOut(data, user);
        string member = user.hex();
        auto committeeTable = getCommitteeTable(_context);
        auto entries =
            committeeTable->select(member + CGP_WEIGTH_SUFFIX, committeeTable->newCondition());
        auto entry = entries->get(0);
        s256 weight = boost::lexical_cast<int>(entry->getField(CGP_COMMITTEE_TABLE_VALUE));
        CHAIN_GOVERNANCE_LOG(INFO) << LOG_DESC("memberWeight") << LOG_KV("weight", weight);
        callResult->setExecResult(abi.abiIn("", weight));
    }
    else
    {
        CHAIN_GOVERNANCE_LOG(ERROR) << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    return callResult;
}

Table::Ptr ChainGovernancePrecompiled::getCommitteeTable(
    shared_ptr<dev::blockverifier::ExecutiveContext> _context)
{
    auto table = openTable(_context, CGP_COMMITTEE_TABLE);
    if (!table)
    {
        table = createTable(
            _context, CGP_COMMITTEE_TABLE, CGP_COMMITTEE_TABLE_KEY, "value,origin,block_limit");
        auto entry = table->newEntry();
        entry->setField(CGP_COMMITTEE_TABLE_VALUE, to_string(0.5));
        table->insert(CGP_AUTH_THRESHOLD, entry, make_shared<AccessOptions>(Address(), false));
    }
    return table;
}

int ChainGovernancePrecompiled::verifyAndRecord(
    shared_ptr<dev::blockverifier::ExecutiveContext> _context, Operation _op, const string& _user,
    const string& _value, const string& _origin)
{
    Table::Ptr acTable = openTable(_context, SYS_ACCESS_TABLE);
    auto condition = acTable->newCondition();
    condition->EQ(SYS_AC_ADDRESS, _origin);
    auto entries = acTable->select(SYS_ACCESS_TABLE, condition);
    if (entries->size() == 0u)
    {
        CHAIN_GOVERNANCE_LOG(INFO)
            << LOG_DESC("verifyAndRecord invalid request") << LOG_KV("origin", _origin)
            << LOG_KV("member", _user) << LOG_KV("return", CODE_INVALID_REQUEST);
        return CODE_INVALID_REQUEST;
    }
    auto committeeTable = getCommitteeTable(_context);

    auto recordVote = [committeeTable, _context](
                          const string& key, const string& value, const string& origin) {
        auto entry = committeeTable->newEntry();
        entry->setField(CGP_COMMITTEE_TABLE_VALUE, value);
        entry->setField(CGP_COMMITTEE_TABLE_ORIGIN, origin);
        entry->setField(CGP_COMMITTEE_TABLE_BLOCKLIMIT,
            to_string(_context->blockInfo().number + g_BCOSConfig.c_blockLimit * 10));
        auto condition = committeeTable->newCondition();
        condition->EQ(CGP_COMMITTEE_TABLE_ORIGIN, origin);
        auto entries = committeeTable->select(key, condition);
        if (entries->size() != 0)
        {  // duplicate vote, update
            committeeTable->update(
                key, entry, condition, make_shared<AccessOptions>(Address(), false));
        }
        committeeTable->insert(key, entry, make_shared<AccessOptions>(Address(), false));
    };

    auto validate = [acTable, committeeTable](
                        const string& key, const string& value, int64_t blockNumber) -> bool {
        auto condition = acTable->newCondition();
        condition->LE(SYS_AC_ENABLENUM, to_string(blockNumber));
        auto entries = acTable->select(SYS_ACCESS_TABLE, condition);
        if (entries->size() == 0u)
        {  // check if already have at least one committee member
            CHAIN_GOVERNANCE_LOG(ERROR) << LOG_DESC("nobody has authority of " + SYS_ACCESS_TABLE);
            return true;
        }
        double total = 0;
        map<string, double> memberWeight;
        for (size_t i = 0; i < entries->size(); i++)
        {  // calculate valiad votes weight
            auto entry = entries->get(i);
            auto member = entry->getField(SYS_AC_ADDRESS);
            auto weightEntries =
                committeeTable->select(member + CGP_WEIGTH_SUFFIX, committeeTable->newCondition());
            auto weightEntry = weightEntries->get(0);
            memberWeight[member] =
                boost::lexical_cast<double>(weightEntry->getField(CGP_COMMITTEE_TABLE_VALUE));
            total += memberWeight[member];
        }
        condition = committeeTable->newCondition();
        condition->EQ(CGP_COMMITTEE_TABLE_VALUE, value);
        condition->GE(CGP_COMMITTEE_TABLE_BLOCKLIMIT, to_string(blockNumber));
        auto votes = committeeTable->select(key, condition);
        double totalVotes = 0;
        for (size_t i = 0; i < votes->size(); i++)
        {  // calculate total members weight
            auto entry = votes->get(i);
            auto member = entry->getField(CGP_COMMITTEE_TABLE_ORIGIN);
            totalVotes += memberWeight[member];
        }
        double threshold = 0.5;
        auto thresholdEntries =
            committeeTable->select(CGP_AUTH_THRESHOLD, committeeTable->newCondition());
        if (thresholdEntries->size() != 0)
        {  // get thresold
            auto thresholdEntry = thresholdEntries->get(0);
            threshold =
                boost::lexical_cast<double>(thresholdEntry->getField(CGP_COMMITTEE_TABLE_VALUE));
        }
        CHAIN_GOVERNANCE_LOG(INFO)
            << LOG_DESC("validate votes") << LOG_KV("key", key) << LOG_KV("value", value)
            << LOG_KV("vote", totalVotes / total) << LOG_KV("threshold", threshold);
        return totalVotes / total > threshold;
    };
    auto deleteUsedVotes = [committeeTable](const string& key, const string& value) {
        auto condition = committeeTable->newCondition();
        condition->EQ(CGP_COMMITTEE_TABLE_VALUE, value);
        committeeTable->remove(key, condition);
    };
    int64_t currentBlockNumber = _context->blockInfo().number + 1;

    switch (_op)
    {
    case GrantCommitteeMember:
    {
        auto value = "grant";
        recordVote(_user, value, _origin);
        if (validate(_user, value, currentBlockNumber))
        {  // grant committee member
            auto entry = acTable->newEntry();
            entry->setField(SYS_AC_TABLE_NAME, SYS_ACCESS_TABLE);
            entry->setField(SYS_AC_ADDRESS, _user);
            entry->setField(SYS_AC_ENABLENUM, to_string(currentBlockNumber));
            int count = acTable->insert(
                SYS_ACCESS_TABLE, entry, make_shared<AccessOptions>(Address(), false));

            // write weight
            entry = committeeTable->newEntry();
            entry->setField(CGP_COMMITTEE_TABLE_VALUE, to_string(1));
            entry->setField(CGP_COMMITTEE_TABLE_ORIGIN, _origin);
            committeeTable->insert(
                _user + CGP_WEIGTH_SUFFIX, entry, make_shared<AccessOptions>(Address(), false));
            deleteUsedVotes(_user, value);
            return count;
        }
        break;
    }
    case RevokeCommitteeMember:
    {
        auto value = "revoke";
        recordVote(_user, value, _origin);
        if (validate(_user, value, currentBlockNumber))
        {  // grant committee member
            auto condition = committeeTable->newCondition();
            condition->EQ(SYS_AC_TABLE_NAME, SYS_ACCESS_TABLE);
            condition->EQ(SYS_AC_ADDRESS, _user);
            int count = acTable->remove(
                SYS_ACCESS_TABLE, condition, make_shared<AccessOptions>(Address(), false));
            committeeTable->remove(_user + CGP_WEIGTH_SUFFIX, committeeTable->newCondition(),
                make_shared<AccessOptions>(Address(), false));
            deleteUsedVotes(_user, value);
            return count;
        }
        break;
    }
    case UpdateCommitteeMemberWeight:
    {
        auto key = _user + CGP_UPDATE_WEIGTH_SUFFIX;
        auto& value = _value;
        recordVote(key, value, _origin);
        if (validate(key, value, currentBlockNumber))
        {  // write member weight
            auto entry = committeeTable->newEntry();
            entry->setField(CGP_COMMITTEE_TABLE_VALUE, value);
            entry->setField(CGP_COMMITTEE_TABLE_ORIGIN, _origin);
            int count = committeeTable->update(_user + CGP_WEIGTH_SUFFIX, entry,
                committeeTable->newCondition(), make_shared<AccessOptions>(Address(), false));
            deleteUsedVotes(key, value);
            return count;
        }
        break;
    }
    case UpdateThreshold:
    {
        auto key = CGP_UPDATE_AUTH_THRESHOLD;
        auto& value = _value;
        recordVote(key, value, _origin);
        if (validate(key, value, currentBlockNumber))
        {  // write member weight
            auto entry = committeeTable->newEntry();
            entry->setField(CGP_COMMITTEE_TABLE_VALUE, _value);
            entry->setField(CGP_COMMITTEE_TABLE_ORIGIN, _origin);
            int count = committeeTable->update(CGP_AUTH_THRESHOLD, entry,
                committeeTable->newCondition(), make_shared<AccessOptions>(Address(), false));
            deleteUsedVotes(_user + CGP_WEIGTH_SUFFIX, value);
            return count;
        }
        break;
    }
    default:
        CHAIN_GOVERNANCE_LOG(INFO) << LOG_DESC("undified operation");
    }
    return 0;
}

string ChainGovernancePrecompiled::queryCommitteeMembers(
    shared_ptr<dev::blockverifier::ExecutiveContext> _context, const string& tableName)
{
    Table::Ptr table = openTable(_context, SYS_ACCESS_TABLE);
    auto condition = table->newCondition();
    auto entries = table->select(tableName, condition);
    Json::Value AuthorityInfos(Json::arrayValue);
    if (entries)
    {
        for (size_t i = 0; i < entries->size(); i++)
        {
            auto entry = entries->get(i);
            if (!entry)
                continue;
            Json::Value AuthorityInfo;
            AuthorityInfo[SYS_AC_ADDRESS] = "0x" + entry->getField(SYS_AC_ADDRESS);
            AuthorityInfo[SYS_AC_ENABLENUM] = entry->getField(SYS_AC_ENABLENUM);
            AuthorityInfos.append(AuthorityInfo);
        }
    }
    Json::FastWriter fastWriter;
    return fastWriter.write(AuthorityInfos);
}
