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
/** @file test_WedprPrecompiled.cpp
 *  @author caryliao
 *  @date 20191009
 */

#include "../libstorage/MemoryStorage.h"
#include "Common.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/extension/WedprPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_WedprPrecompiled
{
class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};
};
struct WedprPrecompiledFixture
{
    WedprPrecompiledFixture()
    {
        context = std::make_shared<ExecutiveContext>();
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context->setBlockInfo(blockInfo);

        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        ExecutiveContextFactory factory;
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();

        tableFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        wedprPrecompiled = context->getPrecompiled(Address(0x5018));
    }

    ~WedprPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr wedprPrecompiled;
    dev::blockverifier::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(WedprPrecompiled, WedprPrecompiledFixture)

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyIssuedCredit)
{
    dev::eth::ContractABI abi;
    std::string issue_argument_pb =
        "CtUCCrQBClhCSElnNjFCMXA5SU4zSmk2aHJOYnNGbnV0YUVUNDMyNnBOREVaVTUvdmFNR0NhdTFydE4ydFByRG01Sz"
        "hCUElYR011VUd5U1hFRkNPcFVNc1ZwWHZydXM9ElhvY2xoSU9JQ0VyV2FJbUErZ1dPdHcyV1BqNUZKSUxiNnd6RGZR"
        "amxObTF4d2hpUytBYjA5YnZYM25BMktQOC9UY1JpMFJTcFFYUUxiNGFia2NoWWJPQT09Egg1ZDllYTI4MhpICiw5Qm"
        "ZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5L2RRSTJvUVlrOVRBWVZjdz09"
        "IkgKLDlCZkpUYWo0aC9PaWNzbk0wY0dBY2U4Y01JUXJVdG1kZlpFWldhQ1hJUW89EhhwYVlMS3kvZFFJMm9RWWs5VE"
        "FZVmN3PT0SLHRMbVBuK1ZMWEsrTW5NZ3Q4L0Rsei9DNEc4bmMydVROdnlwUWdXRHcyeVk9GgIIZA==";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT, issue_argument_pb);
    // hiddenAssetVerifyIssuedCredit(bytes issue_argument_pb)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string current_credit;
    std::string credit_storage;

    abi.abiOut(&out, current_credit, credit_storage);
    BOOST_TEST(current_credit ==
               "Ciw5QmZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5L2RRSTJvUVl"
               "rOVRBWVZjdz09");
    BOOST_TEST(credit_storage ==
               "CrQBClhCSElnNjFCMXA5SU4zSmk2aHJOYnNGbnV0YUVUNDMyNnBOREVaVTUvdmFNR0NhdTFydE4ydFByRG0"
               "1SzhCUElYR011VUd5U1hFRkNPcFVNc1ZwWHZydXM9ElhvY2xoSU9JQ0VyV2FJbUErZ1dPdHcyV1BqNUZKSU"
               "xiNnd6RGZRamxObTF4d2hpUytBYjA5YnZYM25BMktQOC9UY1JpMFJTcFFYUUxiNGFia2NoWWJPQT09Egg1Z"
               "DllYTI4MhpICiw5QmZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5"
               "L2RRSTJvUVlrOVRBWVZjdz09IkgKLDlCZkpUYWo0aC9PaWNzbk0wY0dBY2U4Y01JUXJVdG1kZlpFWldhQ1h"
               "JUW89EhhwYVlMS3kvZFFJMm9RWWs5VEFZVmN3PT0=");

    std::string error_issue_argument_pb = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT, error_issue_argument_pb);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyFulfilledCredit)
{
    dev::eth::ContractABI abi;
    std::string fulfill_argument_pb =
        "CtUCCrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFpDWl"
        "BkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydXpuV3BPWHZR"
        "SGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1ZDllYjU3MxpICix5S2"
        "tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXBpYnlLQVlVUT09"
        "IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s4aDg9EhhJL253RklTYVQ5U2liWm1BL2"
        "hBaXlRPT0SjwErM3gzTDZDWFR2Qm1OeFFZM2drV1hRaXJHM21aMm1ocjE3cmxYZkh0dHdZPXwrM3NiRUkwdGd4MVFP"
        "RWVvSDRTNUdBTkh0NjlLNlNMRS9Pc2QxWFZLTHdNPXxPUG5nc05OUUhtbzN1YkpBbnVMY0NpcnkxT2VzOXFDUTlMZW"
        "8vRzJDamdNPXw1ZDllYjU3Mw==";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT, fulfill_argument_pb);
    // hiddenAssetVerifyFulfilledCredit(bytes fulfill_argument_pb)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string current_credit;
    std::string credit_storage;

    abi.abiOut(&out, current_credit, credit_storage);
    BOOST_TEST(current_credit ==
               "Cix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXB"
               "pYnlLQVlVUT09");
    BOOST_TEST(credit_storage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNK"
               "QUhSL0dENXBpYnlLQVlVUT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");

    std::string error_fulfill_argument_pb = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT, error_fulfill_argument_pb);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyTransferredCredit)
{
    dev::eth::ContractABI abi;
    std::string transfer_request_pb =
        "CtUCCrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFpDWl"
        "BkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydXpuV3BPWHZR"
        "SGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1ZDllYjU3MxpICixjTW"
        "ovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJU2FUOVNpYlptQS9oQWl5UT09"
        "IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s4aDg9EhhJL253RklTYVQ5U2liWm1BL2"
        "hBaXlRPT0S1QIKtAEKWEJBOVJPV1VaRWIvdi9wbGYzdzRReGlXOTk5ZkZTQzFYc0NhV0lUQ3NiU2ZXelpITm9nRkhW"
        "N0NsWkNaUGRqQXZ4ZkdzVWFsSFY4cFZCRENWZ3ZzbG81UT0SWDBEZFlyaTdwdU9KcHBpUXdWVDZjTDdYZTFwaDJ1em"
        "5XcE9YdlFIZVhaSUZ5ZlR2R29RdTNKUXFIWS9XU2dxeVFOOUNpY0duL055cWdLeW1lR0NvS2V3PT0SCDVkOWViNTcz"
        "GkgKLHlLa1AwYWcrZFJad3d1dkR1S3FqVURLcVBaeEZIS05uMnU2RHE0RTB6RTA9EhhDRDhqU0pBSFIvR0Q1cGlieU"
        "tBWVVRPT0iSAosY01qL0lzakNiV0FVdUtHR3JWVktqYXl4NG1BT1BGdmdQMlR4VWRzSzhoOD0SGEkvbndGSVNhVDlT"
        "aWJabUEvaEFpeVE9PRr4AgosZGtXRDlXcnpVek9aVmhlN29NQXBxWTM2ZnVQMEFoenlwcWhndFFwL0IwUT0SLFdHSE"
        "kvZDhwTGRObGF6NEJXWFlxWW5Ba0s2eW5jMk80dW5kSWVvZnBlQjA9GixUaDRjNWlKU0VKQ1NnL2h1UTRiVlZ2STA0"
        "aEV0ZTFUZmowZmhHek83WmtjPSIsWUVWTVF0YjkycUZ1Vk5lajM2Q2p4Q1NYRVB1YXQrQkp6Y1dCejh0UU1nST0qLC"
        "9zNUVjcm5GeGk4NUVCcmp5SjVaQS9JMDRoRXRlMVRmajBmaEd6TzdaZ2M9Oo8BUUljVC9Fb21VWFBzeUdYaFZIYVZI"
        "alhTMmxkbW9ubTFJMXpkRmpPaTNRbz18Z3Jma3VsQTU1RHJPT1IrdE9PdDBKTEs5UlM1WnVGREFQZ3ZVeHEvd1lRUT"
        "18VllWVGQ3QU9SU3Bic0o3RzJ2Y1VTbUZrMnVKeFZTWmgzWGo0eE5YcG1Bbz18NWQ5ZWI1NzMiCHRyYW5zZmVy";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT, transfer_request_pb);
    // hiddenAssetVerifyTransferredCredit(bytes transfer_request_pb)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string spent_current_credit;
    std::string spent_credit_storage;
    std::string new_current_credit;
    std::string new_credit_storage;

    abi.abiOut(
        &out, spent_current_credit, spent_credit_storage, new_current_credit, new_credit_storage);
    BOOST_TEST(spent_current_credit ==
               "CixjTWovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJU2FUOVNpYlp"
               "tQS9oQWl5UT09");
    BOOST_TEST(spent_credit_storage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICixjTWovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJ"
               "U2FUOVNpYlptQS9oQWl5UT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");
    BOOST_TEST(new_current_credit ==
               "Cix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXB"
               "pYnlLQVlVUT09");
    BOOST_TEST(new_credit_storage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNK"
               "QUhSL0dENXBpYnlLQVlVUT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");

    std::string error_transfer_request_pb = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT, error_transfer_request_pb);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifySplitCredit)
{
    dev::eth::ContractABI abi;
    std::string split_request_pb =
        "CtUCCrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR2Q2"
        "ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMMExTSmJJV2RQ"
        "TkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1ZDllYjhkMhpICiwxaj"
        "cxbjZ0dWo0RFpyR1BMVmdrb1ZqaitEdTgwQ2lPbVlOMDQ3U29Rc1VnPRIYaUhicmdIQ3hSWU9zTncvOUtFNFRJQT09"
        "IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1FzVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0"
        "U0VElBPT0S1QIKtAEKWEJDVWRPQWlvM1Q1Rm04a0pjZlVibUY0R3RUMHpMRG1TdWJFUjlyMGhzT1Q0TEpzUWwrN1Rn"
        "VmlpZHZDZlYybFVFMUpDQVJNc2xMRHJIMUIzdnNsSmo5cz0SWFF3UkR3M3d1UEJPYXhrRzl4NVJHREVyWDFRUEwwTF"
        "NKYklXZFBOSlEwbDVwVHdYSE5qUzdhZkx6ZklYWnB4eTc0aEtUN0NMUFBMUUluWm0ycWFTcllnPT0SCDVkOWViOGQz"
        "GkgKLEFzZUJhcXdoMDZOQktFYWgxWFVGVmxENmZvNzlEQVM5cGJvb3ExNHYyaW89EhhoU1lXRXFpQVR1YTdYSGdiUF"
        "BoYVRnPT0iSAosMWo3MW42dHVqNERackdQTFZna29WamorRHU4MENpT21ZTjA0N1NvUXNVZz0SGGlIYnJnSEN4UllP"
        "c053LzlLRTRUSUE9PRLVAgq0AQpYQkNVZE9BaW8zVDVGbThrSmNmVWJtRjRHdFQwekxEbVN1YkVSOXIwaHNPVDRMSn"
        "NRbCs3VGdWaWlkdkNmVjJsVUUxSkNBUk1zbExEckgxQjN2c2xKajlzPRJYUXdSRHczd3VQQk9heGtHOXg1UkdERXJY"
        "MVFQTDBMU0piSVdkUE5KUTBsNXBUd1hITmpTN2FmTHpmSVhacHh5NzRoS1Q3Q0xQUExRSW5abTJxYVNyWWc9PRIINW"
        "Q5ZWI4ZDMaSAosMkkrbzVJMkJXUjJleWZaZTc3d3pVakpyWGVDN2JETWpVa2VjUk42L0JnWT0SGEVrL2dqa1I2U0Ft"
        "R2xudFdDanh4dmc9PSJICiwxajcxbjZ0dWo0RFpyR1BMVmdrb1ZqaitEdTgwQ2lPbVlOMDQ3U29Rc1VnPRIYaUhicm"
        "dIQ3hSWU9zTncvOUtFNFRJQT09Gt8SCp8JCixUaEJpK3FIeDBmNGdsampwb3JNZFMycjB1blpPQzgzbEN6dktVb3BB"
        "U0FvPRIsUkdiU1JQSXBpeEhZeHp4eVpkZUpNdnB2VjVUbWhNeEVMK2h4akw1V1FUND0iLDBjZ1JpODZ0b1VmTkhOT2"
        "1pYnAvcnNCamE2QytnUmQzVUtxVGMzKzVqUUU9MoAHWnBDc3hxOWFLWDRZTC8zNjNoSHVpM3FxM0VJdzFoV3duWkY5"
        "T25jT3huK3E5WmhRUnFIL2txOEd3a3ZONXhXa09mOTJuTjNCWVBaeVZ3bU9WblV6Y1NRUStFYnlTeXFLUVFabmxYWm"
        "FPd240N0liOWViSTh2dkszRVFqb1lGQTBEbXQrbUpOWXNDTFd5cW1Xem50ZzR3cDJ1elRSNW9OdGxWdER2UFRCcURT"
        "QnFWcC9ZczBkZEZvUmh5dFM2QWRJYlR1eTJuNzdUeXcxUzRzRXE0Q1lBMjhUbnRuR2xOb2txTWZrZmRHZmh1REVtTz"
        "YzQVZmTEV1UmRsTXJldGNFQzBiVEFaQUFjOGRqY3BQMmx1UVdzbkowY2ZkVFJGWU12eWo5VVN0aXN4d0dtVGdpSmdm"
        "cnlDWDJOZTdIVmFZNURISVBKQVhUWloyMGkwaTUvNWFhR1Nsci93Q2tQWWhxMlpVRGhENS8xdzNjMXFvLzg1eDNMbm"
        "pQVyt5VlNseHA0Smc1a3VBSEpkVEE0RTdPZ0crWjhxS3pBdnVKN1cxemFuSHR3VXRDbVpUazRicmgvN1dUV0Zld2hE"
        "NThoVjRjbVFhYlhPdXBrUVpMRzBrUER0TnlyVjlhMjFZb01CcndMWWZ4UjN2U1ZEQVQwM0hXcVhZUlp3eC80cGZXZm"
        "JiRWQwQzlGM05PL2N2a2I3NENqL2JaRjQvTkMxRjBjSDNHc1JMRzQ5eEJkeUUzYVpHeEIrbG5BTWxkdkkxTG5FWlNU"
        "cGZtSGRqUXlKUU9zUU5xOFBTbzFVdHpMQTdjZ2t1MDFvQjhBeFVWRGphUlNna0R4SlpuWnU2dWovcFRXQ3JrWGZPZT"
        "lmOFZ4elYxRlZDYmEwMEVnOVZ2WmpTT0VPYXBla0JFWDFYV2dxeHVFRE1JdTVkWEttdGd0cDAyMTdhd3hKWFNMUTZG"
        "cS83YmZkemsvQ3M3dWFXVDNGZm9zTmNLS2tyWndySGllUElTazdjSy80MTZjanV2ZzJkVWtFdk54YklmRG01MmhBY2"
        "tYSmVkejV1Q3p3aVI2SERPTHBwQkMwNU0vVERwenRGOWkyUmNXMkJpVzhEckZrdkFXOWc5NkZ0bXFoZkNoalMyZEk2"
        "KyttaldpQlVQakJPT2lxYWpVdDY0aGJLMXFLOHY2TzFYMUIyODVMNWREbXhpTnVyOEU6jwFyL3RNcW9WWDVBQ0w3Rk"
        "o3MzlXWm1YVmh5QlVxZWRzVFZmOWRuODV4ZFEwPXxKYUdzQmQ3U3pkTWg5eC8xVmpiQy83SDZyZE1YZzFGQy9OOHE0"
        "TWNEcGcwPXw3dHpBbzBzcDdnekI0b1MzcW5uTXFIRGEvQzM0MnB3d0NINGNzUzJ4eWd3PXw1ZDllYjhkMhKzCQosWU"
        "ZkcEhtYlMyakw0RmRwZkFNUm1BOWpYejB6V05sRTcrYnJERW5QNlBqUT0SLHBDcWVoaXBWanNRN2phWVhrbXU3QVBm"
        "V0R5eE1kVUNBOVNvRzlwa2wxeXc9GixHTDZsUnQ4SVhwMHkzQ3YxWFpVeE5xSURsS1E0a0J3eE44RW1LS0hnMTFjPS"
        "IsdlV4SXBBVE9leUtnZDRuQXVvZElHV3owN0VFazM0bjNDRDZLaG9RcGpBRT0qLHM1cllkVnNaQXVVQ3pGWEdCTFRX"
        "emFFRGxLUTRrQnd4TjhFbUtLSGcxd2M9MoAHNmp2SmNtTHhqdkU3SHZKWjdsM3hKRHZPZ0RXVFY1NC9LMGN1ditKL1"
        "FCZFkvS2hTTTREYzFxMWFWNW9EVUQ4L0k1Q1lPbWZrNytJMTVpQzBPc0ZGWmRSQVcrclBvaEhHRXNaYjVDNTQzNmJI"
        "WUhCUktzd1pST0d2SHJrK2JxeGRuUGVxRHBWTm1XOHFuRzB5TjlscHFzNFdSNmFKY2VPWmQ3MEtLNmMra21URUpyUm"
        "VOQThlNmEyRis3WjcrYzQ2VFo1VkQ2TTMxeEFjUWhlV1M3S2JCS1dmTVQ2OStaS1hCS2pKT2Z4Nlp1UjVJaG5YRjRW"
        "U3dsTUVPUXVyRG5NR2ErdUgzR3I5UkVxSjE0Yis1RldJRXc1UW9sSHJpdEsxNlN5ZjIxZ0IwUXpzU1FFM0NudkQ3VW"
        "w3eVdJNmVpdXZoUS9mTmw2eGc1cGV2TTFEMTRoTEtOS2k0Mjd6Z1lYK1BXTXZ3bGYyMG50QVF4UGFPd1hobEZwYkVG"
        "MUs0eUZzdG5nb0JlNTBZaTBTNzFQZnlja3JTWTZWZit5MFZkdHZwM0ZuMHZWbDJGRVV4TE0zODZ2VVBLQW9QZ1MraD"
        "RoQVBkYjRLVU13YVIvN0lxam5jeGlpS3l6dWJzVEdVZnhFZFZOMzBZRG5jYmY2QmtrQWZPMUFJcFpid1ZzYVArUUFN"
        "TytuNUZVZ0VJV3RvK0lhbmU3SkQ0Z3Y4WHpLSUxEdzRBSEhoM1AyVVd3VU11ZFgxbFhuT2hSK3h2eGpVL3ovcU9WeD"
        "RMOCtuRzhiMkp6MkdkWmpBMXd6ZUhkYjlEUUI5RnFGZmxsWDRkVlFVY0xyY3B1YzlGUTlyVWUyYng0MEFJUXZXYlUy"
        "bnovMzZtZGloVnFUd3lTcjRIakhNV2tFcGNLRWJwVXRYR1ZRcDc5V1p3Z2RRUHRTSUVvTnZWRlNLS3NnNHRQcWladj"
        "BpenhBeWVDVUNucnVTM1hnYU41YnhoNUJGRXNCSmMwSWdlVE0zMkR2bURob2JPUVNDL3huNXVnZkc0NUEwVlA3VFVm"
        "enJOamtmVjFPL3lxNVlVT2p2TkNSSUdDQTRScUdGS0tYbUU4VU1HbHduQnVnczkwK0xkeWRVeHArRDNXaktYa2U5OW"
        "U5QW1LUE5tbHdla1dwSUNlQjRxanlCNysrNVMwMTRmQjF3NEx6Si9uRFFBTUZCSAosQXNlQmFxd2gwNk5CS0VhaDFY"
        "VUZWbEQ2Zm83OURBUzlwYm9vcTE0djJpbz0SGGhTWVdFcWlBVHVhN1hIZ2JQUGhhVGc9PRoFc3BsaXQ=";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT, split_request_pb);
    // hiddenAssetVerifySplitCredit(bytes split_request_pb)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string spent_current_credit;
    std::string spent_credit_storage;
    std::string new_current_credit1;
    std::string new_credit_storage1;
    std::string new_current_credit2;
    std::string new_credit_storage2;

    abi.abiOut(&out, spent_current_credit, spent_credit_storage, new_current_credit1,
        new_credit_storage1, new_current_credit2, new_credit_storage2);
    BOOST_TEST(spent_current_credit ==
               "CiwxajcxbjZ0dWo0RFpyR1BMVmdrb1ZqaitEdTgwQ2lPbVlOMDQ3U29Rc1VnPRIYaUhicmdIQ3hSWU9zTnc"
               "vOUtFNFRJQT09");
    BOOST_TEST(spent_credit_storage ==
               "CrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR"
               "2Q2ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMME"
               "xTSmJJV2RQTkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1Z"
               "DllYjhkMhpICiwxajcxbjZ0dWo0RFpyR1BMVmdrb1ZqaitEdTgwQ2lPbVlOMDQ3U29Rc1VnPRIYaUhicmdI"
               "Q3hSWU9zTncvOUtFNFRJQT09IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1F"
               "zVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0U0VElBPT0=");
    BOOST_TEST(new_current_credit1 ==
               "CixBc2VCYXF3aDA2TkJLRWFoMVhVRlZsRDZmbzc5REFTOXBib29xMTR2MmlvPRIYaFNZV0VxaUFUdWE3WEh"
               "nYlBQaGFUZz09");
    BOOST_TEST(new_credit_storage1 ==
               "CrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR"
               "2Q2ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMME"
               "xTSmJJV2RQTkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1Z"
               "DllYjhkMxpICixBc2VCYXF3aDA2TkJLRWFoMVhVRlZsRDZmbzc5REFTOXBib29xMTR2MmlvPRIYaFNZV0Vx"
               "aUFUdWE3WEhnYlBQaGFUZz09IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1F"
               "zVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0U0VElBPT0=");
    BOOST_TEST(new_current_credit2 ==
               "CiwySStvNUkyQldSMmV5ZlplNzd3elVqSnJYZUM3YkRNalVrZWNSTjYvQmdZPRIYRWsvZ2prUjZTQW1HbG5"
               "0V0NqeHh2Zz09");
    BOOST_TEST(new_credit_storage2 ==
               "CrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR"
               "2Q2ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMME"
               "xTSmJJV2RQTkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1Z"
               "DllYjhkMxpICiwySStvNUkyQldSMmV5ZlplNzd3elVqSnJYZUM3YkRNalVrZWNSTjYvQmdZPRIYRWsvZ2pr"
               "UjZTQW1HbG50V0NqeHh2Zz09IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1F"
               "zVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0U0VElBPT0=");

    std::string error_split_request_pb = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT, error_split_request_pb);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(unKnownFunc)
{
    // function not exist
    dev::eth::ContractABI abi;
    std::string split_request_pb = "123";
    std::string errorFunc = "errorFunc(string)";
    bytes param = abi.abiIn(errorFunc, split_request_pb);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(wedprPrecompiled->toString() == WEDPR_PRECOMPILED);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_WedprPrecompiled
