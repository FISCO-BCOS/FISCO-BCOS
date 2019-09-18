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
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file WedprPrecompiled.cpp
 *  @author caryliao
 *  @date 20190917
 */

#include "WedprPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/StorageException.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace std;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const string API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT =
    "hidden_asset_verify_issued_credit(bytes issue_argument_pb)";
const string API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT =
    "hidden_asset_verify_fulfilled_credit(bytes fulfill_argument_pb)";
const string API_HIDDEN_ASSET_VERIFY_TRANSFER_CREDIT =
    "hidden_asset_verify_transfer_credit(bytes transfer_request_pb)";
const string API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT =
    "hidden_asset_verify_split_credit(bytes split_request_pb)";

const string CONTRACT_NAME = "WedprPrecompiled";

WedprPrecompiled::WedprPrecompiled()
{
    name2Selector[API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT);
    name2Selector[API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT);
    name2Selector[API_HIDDEN_ASSET_VERIFY_TRANSFER_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_TRANSFER_CREDIT);
    name2Selector[API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT] =
        getFuncSelector(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT);
}

std::string WedprPrecompiled::toString()
{
    return CONTRACT_NAME;
}

void logError(const std::string& _op, const std::string& _msg)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(CONTRACT_NAME) << LOG_DESC(_op) << ": " << LOG_DESC(_msg);
}

void logError(const std::string& _op, const std::string& _key, const std::string& _value)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(CONTRACT_NAME) << LOG_DESC(_op) << LOG_KV(_key, _value);
}

void throwException(const std::string& msg)
{
    BOOST_THROW_EXCEPTION(dev::eth::TransactionRefused() << errinfo_comment(msg));
}

bytes WedprPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE(CONTRACT_NAME) << LOG_DESC("call")
                           << LOG_KV("param", toHex(param)) << LOG_KV("origin", origin)
                           << LOG_KV("context", context->txGasLimit());

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    // hidden_asset_verify_issued_credit(bytes issue_argument_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT])
    {
        std::string issue_argument_pb;
        abi.abiOut(data, issue_argument_pb);
        // TODO call rust lib
    }
    // hidden_asset_verify_fulfilled_credit(bytes fulfill_argument_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_TRANSFER_CREDIT])
    {
        std::string fulfill_argument_pb;
        abi.abiOut(data, fulfill_argument_pb);
        // TODO call rust lib
    }
    // hidden_asset_verify_transfer_credit(bytes transfer_request_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT])
    {
        std::string transfer_request_pb;
        abi.abiOut(data, transfer_request_pb);
        // TODO call rust lib
    }
    // hidden_asset_verify_split_credit(bytes split_request_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT])
    {
        std::string split_request_pb;
        abi.abiOut(data, split_request_pb);
        // TODO call rust lib
    }
    else
    {
        // unknown function call
        logError("unknown func", "func", std::to_string(func));
        throwException("unknown func");
    }
    return out;
}
