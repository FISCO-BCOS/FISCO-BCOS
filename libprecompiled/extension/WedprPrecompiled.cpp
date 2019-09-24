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
#include "libprecompiled/Common.h"
#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/StorageException.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include "wedpr-generated/asset_hiding/asset_hiding.h"
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace std;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const string API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT = "hiddenAssetVerifyIssuedCredit(bytes)";
const string API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT = "hiddenAssetVerifyFulfilledCredit(bytes)";
const string API_HIDDEN_ASSET_VERIFY_TRANSFER_CREDIT = "hiddenAssetVerifyTransferCredit(bytes)";
const string API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT = "hiddenAssetVerifySplitCredit(bytes)";
const string VERFIY_FAILED = "verfiy failed";
const int SUCCESS = 0;
const int FAILURE = -1;

const string PRECOMPILED_NAME = "WedprPrecompiled";

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
    return PRECOMPILED_NAME;
}

bytes WedprPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE(PRECOMPILED_NAME) << LOG_DESC("call")
                           << LOG_KV("param", toHex(param)) << LOG_KV("origin", origin)
                           << LOG_KV("context", context->txGasLimit());

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    // hiddenAssetVerifyIssuedCredit(bytes issue_argument_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT])
    {
        // parse parameter
        std::string issue_argument_pb;
        abi.abiOut(data, issue_argument_pb);

        // verify issued credit
        if (verify_issued_credit(string_to_char(issue_argument_pb)) == FAILURE)
        {
            logError(PRECOMPILED_NAME, "verify_issued_credit", VERFIY_FAILED);
            throwException(VERFIY_FAILED);
        }

        std::string current_credit;
        std::string credit_storage;

        // return current_credit and credit_storage
        out = abi.abiIn("", current_credit, credit_storage);
    }
    // hiddenAssetVerifyFulfilledCredit(bytes fulfill_argument_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_TRANSFER_CREDIT])
    {
        std::string fulfill_argument_pb;
        abi.abiOut(data, fulfill_argument_pb);

        if (verify_fulfilled_credit(string_to_char(fulfill_argument_pb)) == FAILURE)
        {
            logError(PRECOMPILED_NAME, "verify_fulfilled_credit", VERFIY_FAILED);
            throwException(VERFIY_FAILED);
        }

        std::string current_credit;
        std::string credit_storage;

        // return current_credit and credit_storage
        out = abi.abiIn("", current_credit, credit_storage);
    }
    // hiddenAssetVerifyTransferCredit(bytes transfer_request_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT])
    {
        std::string transfer_request_pb;
        abi.abiOut(data, transfer_request_pb);

        if (verify_transfer_credit(string_to_char(transfer_request_pb)) == FAILURE)
        {
            logError(PRECOMPILED_NAME, "verify_transfer_credit", VERFIY_FAILED);
            throwException(VERFIY_FAILED);
        }

        std::string spent_current_credit;
        std::string spent_credit_storage;
        std::string new_current_credit;
        std::string new_credit_storage;

        // return current_credit and credit_storage
        out = abi.abiIn(
            "", spent_current_credit, spent_credit_storage, new_current_credit, new_credit_storage);
    }
    // hiddenAssetVerifySplitCredit(bytes split_request_pb)
    if (func == name2Selector[API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT])
    {
        std::string split_request_pb;
        abi.abiOut(data, split_request_pb);

        if (verify_split_credit(string_to_char(split_request_pb)) == FAILURE)
        {
            logError(PRECOMPILED_NAME, "verify_split_credit", VERFIY_FAILED);
            throwException(VERFIY_FAILED);
        }

        std::string spent_current_credit;
        std::string spent_credit_storage;
        std::string new_current_credit1;
        std::string new_credit_storage1;
        std::string new_current_credit2;
        std::string new_credit_storage2;

        // return current_credit and credit_storage
        out = abi.abiIn("", spent_current_credit, spent_credit_storage, new_current_credit1,
            new_credit_storage1, new_current_credit2, new_credit_storage2);
    }
    else
    {
        // unknown function call
        logError(PRECOMPILED_NAME, "unknown func", "func", std::to_string(func));
        throwException("unknown func");
    }
    return out;
}
