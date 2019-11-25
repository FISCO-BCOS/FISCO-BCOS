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
    std::string issueArgument =
        "CtUCCrQBClhCSElnNjFCMXA5SU4zSmk2aHJOYnNGbnV0YUVUNDMyNnBOREVaVTUvdmFNR0NhdTFydE4ydFByRG01Sz"
        "hCUElYR011VUd5U1hFRkNPcFVNc1ZwWHZydXM9ElhvY2xoSU9JQ0VyV2FJbUErZ1dPdHcyV1BqNUZKSUxiNnd6RGZR"
        "amxObTF4d2hpUytBYjA5YnZYM25BMktQOC9UY1JpMFJTcFFYUUxiNGFia2NoWWJPQT09Egg1ZDllYTI4MhpICiw5Qm"
        "ZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5L2RRSTJvUVlrOVRBWVZjdz09"
        "IkgKLDlCZkpUYWo0aC9PaWNzbk0wY0dBY2U4Y01JUXJVdG1kZlpFWldhQ1hJUW89EhhwYVlMS3kvZFFJMm9RWWs5VE"
        "FZVmN3PT0SLHRMbVBuK1ZMWEsrTW5NZ3Q4L0Rsei9DNEc4bmMydVROdnlwUWdXRHcyeVk9GgIIZA==";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT, issueArgument);
    // hiddenAssetVerifyIssuedCredit(string issueArgument)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string currentCredit;
    std::string creditStorage;

    abi.abiOut(&out, currentCredit, creditStorage);
    BOOST_TEST(currentCredit ==
               "Ciw5QmZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5L2RRSTJvUVl"
               "rOVRBWVZjdz09");
    BOOST_TEST(creditStorage ==
               "CrQBClhCSElnNjFCMXA5SU4zSmk2aHJOYnNGbnV0YUVUNDMyNnBOREVaVTUvdmFNR0NhdTFydE4ydFByRG0"
               "1SzhCUElYR011VUd5U1hFRkNPcFVNc1ZwWHZydXM9ElhvY2xoSU9JQ0VyV2FJbUErZ1dPdHcyV1BqNUZKSU"
               "xiNnd6RGZRamxObTF4d2hpUytBYjA5YnZYM25BMktQOC9UY1JpMFJTcFFYUUxiNGFia2NoWWJPQT09Egg1Z"
               "DllYTI4MhpICiw5QmZKVGFqNGgvT2ljc25NMGNHQWNlOGNNSVFyVXRtZGZaRVpXYUNYSVFvPRIYcGFZTEt5"
               "L2RRSTJvUVlrOVRBWVZjdz09IkgKLDlCZkpUYWo0aC9PaWNzbk0wY0dBY2U4Y01JUXJVdG1kZlpFWldhQ1h"
               "JUW89EhhwYVlMS3kvZFFJMm9RWWs5VEFZVmN3PT0=");

    std::string errorIssueArgument = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_ISSUED_CREDIT, errorIssueArgument);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyFulfilledCredit)
{
    dev::eth::ContractABI abi;
    std::string fulfillArgument =
        "CtUCCrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFpDWl"
        "BkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydXpuV3BPWHZR"
        "SGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1ZDllYjU3MxpICix5S2"
        "tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXBpYnlLQVlVUT09"
        "IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s4aDg9EhhJL253RklTYVQ5U2liWm1BL2"
        "hBaXlRPT0SjwErM3gzTDZDWFR2Qm1OeFFZM2drV1hRaXJHM21aMm1ocjE3cmxYZkh0dHdZPXwrM3NiRUkwdGd4MVFP"
        "RWVvSDRTNUdBTkh0NjlLNlNMRS9Pc2QxWFZLTHdNPXxPUG5nc05OUUhtbzN1YkpBbnVMY0NpcnkxT2VzOXFDUTlMZW"
        "8vRzJDamdNPXw1ZDllYjU3Mw==";
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT, fulfillArgument);
    // hiddenAssetVerifyFulfilledCredit(string fulfillArgument)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string currentCredit;
    std::string creditStorage;

    abi.abiOut(&out, currentCredit, creditStorage);
    BOOST_TEST(currentCredit ==
               "Cix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXB"
               "pYnlLQVlVUT09");
    BOOST_TEST(creditStorage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNK"
               "QUhSL0dENXBpYnlLQVlVUT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");

    std::string errorFulfillArgument = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_FULFILLED_CREDIT, errorFulfillArgument);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifyTransferredCredit)
{
    dev::eth::ContractABI abi;
    std::string transferRequest =
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
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT, transferRequest);
    // hiddenAssetVerifyTransferredCredit(string transferRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string spentCurrentCredit;
    std::string spentCreditStorage;
    std::string newCurrentCredit;
    std::string newCreditStorage;

    abi.abiOut(&out, spentCurrentCredit, spentCreditStorage, newCurrentCredit, newCreditStorage);
    BOOST_TEST(spentCurrentCredit ==
               "CixjTWovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJU2FUOVNpYlp"
               "tQS9oQWl5UT09");
    BOOST_TEST(spentCreditStorage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICixjTWovSXNqQ2JXQVV1S0dHclZWS2pheXg0bUFPUEZ2Z1AyVHhVZHNLOGg4PRIYSS9ud0ZJ"
               "U2FUOVNpYlptQS9oQWl5UT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");
    BOOST_TEST(newCurrentCredit ==
               "Cix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNKQUhSL0dENXB"
               "pYnlLQVlVUT09");
    BOOST_TEST(newCreditStorage ==
               "CrQBClhCQTlST1dVWkViL3YvcGxmM3c0UXhpVzk5OWZGU0MxWHNDYVdJVENzYlNmV3paSE5vZ0ZIVjdDbFp"
               "DWlBkakF2eGZHc1VhbEhWOHBWQkRDVmd2c2xvNVE9ElgwRGRZcmk3cHVPSnBwaVF3VlQ2Y0w3WGUxcGgydX"
               "puV3BPWHZRSGVYWklGeWZUdkdvUXUzSlFxSFkvV1NncXlRTjlDaWNHbi9OeXFnS3ltZUdDb0tldz09Egg1Z"
               "DllYjU3MxpICix5S2tQMGFnK2RSWnd3dXZEdUtxalVES3FQWnhGSEtObjJ1NkRxNEUwekUwPRIYQ0Q4alNK"
               "QUhSL0dENXBpYnlLQVlVUT09IkgKLGNNai9Jc2pDYldBVXVLR0dyVlZLamF5eDRtQU9QRnZnUDJUeFVkc0s"
               "4aDg9EhhJL253RklTYVQ5U2liWm1BL2hBaXlRPT0=");

    std::string errorTransferRequest = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_TRANSFERRED_CREDIT, errorTransferRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(hiddenAssetVerifySplitCredit)
{
    dev::eth::ContractABI abi;
    std::string splitRequest =
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
    bytes param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT, splitRequest);
    // hiddenAssetVerifySplitCredit(string splitRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string spentCurrentCredit;
    std::string spentCreditStorage;
    std::string newCurrentCredit1;
    std::string newCreditStorage1;
    std::string newCurrentCredit2;
    std::string newCreditStorage2;

    abi.abiOut(&out, spentCurrentCredit, spentCreditStorage, newCurrentCredit1, newCreditStorage1,
        newCurrentCredit2, newCreditStorage2);
    BOOST_TEST(spentCurrentCredit ==
               "CiwxajcxbjZ0dWo0RFpyR1BMVmdrb1ZqaitEdTgwQ2lPbVlOMDQ3U29Rc1VnPRIYaUhicmdIQ3hSWU9zTnc"
               "vOUtFNFRJQT09");
    BOOST_TEST(spentCreditStorage ==
               "CrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR"
               "2Q2ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMME"
               "xTSmJJV2RQTkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1Z"
               "DllYjhkMhpICiwxajcxbjZ0dWo0RFpyR1BMVmdrb1ZqaitEdTgwQ2lPbVlOMDQ3U29Rc1VnPRIYaUhicmdI"
               "Q3hSWU9zTncvOUtFNFRJQT09IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1F"
               "zVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0U0VElBPT0=");
    BOOST_TEST(newCurrentCredit1 ==
               "CixBc2VCYXF3aDA2TkJLRWFoMVhVRlZsRDZmbzc5REFTOXBib29xMTR2MmlvPRIYaFNZV0VxaUFUdWE3WEh"
               "nYlBQaGFUZz09");
    BOOST_TEST(newCreditStorage1 ==
               "CrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR"
               "2Q2ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMME"
               "xTSmJJV2RQTkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1Z"
               "DllYjhkMxpICixBc2VCYXF3aDA2TkJLRWFoMVhVRlZsRDZmbzc5REFTOXBib29xMTR2MmlvPRIYaFNZV0Vx"
               "aUFUdWE3WEhnYlBQaGFUZz09IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1F"
               "zVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0U0VElBPT0=");
    BOOST_TEST(newCurrentCredit2 ==
               "CiwySStvNUkyQldSMmV5ZlplNzd3elVqSnJYZUM3YkRNalVrZWNSTjYvQmdZPRIYRWsvZ2prUjZTQW1HbG5"
               "0V0NqeHh2Zz09");
    BOOST_TEST(newCreditStorage2 ==
               "CrQBClhCQ1VkT0FpbzNUNUZtOGtKY2ZVYm1GNEd0VDB6TERtU3ViRVI5cjBoc09UNExKc1FsKzdUZ1ZpaWR"
               "2Q2ZWMmxVRTFKQ0FSTXNsTERySDFCM3ZzbEpqOXM9ElhRd1JEdzN3dVBCT2F4a0c5eDVSR0RFclgxUVBMME"
               "xTSmJJV2RQTkpRMGw1cFR3WEhOalM3YWZMemZJWFpweHk3NGhLVDdDTFBQTFFJblptMnFhU3JZZz09Egg1Z"
               "DllYjhkMxpICiwySStvNUkyQldSMmV5ZlplNzd3elVqSnJYZUM3YkRNalVrZWNSTjYvQmdZPRIYRWsvZ2pr"
               "UjZTQW1HbG50V0NqeHh2Zz09IkgKLDFqNzFuNnR1ajREWnJHUExWZ2tvVmpqK0R1ODBDaU9tWU4wNDdTb1F"
               "zVWc9EhhpSGJyZ0hDeFJZT3NOdy85S0U0VElBPT0=");

    std::string errorSplitRequest = "123";
    param = abi.abiIn(API_HIDDEN_ASSET_VERIFY_SPLIT_CREDIT, errorSplitRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyBoundedVoteRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixxcWk4QmMwaXNYczZjRk1nWkl2ZEw1M3A5ZWM5c1Fqc1d5RWZQSUdZUkE4PRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteRequest =
        "CvoECrQBClhRLzY1UWJvbTFvRzZBRlFQSUFOU0tQYTBseGR4eTNXbWNsaWVXVDJxRzBCWWlYM2FpMksrdWJodEhrVD"
        "FZUmNsVG1TaTgxT096aGJxbTBZVzdrYmpwQT09ElhCSE5jR3llOFV1NzdLTHJOZGVYTldXVTdxSVNxVDdKRjFQelBF"
        "NkZkK0NLRXAycEF3Z0w1N3JqejBSa3VwTGwrb1FEeXU1blRoMXFVRkxqTzBqY1VOV0k9ElwKLFNIRGNocUdXWnFpWU"
        "dQcllKK0E2UTRKRUJYbFQ5WmlxWkRvTmNxVGxJeGs9Eix3RGEybW02UStHZ3pTUVFVM2QvMGZ4MnhtbllWRDczMDlX"
        "VGhnOUt6aXpVPRouCixSSTdMQ1A2d1kvdUV0YlBKZDlCNDJheU1KTHhBMFV0bFFCZ1kzUFV4dFRZPSJmCgZLaXR0ZW"
        "4SXAosckE1OXdMY21JbWt3YmlZWHNBUWhnVG1FYkhaM1hwQVA4R3ExampDSGhsYz0SLE5wZWc4TjNsWGxpaUxaWjlr"
        "SFdvZUljR1ZoQXViWVJZdlBVU0c2cDAwRTg9ImQKBERvZ2USXAosaW5nRWVaRy9oSzZlR2wyZlJoNTZKZytyYnN0cm"
        "h0S1Y4eUFtMWhGRy9sTT0SLGRMOW9tN0h6OFk1SHVDV1pJUUFnZHY3RUJQTzVzV0NIcysyU0N0TjN4d289ImUKBUJ1"
        "bm55ElwKLG1JV2I5eHRVaE9oQVE3cEkzZGltdktBcUJPb3d6aUFHMlpmWlVodkFrWDA9EixoaWhNOUIrcTFQRkdNTn"
        "ZRTnJ4Y3ZVTUluUzBwUnBvdDFGNGtkRXgwTHhZPRKYAQoGS2l0dGVuEo0BCooBCixvZHYvQWpYOFhzdVgzSHdIVGRS"
        "U3UxWE1mVmtaWGhGQXVqMGYrd2N3bndBPRIsM0dIQmNzMzRmV2RweldJU0puczZKN0h2RGJRY0c3RlpkRGRYQ1JXM1"
        "FBaz0aLFIrdldEUVM1SW1DY1lBZnNGZ3MvVEdBSXZHM2NmOFFSdHRnOXNZODN1d289EpYBCgREb2dlEo0BCooBCiw2"
        "cGJXVWVCaE9aNEhVTkhBd29GUjZZQ015djlwaCt4dW1HS3d6aEhWc0FJPRIseDlkMDZGRS8rcXFIL0I5UDN5TVZ1L2"
        "1YY3R2R2hVTmpTQVRWdUJOQjlBZz0aLDBPeUp2YzlFOUZYNlBIYXZPeGdxcFhvUkRDK3g1TTVQcEVMNS92UjlkZ3M9"
        "EpcBCgVCdW5ueRKNAQqKAQosZklxODhNOEVSMldOMEpnSHlDOHF0L2VmaWhFNEdWam90d0RZRGtXb3J3dz0SLEtFVW"
        "tFZFlrcGF0M05Nd3U4d2NRZklBcHFwMkIyUFk5MkZsTDJnbFhhUVk9Gix4dGtGMmVFaE9YU21kK0hpWFc3ay9wNFp6"
        "a1ptdzlYZUNpQVBWYzMya1E4PRqsCExyWHpRMkkwd2kxRjdKa3FJZ2NrZkJyM0M3NmU0aitNWHZqc3FiY2k5M0V5VE"
        "ZBc3pTb1V1aUxoMkpyMDZSQ0ZlbVNxT0lYbUxzcXk2Z3oxVlJtTGFOS2Q5V0RWK1JBRFQ4NDVxQmFoNE93NXBzVGJu"
        "S0Z4NVJLdlVycU9IN2dSQlB4R0xKdDdFNU5Oa2pNUGJvaGx0ZFhSd2E0NUdDSDRucEt1a3A5bHFsQzEyc0FnWkIyMF"
        "Rtay9OMXNJMFVZbU5QVWRFbnFZNndyZWJodVdSdGVnQ3BZNU1Xbjd6T0tMd1V5TENNbHU1Lzl1ZTRZMDNRcmlmVG5p"
        "Q2Z0UXBMSVBFcnZsQzY5VzViTG1zS0swM3BBK1NBeFphWGcrN2VWbGNVWjZpalh5K0F5Q0Jya3VwTkpLeUFrQjYzTn"
        "c5UVYzajk1Z2NvSStQZzM1TTE2VzJhdUNEN3IxL2pSeTMySUdaV052dXYyT1NDT0h1eWNYdEt0MTMvSlBFR3FWeCtK"
        "OE1scUtOajJQUUhDZG5rQlhmbG1GMU5zTFloeWFvTzFCdE95NVNLVS83SHorZFlEQW05TG5TRGZCeE80Si85Z1NscD"
        "RITXFyTTRHNUtZU3Q4YUVaZVA2QVUxZVA3bHRWZU9nK1orUUNsV3RIck9ESTZRYysrWFlGS3VpckF0QjBCT0M5SUc0"
        "YitSL1ZOQTFBRVgxOElFUUpMRVRMazNrOVJYQ3FSNnZEakluSzg5UlhxUXViZFdEbUFTcWRpSWhLeWduWXhJb2VMcD"
        "lzNXRhSXpTL2hFVzdCcEtLbjUxYUtQMkt1eUh5djZuN3dkMmRiWExnZHVISjVYU1ZHakpVUlBSc0RKK1FqZkRPYzlW"
        "dXUvc0txSEdENjE3cHBZN1FwS1dEVUdLd09yOGd5Y21XbHkyU2lLTWF2MWU1VjNvWXlwdmdWODVUS2ZXTU9lZ0JZdU"
        "s2NVJYOEFGV0pLU28yMmFDK2V2U2h3UHd3OGJmQ1UydWpxWmlFdWRCTVhTT3U4Y1JLUmlweUhtdDV3aStrWjVSZzd2"
        "U0Qrek14cmVaRTg3ZzNZRTAwb3haMG9NbjlsVzVpMW9PYWxWU1liWGMzMFRpTmhjd0VybENoNnpheXA5WG44TUtnNy"
        "9ZNi9kamYvMk45OFZUVlRPNXhzbU5CakRtc0F0UHNMekRRNU9ielJTYkNyZWpJcndOc1VSYXc4blE4Ly94U3U0SWlC"
        "WmJ5TVdTT3VOSlBESFdoOW1pbzNSd2xBYUYweWc0V25CdXE3U3Ntbm1HWklEYWk4MEZQWS91Ty9lQ2s4azVPbmlYd1"
        "NITkR6VU10UkkzMlNLM0ZMSW5LdUtNYzF1Z0VnQzNXMEFidEZDSmVDaTNyYk9zTWlqRjlhZkRKNFpoRjlkV1cxaVB2"
        "K04zZmJYY3dJPSKUAgosZzJRZ2d5VFlzL0c5U2prVDlIb2kycmUrSjFNQlhOczZrK2pqcFdJMFJnUT0SLHp6Rys5Nn"
        "NSV3VXMExDcXhoajRNRWo0a0JGTkZ2ZThzckJBOUNvL2p3QVk9Giw0dmtPaWd2NXRWdklRWTRVNU5XOWk5MlhaVmg0"
        "UGZtWnYrWnJrNHpSU1E4PSIsRmxoa1haLzBNS2twWlBLL21yY0JQTlppTFJjRlFVMy9oOGlVUjJwNytBTT0qLFZSa2"
        "1QTTkzeVNheWFydHBOdFJnTVpCdDNRQ1dHZS9UQVVuNDVlQzhPd0U9MixTQ2laSE5oVkJqWEoyWWdzUThIZzArcHB5"
        "NVNOOCtYSEJjWUtDbHBlWmdNPQ==";
    bytes param =
        abi.abiIn(API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, systemParameters, voteRequest);
    // anonymousVotingVerifyBoundedVoteRequest(string systemParameters, string voteRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    u256 result;

    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorSystemParameters = "123";
    std::string errorVoteRequest = "123";
    param = abi.abiIn(
        API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, errorSystemParameters, errorVoteRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyUnboundedVoteRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixGaWxyOG1vc3dtMDZSQ1J2clZCVGF3YlhXMFNvS01xRjJNeXRRbk1FdVVjPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteRequest =
        "CqgFCrQBClhDbjEzRjNJNVpYbHZQK3N6QWpHVklzSHFST3FuUGVDSXB3UVFUaFR0bkhzdUJ0eTdrVlRzM2dkTFBFcH"
        "VnM1hGV1VOZmZxbER1THRadG9VallraUlWZz09ElhCQ1ozaHRSdlpOSVgvVkVacUZxenNFWk5pZnl4SVpMamM3ZXJB"
        "cnFGTGM3UktpNWJWR1c5VU1xaDR1bHprODg1OGhlTVVOUUZwdlVwbEFzbC9KOXhIVEU9ElwKLHJ2UUtXcnB0bVYyRE"
        "Y4NWZLbDhNNkpOcS9HMmFqYXhjT1hIQThZTnJzbHM9EixkR1RSL1FsUnQ5cUxwSnZydlRyeTZ3S09lc29LTmg5bnR3"
        "OU9IYXU1b1c0PSJmCgZLaXR0ZW4SXAosN3NBOExzazZvUWxsc2lxaE9rZ2VHQms4TXdsblpSTnZBYVdHaW5WNDFFTT"
        "0SLEdMb2d2SmtaeS9ZeVV3U3RKeVBYbFVaMHcrQ1JpUEk3ZGg0R0FRUVlxM2M9ImQKBERvZ2USXAosdGowb0ZWVHRi"
        "Um1xS3dpVVJOM1VuWUtWSkJSdEdIS2VxY09BR0VFMmxCTT0SLDRGNFBVeVIvU0M3ZVVFQjRaWHljNk83dmo3Z2NKTH"
        "Awa3lhcVNqK0lNblE9ImUKBUJ1bm55ElwKLHluWU50YmFhWTBaUHh0NmEvUHNrc05TVVMwaS81VFNyekxSd3FRVWor"
        "eHM9EixVRGUvSkFTeUhsZWZGbWRaQUFaNjltblA1ZnZKWFVWRjVzSTNpMjFkWmhRPSpcCixBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRIsQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQT0SiwQKBktpdHRlbhKABAqKAQosbXBxYkR3dTRPanZSd1pCeHFVcXJTMmZmY0h4R3BSOTFLNG82ckNaSk"
        "53QT0SLCszb2dVTlVzb1llelFGYTVKT0JBWXlBOVVVSFZOb0k2aWFFUnJ3K0YrZ2s9GixuVjZUWHpKWXNHbHlRT3Ur"
        "bnVpWDIxVTArTnBLZFR2Rm80aEtOS2tlUUFJPRLwAgosVThydGd0dU8wMElGNXFYRis3L2kwaHFicXRjeFdUVlBoOX"
        "BQTllhK1FnVT0SLDJyemVHZk9aNVU3VkVjQlBNUkpBMFovOXU5ZkJXUmNSUmlIZGtmZHJkQTg9GixHQnhUV2xaeWRY"
        "Uy9EUXNZM0xqTXdZaEpERXJGTFdCNU0yN1NvMjA3TEF3PSIsaEZoVW81NjE5NTlHMGN0WWtTTFhCR2NxNUNBS2RZRi"
        "9GZm5qUlF5Q3JBQT0qLFhPVWJIV1ZoNSs0NVg2T3RacU1BNms3b3FSMVlQS242TDZzNlBtN1JaZ3c9MixaNitOMTBG"
        "Qnd6dUwxZW9HL3VBT1JPbGhMNjZ2UEtZMy9QakJOOS84akFNPTosSllQS3lzWmU4aGZFcGh4Zyt3VkNiYktscHRNTT"
        "BZNERvRWFBVStlMFRRaz1CLEtDSjN0SXlUM2swWDBWTWZKRFFIVUlQa28vVGUxVnlUMEUzdlNrYjFCd3M9EokECgRE"
        "b2dlEoAECooBCixxWWxQaUtpR2lpV2Q3OVUxQm9oZThpa0NWZzdxZW95bXI0SmRrejgrQVFFPRIsNE1DUTFPbW5BZG"
        "xUb1Q5ck51SXRmbzZpaE1RR2R0R3luQjFFbWF5UTF3VT0aLFFUNmZIRkNuU0pGVmJBUE0yZ0dHeHc5SktXWEtmT1hS"
        "cDJKT1VnbDN4Z1k9EvACCixoUHZrUTdzYndrRk9rdmY4T0ZOaEdERjR4OWo2TnRpQ01jN01IMHZLOUFzPRIsdHhtYX"
        "NkdkZtRXh2alBRVERpSmU4bHRKdjFOaEo3bXdqZFNiU01BZHBnOD0aLElKOFFCc08wQjRnNmhPZDVkL04zMDI0VGI2"
        "NjRMbnBrWFJ5THVUcmUvZzg9IixtbnMzb1pjVTVHaW1RWTNTam9WVVpocEFUcWYzMDJyU0M2MW5sQkgwU0FzPSoscz"
        "REMTNBcEhRSElNNTNhS3BXNExLcTcvNldwbm9FQ0c2Uy9RMFZRczNnRT0yLDRwV0xldm5TRUZ6SGNzbkZZQmRnYlVh"
        "eDlsRE8yWDVKOXVuRG5ZWTlSQXc9OixTa015UDQyQUgvS0djMEZzM0ppVmg0aTFtQytpRExhNTE4SEZsdFlnNndnPU"
        "IsUUdZRlBRamNnVXduZkxsM3EreEJLRDFtZ3c1U0JGK0tZTHl4Rkhsb2p3QT0SigQKBUJ1bm55EoAECooBCix5Nkkx"
        "bDc1Vm1GTDhJVDBvcGNTWmNtQ2tNamJiQXl6VzljaTZjelpSeXdZPRIsYU12eDlkVmlkbkh3TEhHMkl0UDJFWDMwaH"
        "poaEI2dXJ1V1JVSlhCcFNRcz0aLE96V0ZtU21BOGZ5OHVobnB4ZjdQZXJ4bGhXSVJVQUdXdTdXNlN4R0hUQXc9EvAC"
        "CixSRVZ1UkF4Y0ZuWjB6RXE0NHBGMzd4MVhHb2NoSGI3RlZ6U2dzMzdvQ3dVPRIsamtzM3FQK242YVBhM1VIWEVxdH"
        "kwd0MwL0hFRFovWHFTenJKYStzU1dBVT0aLHdhY2wvbWY1bElxcGp6Yzhad05kWnhFU2RQUGJrUUw4M2xES3F4N2or"
        "UTg9IixUSmlLUzd3UGFTdktiU0o1S2YycUNIUStwVmhVdm05Q2EzdlBZN0VXZGdRPSosam80bzVYN0Z3RDhyV2ovMW"
        "p2cGFaY0dodlI4SGFqYWg1bC9oUGdXdGR3RT0yLDQ4NkpQVWtDbmh3VFZpRzQrVmNmYXBISDRnTWdSOFdHdzczU2Nl"
        "ZEdhZ009OixSR1d1djRRdU84d1d6dzFNRnZUdHVqcmt3SVBPQUJnb2cvRTkxVkZzaHdvPUIsUk5kcG1adndna0FjYk"
        "RudTlOWGNyT0ZLK2JyenVHZlp6aXpUVFYwQTdnaz0=";
    bytes param = abi.abiIn(
        API_ANONYMOUS_VOTING_UNBOUNDED_VERIFY_VOTE_REQUEST, systemParameters, voteRequest);
    //  anonymousVotingVerifyUnboundedVoteRequest(string systemParameters, string voteRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    u256 result;

    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorSystemParameters = "123";
    std::string errorVoteRequest = "123";
    param = abi.abiIn(
        API_ANONYMOUS_VOTING_BOUNDED_VERIFY_VOTE_REQUEST, errorSystemParameters, errorVoteRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingAggregateVoteSumResponse)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixVQkpWbjFYY3p0Nmg1SDQvTzI2NmNkd3J6YVZSUWxhcWNjMnNSREtMWm5BPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteRequest =
        "CvoECrQBClh4Q3FjVWlsczdXeTVxcy9WTGpoUmxkVDVWODU5T3I1MFUrdGRNR0tiV1dRQUpSN3gvV1VkRlZxZm92U1"
        "RFMGJ0T0ZkaUZTNmF1U2dQTGZyMDRaK0E2dz09ElhCRXRSUGZNNWNOYVZ3UjNvQm9UY1Q0djlzSjdabDdIMzNrTGtK"
        "Q1VSWjg1MUJNWUd2cFhHM1cvU0lEczZJNUdvV1Z5QXprWU1PVm91SHNWRmVCTjdqWkU9ElwKLE1nTUplRyt3c3hkOW"
        "ZTUXQzQytDbEt2bFVKRnFBcHh3NlBmVTM0eUk4a2s9Eiw4TTgvUjNsdUF6VVh6RzZMem9jdzNFSldXYmZNNmVEa1JW"
        "aEJNZ1d3WWgwPRouCixVQ0tpRGkzTjhYdmJuVUNleHdRYzdBaHFTNzJKbm5nK0twM0V5WUVqb2hrPSJmCgZLaXR0ZW"
        "4SXAosQUJ6Y3c3OFozYzVmVGxoUzh5QmtwamdXeC9ZeDhpb2lnanMwWjlKS0hCYz0SLEJKbjBxWFd1czZrUWtzN2NM"
        "dUNaWDNEOEVHME1CNEt5cEk3VmRKTjhvQ3c9ImQKBERvZ2USXAosa09tTi92d3p3THlUaC95REVJdTlUR0hzczFQM0"
        "p2V3phcTFvblJ2K0xUdz0SLG1Bb0tyN3dBOTNQam5icXYzV1poUGljMzdZY0Q0clgyb29RKzl6N1FBbG89ImUKBUJ1"
        "bm55ElwKLGZDbUtyOEVmTFMwUWpvYzgxZ1JjL09qdlpwcktaNDB1U3ByVEtFVStPMWM9EixRc3p6NGRhcURVMloxaX"
        "Z4NUhMWXBxbk9icGpldDMvZXZlUGE2aU10M21nPRKYAQoGS2l0dGVuEo0BCooBCixNRldZaUdQVEFMMEUvdUdCU2dY"
        "djFidHpsSTNaWTVTaG0vTmM2VUozL0FNPRIsNWJReTZ4dVBoaWVnUmlydFJsZ0g0Vk5OR2ZlSUs4QUREZTgxb05oZz"
        "lnZz0aLGV1ZEJMRVlHOTZ0WnNyT09kMFVNbmV5cE96OFRXcjdYNDdSMFB4b3kvQTg9EpYBCgREb2dlEo0BCooBCixK"
        "S2VKZVFOajBQbVM5RXJSZC93NUNTcXpHUk1Ud2tKSS9ZSEdDbkdRZGdzPRIsMndSVkZGcDNLRk9BQ1RLbUcvV2NvMk"
        "I1QTVzUlFXS0hwQVk4T00yN0lBRT0aLEl1N0V1UW5vbmZTcXlVa05TU0JzRzgvVUs1UUhXZ3ZEeXRScnRSTlhzQTQ9"
        "EpcBCgVCdW5ueRKNAQqKAQosSkhlVnFDZW05OGRjRFJESmtKdDVqMW92aDlybTRRWS9QUzFERnM1RzZBbz0SLC9Tbk"
        "lGcWNEVFVEdldxbkI2WnJkajJHUThHZG1mRktzb09ZVnBMVTdjUXc9Giw4cDZtV3piUExLbUFMdXNqTWdHU2dDbkd6"
        "QVpEQy9zejdNWDFYMWN5U3dvPRqsCFl0Wm9ZN1E0MHc0NzV5MDVvVitJbjZkUUhPU0hsY3JmTWtTdW1LT1FHVmY2YS"
        "t1VW5KK1F0QkIvcWRtcFFaeVdZdlJKMFBIcExScjAwbmQ5ZWtKZFMvcVZ2aGx4SVRrdC9jd2EvSG9uYVhDa1pZSWZQ"
        "MGJ6SFF2Ri9lREFOMTRsaWdqejNrQnluWHlKb1lEbCtYOFVRTjR3U3ZwL0xWcG5sWXZxTWoyMlptTlVPMmFvd2g1aE"
        "JrSXE4YXo2aXI1Z0tidmZtcGV6ZVZ6U0lPMDFGREVSQURwYVlrNEJ0dE41WEJRcEtJanRCQmsrSEV5bnVKNDk0c0dS"
        "UmhRNVQ3RUxJdWY5SG8rSFpFUFpVZVBrSjhFZVJxcXFvcENrVXh3UnI4QXpBMUdBL1FGY00xWk9UQ2pNc1B0U3N1eW"
        "81VTNSYklhTkl4TGlGRHVLTk5DZjFDekZFYzRIdWNVbGhFQ3pnL1U1dVFPT2sveFg5cXVZNXRxVGpaNGJZNlhTVFRO"
        "UEVqV3BNdEJqcy9tZDN2Q0tFRmk2M1lRcEZkM3ZsZzM4MlVDSDl5Sm9mWG42d2tUS0dDc25nVkduWnhhV3pIQjN6Yj"
        "BaUXEvaXBBYk9VY1VkWlZBNkF5TFdiMUZHUHJFeWN3T1Fld0lBZndZSThXMjRVOUt2OVVHa2hNS0sraWNublBFbjZX"
        "NVVPYTJZSDE2YUVLWG9zZzVodkZmN2E3dGI2K0ZxQmVlckpCUXd5ZjUxYXlWNmlKZGc1R2RBZEJzTGVSSnFGbXFzQj"
        "JNRWZvZlR4Z1J6QjBpbmUxNUxMZDdZcS9lTGd2U2RaeVNjbHpDUGs5TndBVkNrVkJnSU9sQnZ5anZIZ1BoL1FWczBT"
        "WVZVUUtwTGNkSHFaMDNIM3JqRGpOa2h4ZjMvaFVQK1E1SHh6bC9jWXRUcFJXaXVzYVhWZFdYLzVzMy9iNllXR2VieT"
        "NibGpNN3cxWW5pZXBxLzJwSzIyYlBBQnZaMm9ZWjZXNlc0aTZ6Rmw3OGw0K3c5d3JrNzB3MWhvWGo4RVZmdlYxT09o"
        "aUtCYkFJc0JlTkhDbTdhS2VmNklZbEE2RlF3cm9kRnpZYUdxMDNld3h1QTBSRVRaTkQxRXdmKzByMHRXeGltclN4Q1"
        "IrSXcvNWRWd1UwaEVITEFTd0VVWlZPcFhWSWprWmJGb3Jxc05td0ppL29VcDVmZEthYkJjTmRmTWE3MHJCRC93STYz"
        "NmR3K1pyNzlkVVRkSnJ4NkVId3BOU1RJSTg3cVdNcklCRVdXMFdKOTN0VHZ0b1hEOXlxZTd2b2tJTjU4MmRBWGZNRj"
        "VJYXdBT2p4U09KbGJaUEtyWXYydldROHRIeEljL1ZXVUlvQ0haUVFodFdpbFpDbyt3SVJlS1h1WUZ6eUY3NkFDU0k2"
        "VFhEZ2N2cVFnPSKUAgosaklxRlJkYU1qeVE3NDBiRkRhMEp0VXN3V3R1aitickROVExDQVpyZnp3dz0SLEllekdFbD"
        "V2a3BBVmR0eVhqeHRhVnBYQjJlb0tBSy9JMmZ1QVh6Nk1Wd0k9Giw1d0h3YXRFeEMvOGlTRGNxSU9oVjBuRHNNRXZk"
        "bFFWamdGZ1VsRnk1aWdZPSIsZ3oyYXVlT3hJb1Ryem00QUFLbUNmTXJ2a1lrN25VMzhhOXNxckpmRXJRaz0qLCtYTm"
        "ZtT1E4enJQQkpnS0tBVHhaeTgxYzBmSWtQeDM1UU0zUk1iVllUUWc9MixVeUFSd0ZIdFJGb05GV0cwWERTbjNtQ0tQ"
        "Ym85bldsRi83MUNDQjJBR1E4PQ==";
    std::string voteStorage =
        "ElwKLFNpTzZoY0pGMGZ2ZnBBRWx4bk80TXdxMnh3M0dMVmRMQzV5cnpPd0ZMVGM9EixOT2hjNU1pTitmMVpOYmc2ZX"
        "dGTklTdkduc3o3WWpOUUk5c1NEUzV3Q21VPSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosYUtITTAvZXVOK0xRMG5JZTk5dDV5dGJibXVPVVQyaXYySm9YK0R4NzBXVT0SLHp2VndTc1VjMz"
        "JXNUMzNGhlRmlhY3dqa3BQREJ4MDV2UHhqR01RRzVCekU9ImQKBERvZ2USXAosOGtKSTE2YzJiamhwUVRpL3ozUnFO"
        "QmF6VTM0cTE2L1pyNVhWZkNjWHVGMD0SLGRqRXlsWHdzaWt2ZG5vU0s1SS8vakljWDlacVlFRURDUWNycG92RVp2MG"
        "89ImUKBUJ1bm55ElwKLCt0eGtNa0N4TUFueGxYNlJBNUpSa0s0OTcxWDBxaTk5ME84TEQ4RkphVzg9Eiw2Z2dzOGRy"
        "NEZPeE43NTFqN0c1UWxlSXhRazh2TEFEUWhUODhDK0VMd2x3PSJmCgZLaXR0ZW4SXAosaHA3NXh1RDNhQjJCNEt5cU"
        "1sczdzSVloWEtRRnZJdGhxRG5iTHJ3VVZDTT0SLE1HMWd1MUt1Si9zRC9yRVMxam8xR0FGSWZTWjc3eDg4elpiR1pX"
        "Y2hJUms9ImQKBERvZ2USXAosQkxXK3M0WUZ4emRxRnV2V1hpUFVsZFVrWVNPWXBkY1hmRXdBWlJMcFBDaz0SLEtNSm"
        "VYL2dnRWlYeThPM0pYRGpmTkYyZVhvMCtMSXAxTGZROUVKRC90UXM9ImUKBUJ1bm55ElwKLHRveUI0SC84WmdSclJv"
        "TVNZQnd5Z2c2b1huT1QxTDhLd0RWRnVLT3ZXejQ9Eiw0TTA4RG9BUktTNVVSUldNNm9WNk5pUzR2M3d2SDhMY2FoZH"
        "ZpWDRyN1RFPQ==";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_AGGREGATE_VOTE_SUM_RESPONSE, systemParameters,
        voteRequest, voteStorage);
    // anonymousVotingAggregateVoteSumResponse(string systemParameters, string voteRequest, string
    // voteStorage)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string blankBallot;
    std::string voteStoragePart;
    std::string voteStorageSum;

    abi.abiOut(&out, blankBallot, voteStoragePart, voteStorageSum);
    BOOST_TEST(blankBallot ==
               "CixNZ01KZUcrd3N4ZDlmU1F0M0MrQ2xLdmxVSkZxQXB4dzZQZlUzNHlJOGtrPRIsOE04L1IzbHVBelVYekc"
               "2THpvY3czRUpXV2JmTTZlRGtSVmhCTWdXd1loMD0=");
    BOOST_TEST(
        voteStoragePart ==
        "CrQBClh4Q3FjVWlsczdXeTVxcy9WTGpoUmxkVDVWODU5T3I1MFUrdGRNR0tiV1dRQUpSN3gvV1VkRlZxZm92U1RFMG"
        "J0T0ZkaUZTNmF1U2dQTGZyMDRaK0E2dz09ElhCRXRSUGZNNWNOYVZ3UjNvQm9UY1Q0djlzSjdabDdIMzNrTGtKQ1VS"
        "Wjg1MUJNWUd2cFhHM1cvU0lEczZJNUdvV1Z5QXprWU1PVm91SHNWRmVCTjdqWkU9ElwKLE1nTUplRyt3c3hkOWZTUX"
        "QzQytDbEt2bFVKRnFBcHh3NlBmVTM0eUk4a2s9Eiw4TTgvUjNsdUF6VVh6RzZMem9jdzNFSldXYmZNNmVEa1JWaEJN"
        "Z1d3WWgwPRouCixVQ0tpRGkzTjhYdmJuVUNleHdRYzdBaHFTNzJKbm5nK0twM0V5WUVqb2hrPSJmCgZLaXR0ZW4SXA"
        "osQUJ6Y3c3OFozYzVmVGxoUzh5QmtwamdXeC9ZeDhpb2lnanMwWjlKS0hCYz0SLEJKbjBxWFd1czZrUWtzN2NMdUNa"
        "WDNEOEVHME1CNEt5cEk3VmRKTjhvQ3c9ImQKBERvZ2USXAosa09tTi92d3p3THlUaC95REVJdTlUR0hzczFQM0p2V3"
        "phcTFvblJ2K0xUdz0SLG1Bb0tyN3dBOTNQam5icXYzV1poUGljMzdZY0Q0clgyb29RKzl6N1FBbG89ImUKBUJ1bm55"
        "ElwKLGZDbUtyOEVmTFMwUWpvYzgxZ1JjL09qdlpwcktaNDB1U3ByVEtFVStPMWM9EixRc3p6NGRhcURVMloxaXZ4NU"
        "hMWXBxbk9icGpldDMvZXZlUGE2aU10M21nPQ==");
    BOOST_TEST(
        voteStorageSum ==
        "ElwKLGRDRStXZE05UnpQTHlLMFFCNlZucExlUkhUZ1lNNnR2THJvZHM0UGsraDg9EixJTzhzejBMVWFRK3NJZzdxbD"
        "dyNkxPeCtPVEpRLzl1TU5Rb245bXVSOGd3PSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosYUtITTAvZXVOK0xRMG5JZTk5dDV5dGJibXVPVVQyaXYySm9YK0R4NzBXVT0SLHp2VndTc1VjMz"
        "JXNUMzNGhlRmlhY3dqa3BQREJ4MDV2UHhqR01RRzVCekU9ImQKBERvZ2USXAosOGtKSTE2YzJiamhwUVRpL3ozUnFO"
        "QmF6VTM0cTE2L1pyNVhWZkNjWHVGMD0SLGRqRXlsWHdzaWt2ZG5vU0s1SS8vakljWDlacVlFRURDUWNycG92RVp2MG"
        "89ImUKBUJ1bm55ElwKLCt0eGtNa0N4TUFueGxYNlJBNUpSa0s0OTcxWDBxaTk5ME84TEQ4RkphVzg9Eiw2Z2dzOGRy"
        "NEZPeE43NTFqN0c1UWxlSXhRazh2TEFEUWhUODhDK0VMd2x3PSJmCgZLaXR0ZW4SXAosaHA3NXh1RDNhQjJCNEt5cU"
        "1sczdzSVloWEtRRnZJdGhxRG5iTHJ3VVZDTT0SLE1HMWd1MUt1Si9zRC9yRVMxam8xR0FGSWZTWjc3eDg4elpiR1pX"
        "Y2hJUms9ImQKBERvZ2USXAosQkxXK3M0WUZ4emRxRnV2V1hpUFVsZFVrWVNPWXBkY1hmRXdBWlJMcFBDaz0SLEtNSm"
        "VYL2dnRWlYeThPM0pYRGpmTkYyZVhvMCtMSXAxTGZROUVKRC90UXM9ImUKBUJ1bm55ElwKLHRveUI0SC84WmdSclJv"
        "TVNZQnd5Z2c2b1huT1QxTDhLd0RWRnVLT3ZXejQ9Eiw0TTA4RG9BUktTNVVSUldNNm9WNk5pUzR2M3d2SDhMY2FoZH"
        "ZpWDRyN1RFPSJmCgZLaXR0ZW4SXAosZWliR1Z5RjZPSUVDVS83QlJSK1cyWkc4U3FEQ0NYOEwrVEF4YmpOZ2gzdz0S"
        "LHRLdXh5b1FtS3QyUVFiWHZxbEpmTXpoMDRHMnl4NjVaSDlybUl3TXVVRjQ9ImQKBERvZ2USXAosSG9kWC9ZVWMyYk"
        "80RGkrck9XVHF0dmtpQXErdVQ4T0dCT09IWnFSdW1DTT0SLDBvZC9kZ0lHQzdBdE1Wb1dwejh6dHRLWHZLRk9QcHpF"
        "aE5vT2Q5ZTI2M0k9ImUKBUJ1bm55ElwKLHFxYlRXSEVFVmtDWm0wUzZWUS83cUh0MTU1NzBvVUFKc2FkUWVISXFoRn"
        "c9EixPbVZaT25PN3hIempCYkhRWU9pRW8zYUNXa0Jvd0pMc1hNMWJiSk9ydmhJPQ==");
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyCountRequest)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixxcWk4QmMwaXNYczZjRk1nWkl2ZEw1M3A5ZWM5c1Fqc1d5RWZQSUdZUkE4PRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteStorage =
        "ElwKLDFuZlcyWDBpT3FPTjEwTHBOMnkzSnZlTVJaaU9DY1pzUk1yaUt4YVExbW89EixRampqaDhkWFRiZnBLKzZ1TX"
        "ZzUkh0amZEMjhNUjVPV0RIRDN2MmkvY1FVPSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosckE1OXdMY21JbWt3YmlZWHNBUWhnVG1FYkhaM1hwQVA4R3ExampDSGhsYz0SLE5wZWc4TjNsWG"
        "xpaUxaWjlrSFdvZUljR1ZoQXViWVJZdlBVU0c2cDAwRTg9ImQKBERvZ2USXAosaW5nRWVaRy9oSzZlR2wyZlJoNTZK"
        "ZytyYnN0cmh0S1Y4eUFtMWhGRy9sTT0SLGRMOW9tN0h6OFk1SHVDV1pJUUFnZHY3RUJQTzVzV0NIcysyU0N0TjN4d2"
        "89ImUKBUJ1bm55ElwKLG1JV2I5eHRVaE9oQVE3cEkzZGltdktBcUJPb3d6aUFHMlpmWlVodkFrWDA9EixoaWhNOUIr"
        "cTFQRkdNTnZRTnJ4Y3ZVTUluUzBwUnBvdDFGNGtkRXgwTHhZPSJmCgZLaXR0ZW4SXAosdUdHUmZrZ0Y4QkVML3VjZj"
        "crT09kc1Y0UDNENEhDUGo2UURNVWRXR1pEQT0SLGpxTHFQTDZlM0VqVWtRQW0ycFh5ZmtsNERhV2IwRFUzdHFlVWd5"
        "NUhCaFk9ImQKBERvZ2USXAoscHFrRjJMbWdaRENTeTFFaytlTWo5Q1JaU1FqSE9TSlZaZnU2RzJBS1puMD0SLFlHZV"
        "VuQUlYRk95dzJQcGxXWjVVa2N0cHhXNGp4VVljbDQxbGQxRmluRFU9ImUKBUJ1bm55ElwKLHFyMm9RckQrRGluaXJF"
        "R0kyZHUzVHZqejJmaFlFKzVKbnV2RC9KQTZmQUk9EixrdWQ1dmFHeFdCWVFFdWpDSnBVVUhDYnNEd0NoWmZhcStMWW"
        "JWV2NLb1JrPSJmCgZLaXR0ZW4SXAosYW5oemkwYXpMYTVnZXIxYzJYRDRpeTl0SGw2ZTl5VTFGK1l6bjZadVZCOD0S"
        "LE5tMnE3Tzkrc0Q3SHdoMFNpT25IdFovZURyMnBVdkxualg0Nld3OWx1MDg9ImQKBERvZ2USXAosaGtvZ2lTZnRiSF"
        "B3WFBPY0Yrb1AwczZuaElZK29WQWlFek5rUDhYQ1lYZz0SLGRIc2FkL1ZWaGdMRXI5WTdGUWtHUVhlWmVIQStOTTlT"
        "UEpwdXJ6R1ZlUVE9ImUKBUJ1bm55ElwKLEVuOWdRcjdlcGtpbnVrWFBYU2JaSEVTdlpRS09LWDEya3B4aDRlbitlVH"
        "c9EixZRmF6WWlxTEs5bkVneHpXdHJUeDRCSEd2QkJwZzl1UlhLR0laRkV6a1ZjPQ==";
    std::string hPointShare = "9GBQ+m8cy4qDwvQA/uALl2DzVzJ8+mtr1ZSqHQRBCAA=";
    std::string decryptedRequest =
        "CsIPCpMBCgUxMDA4NhIsQWp5dko5NTN2aUxDenJhdUpoWVlkeHlOWEFmQkZrb3JuczNMbCtSZXdEdz0aXAosMXRPUF"
        "psUWcwOXhrWVh1TVlZK2VuMXhsS1VJUTRNNHRDUUpkVVJpYXBRRT0SLEFadDYzMk1rQmNwOFVzdmRGWHRpb0NZMzdm"
        "NHEwVG5VKzE5OW9nazJ2QWc9EpcBCgZLaXR0ZW4SjAESLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUE9GlwKLDVUdytTdzhjTnVoNUVKektLTzNQMGJtSUZ2b21vaFh1UWhHcExnVzJYQWM9EixnV05OQXhi"
        "cXpnV2RGYlVpUWRlNDZweVVnTmUwdzZYanZ3Ym0wb21uUGdvPRKVAQoERG9nZRKMARIsQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosbWNISm44a0Q4NkNvcXlMR1RSUkloR0YydjRndjNFWUg0bG1Q"
        "QWsrQ1Jndz0SLENKR3RBR3pmVkZqZUtzbngrS2RGaEw3M0tJWHZpc2c1SkhJTkF1VXVFUVU9EpYBCgVCdW5ueRKMAR"
        "IsQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosUzErZERiZldLcTRnbXhYK1ZD"
        "MTBSRDYzSS9qZUJFMFBoazhVS1poRVpBZz0SLHNRTkRSbUVSZWpTbkxvb2FWNFF1N3BKQ3IxNG80ck16aDkvUWs0UE"
        "9Bd2s9EpcBCgZLaXR0ZW4SjAESLGNtVkFUbnBQeWRLOHN2ZG5PSUtxVTU1NWd3WlRpR1p6QTRvKzJKcGpEM2M9GlwK"
        "LFRvZHBFMGg0WkJ5a0UzSldzZzJ2Z3JXYXBXclpNam1EQWVLZ3gzaEpkQXM9EixacnBCQklZR2dqdW9LdGZGQWVUdz"
        "hjenRDRzYzSW5RVGdjSm9ERmQvTWcwPRKVAQoERG9nZRKMARIsU25wMjB4OUw1d2t2RHpzblBHcW1kVDhuQjhBMUM3"
        "WENXaGZPeFd0NE9rQT0aXAoselpjcHhVcS9pSVB1K0w2bFlRcDZXc3JybVhUcGtxWHpCcEIxdVdPYnJRTT0SLElmej"
        "Qxanp0QVo2ZFMzblBUNERzb0NIMVJpQWZ4WjNmajA2YUJjQUU3Z2M9EpYBCgVCdW5ueRKMARIsbU5kTEVNcGw3cTlz"
        "cXJFaVNvdDdMYkdwS2pCUEljdE5SaWRiU01pT2pTST0aXAosaG10bzU3enhpOWNFNStLSm83a2JWOHV0SUFuUDNXck"
        "VDdWxTeHNBVzRRcz0SLEJEb1lDT0R4eHFzNTlaRWhhUVNtL1V5M3dzQ0YwU3NZS2JhMEloNW1vd2c9EpcBCgZLaXR0"
        "ZW4SjAESLC9HQk5QMThTVXNXMGtWVzhMUGxQZ2pZMjc0c0l0VXI0VkF5aE51M1dnSFE9GlwKLCtYV3pxSjlFblVic2"
        "51MUNqV0N2N3ZCQ282WlRTNVJQUFdpalc4UWltZ1k9EixQTXF1enljOEtnazRaVEJtRVJXRDFCSzZ5Nys2eUZUa2k2"
        "eno1L1RJUHdrPRKVAQoERG9nZRKMARIsMXUwTmR4Uk1PN2huRzdTMTBWeTJEWGU1VEgvRko5cTZlTS9CQnA0Y05uWT"
        "0aXAosZXdhZUdhMHc4bnlsSmFDS3JTaGYxQ1kzRWJWZ3cyMlN0OXZ6Z2pUd3Z3az0SLHNhcmJnNFhiRVNzc3kzTFI5"
        "SElQbUlPdjIybitlUUtlSXhVV0grM3ZEUTg9EpYBCgVCdW5ueRKMARIsYkdQam8zUlNaTXZBTmkwUlo0Zi9JVWNQaF"
        "kwNE5GbGVhMWpYRjBOakFSQT0aXAosTXYrWk95OEk3RGtMZjZzVTh4cDIxa2hPbnpmbCtqc0QrTmJSbTAyMDBRWT0S"
        "LDZNQXBsUGRScTdvRkxjQlhLZ0FKQUduYWxkTWlmNXpJSjNmanVQV2xzd009EpcBCgZLaXR0ZW4SjAESLHNudXp1ZG"
        "ZxWGRkU1RTWFZxQ1B4MGJPOU1iMzhJdGFZSzFJNDRaaE53MWc9GlwKLFV4QWJubEVVVm1vYmZVME5RZXpFQ2FYcUwx"
        "M3ljcE9ZeTRtTzRMemIrZ1E9EixkN3hmRnArS3VJUUE2TllTeGdKTUlQdHc2MHFuVUo1UkI1ekdkZlBpV1FjPRKVAQ"
        "oERG9nZRKMARIsc3ZramwyTEIraTFjbXkycGJRSWQvR1EwSGludWhQT0I5YnBLK1VZSGhHcz0aXAosS1ord091ZmlJ"
        "M1pOOVFzMEkwaG5EaXJVSDNmRE5UaGNvOGdkdys5WnB3Zz0SLEV6Q1RaME1NeElLSjFOS1h6UC9nM1BKQjk4WG80YX"
        "lkbWxaK0NibDZkQUE9EpYBCgVCdW5ueRKMARIseUdLOFQrZnl2bzBvbHNselJnb2FFUlZXeWprdEltZ3lDb21CNElC"
        "NkZnRT0aXAosVWVJU3dhNHdkMDd3bEZyMGRGWUpPeis5TU8wNnlLeS8xZGJacjliUjNRND0SLFFwZjE2YnVQSGNOaG"
        "t1ZWdWY1N0TUxBZWtnZFBZUlNuUlVtNjFOdFdVd1k9";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST, systemParameters,
        voteStorage, hPointShare, decryptedRequest);
    // anonymousVotingVerifyCountRequest(string systemParameters, string voteStorage, string
    // hPointShare, string decryptedRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    u256 result;

    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorSystemParameters = "123";
    std::string errorVoteStorage = "123";
    std::string errorHPointShare = "123";
    std::string errorDecryptedRequest = "123";
    param = abi.abiIn(API_ANONYMOUS_VOTING_VERIFY_COUNT_REQUEST, errorSystemParameters,
        errorVoteStorage, errorHPointShare, errorDecryptedRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingAggregateDecryptedPartSum)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "CixxcWk4QmMwaXNYczZjRk1nWkl2ZEw1M3A5ZWM5c1Fqc1d5RWZQSUdZUkE4PRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string decryptedRequest =
        "CsIPCpMBCgUxMDA4NhIsQWp5dko5NTN2aUxDenJhdUpoWVlkeHlOWEFmQkZrb3JuczNMbCtSZXdEdz0aXAosMXRPUF"
        "psUWcwOXhrWVh1TVlZK2VuMXhsS1VJUTRNNHRDUUpkVVJpYXBRRT0SLEFadDYzMk1rQmNwOFVzdmRGWHRpb0NZMzdm"
        "NHEwVG5VKzE5OW9nazJ2QWc9EpcBCgZLaXR0ZW4SjAESLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUE9GlwKLDVUdytTdzhjTnVoNUVKektLTzNQMGJtSUZ2b21vaFh1UWhHcExnVzJYQWM9EixnV05OQXhi"
        "cXpnV2RGYlVpUWRlNDZweVVnTmUwdzZYanZ3Ym0wb21uUGdvPRKVAQoERG9nZRKMARIsQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosbWNISm44a0Q4NkNvcXlMR1RSUkloR0YydjRndjNFWUg0bG1Q"
        "QWsrQ1Jndz0SLENKR3RBR3pmVkZqZUtzbngrS2RGaEw3M0tJWHZpc2c1SkhJTkF1VXVFUVU9EpYBCgVCdW5ueRKMAR"
        "IsQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosUzErZERiZldLcTRnbXhYK1ZD"
        "MTBSRDYzSS9qZUJFMFBoazhVS1poRVpBZz0SLHNRTkRSbUVSZWpTbkxvb2FWNFF1N3BKQ3IxNG80ck16aDkvUWs0UE"
        "9Bd2s9EpcBCgZLaXR0ZW4SjAESLGNtVkFUbnBQeWRLOHN2ZG5PSUtxVTU1NWd3WlRpR1p6QTRvKzJKcGpEM2M9GlwK"
        "LFRvZHBFMGg0WkJ5a0UzSldzZzJ2Z3JXYXBXclpNam1EQWVLZ3gzaEpkQXM9EixacnBCQklZR2dqdW9LdGZGQWVUdz"
        "hjenRDRzYzSW5RVGdjSm9ERmQvTWcwPRKVAQoERG9nZRKMARIsU25wMjB4OUw1d2t2RHpzblBHcW1kVDhuQjhBMUM3"
        "WENXaGZPeFd0NE9rQT0aXAoselpjcHhVcS9pSVB1K0w2bFlRcDZXc3JybVhUcGtxWHpCcEIxdVdPYnJRTT0SLElmej"
        "Qxanp0QVo2ZFMzblBUNERzb0NIMVJpQWZ4WjNmajA2YUJjQUU3Z2M9EpYBCgVCdW5ueRKMARIsbU5kTEVNcGw3cTlz"
        "cXJFaVNvdDdMYkdwS2pCUEljdE5SaWRiU01pT2pTST0aXAosaG10bzU3enhpOWNFNStLSm83a2JWOHV0SUFuUDNXck"
        "VDdWxTeHNBVzRRcz0SLEJEb1lDT0R4eHFzNTlaRWhhUVNtL1V5M3dzQ0YwU3NZS2JhMEloNW1vd2c9EpcBCgZLaXR0"
        "ZW4SjAESLC9HQk5QMThTVXNXMGtWVzhMUGxQZ2pZMjc0c0l0VXI0VkF5aE51M1dnSFE9GlwKLCtYV3pxSjlFblVic2"
        "51MUNqV0N2N3ZCQ282WlRTNVJQUFdpalc4UWltZ1k9EixQTXF1enljOEtnazRaVEJtRVJXRDFCSzZ5Nys2eUZUa2k2"
        "eno1L1RJUHdrPRKVAQoERG9nZRKMARIsMXUwTmR4Uk1PN2huRzdTMTBWeTJEWGU1VEgvRko5cTZlTS9CQnA0Y05uWT"
        "0aXAosZXdhZUdhMHc4bnlsSmFDS3JTaGYxQ1kzRWJWZ3cyMlN0OXZ6Z2pUd3Z3az0SLHNhcmJnNFhiRVNzc3kzTFI5"
        "SElQbUlPdjIybitlUUtlSXhVV0grM3ZEUTg9EpYBCgVCdW5ueRKMARIsYkdQam8zUlNaTXZBTmkwUlo0Zi9JVWNQaF"
        "kwNE5GbGVhMWpYRjBOakFSQT0aXAosTXYrWk95OEk3RGtMZjZzVTh4cDIxa2hPbnpmbCtqc0QrTmJSbTAyMDBRWT0S"
        "LDZNQXBsUGRScTdvRkxjQlhLZ0FKQUduYWxkTWlmNXpJSjNmanVQV2xzd009EpcBCgZLaXR0ZW4SjAESLHNudXp1ZG"
        "ZxWGRkU1RTWFZxQ1B4MGJPOU1iMzhJdGFZSzFJNDRaaE53MWc9GlwKLFV4QWJubEVVVm1vYmZVME5RZXpFQ2FYcUwx"
        "M3ljcE9ZeTRtTzRMemIrZ1E9EixkN3hmRnArS3VJUUE2TllTeGdKTUlQdHc2MHFuVUo1UkI1ekdkZlBpV1FjPRKVAQ"
        "oERG9nZRKMARIsc3ZramwyTEIraTFjbXkycGJRSWQvR1EwSGludWhQT0I5YnBLK1VZSGhHcz0aXAosS1ord091ZmlJ"
        "M1pOOVFzMEkwaG5EaXJVSDNmRE5UaGNvOGdkdys5WnB3Zz0SLEV6Q1RaME1NeElLSjFOS1h6UC9nM1BKQjk4WG80YX"
        "lkbWxaK0NibDZkQUE9EpYBCgVCdW5ueRKMARIseUdLOFQrZnl2bzBvbHNselJnb2FFUlZXeWprdEltZ3lDb21CNElC"
        "NkZnRT0aXAosVWVJU3dhNHdkMDd3bEZyMGRGWUpPeis5TU8wNnlLeS8xZGJacjliUjNRND0SLFFwZjE2YnVQSGNOaG"
        "t1ZWdWY1N0TUxBZWtnZFBZUlNuUlVtNjFOdFdVd1k9";
    std::string decryptedResultPartStorage =
        "CjcKB2RlZmF1bHQSLEFqeXZKOTUzdmlMQ3pyYXVKaFlZZHh5TlhBZkJGa29ybnMzTGwrUmV3RHc9EjgKBktpdHRlbh"
        "IuEixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRI2CgREb2dlEi4SLEFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjcKBUJ1bm55Ei4SLEFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjgKBktpdHRlbhIuEixzbnV6dWRmcVhkZFNUU1hWcUNQeDBiTzlNYjM4"
        "SXRhWUsxSTQ0WmhOdzFnPRI2CgREb2dlEi4SLHN2a2psMkxCK2kxY215MnBiUUlkL0dRMEhpbnVoUE9COWJwSytVWU"
        "hoR3M9EjcKBUJ1bm55Ei4SLHlHSzhUK2Z5dm8wb2xzbHpSZ29hRVJWV3lqa3RJbWd5Q29tQjRJQjZGZ0U9";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_AGGREGATE_DECRYPTED_PART_SUM, systemParameters,
        decryptedRequest, decryptedResultPartStorage);
    // anonymousVotingAggregateDecryptedPartSum(string systemParameters, string decryptRequest,
    // string decryptedResultPartStorage)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string counterId;
    std::string decryptedResultPartStoragePart;
    std::string decryptedResultPartStorageSum;

    abi.abiOut(&out, counterId, decryptedResultPartStoragePart, decryptedResultPartStorageSum);
    BOOST_TEST(counterId == "10086");
    BOOST_TEST(
        decryptedResultPartStoragePart ==
        "CpMBCgUxMDA4NhIsQWp5dko5NTN2aUxDenJhdUpoWVlkeHlOWEFmQkZrb3JuczNMbCtSZXdEdz0aXAosMXRPUFpsUW"
        "cwOXhrWVh1TVlZK2VuMXhsS1VJUTRNNHRDUUpkVVJpYXBRRT0SLEFadDYzMk1rQmNwOFVzdmRGWHRpb0NZMzdmNHEw"
        "VG5VKzE5OW9nazJ2QWc9EpcBCgZLaXR0ZW4SjAESLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUE9GlwKLDVUdytTdzhjTnVoNUVKektLTzNQMGJtSUZ2b21vaFh1UWhHcExnVzJYQWM9EixnV05OQXhicXpn"
        "V2RGYlVpUWRlNDZweVVnTmUwdzZYanZ3Ym0wb21uUGdvPRKVAQoERG9nZRKMARIsQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosbWNISm44a0Q4NkNvcXlMR1RSUkloR0YydjRndjNFWUg0bG1QQWsr"
        "Q1Jndz0SLENKR3RBR3pmVkZqZUtzbngrS2RGaEw3M0tJWHZpc2c1SkhJTkF1VXVFUVU9EpYBCgVCdW5ueRKMARIsQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0aXAosUzErZERiZldLcTRnbXhYK1ZDMTBS"
        "RDYzSS9qZUJFMFBoazhVS1poRVpBZz0SLHNRTkRSbUVSZWpTbkxvb2FWNFF1N3BKQ3IxNG80ck16aDkvUWs0UE9Bd2"
        "s9EpcBCgZLaXR0ZW4SjAESLGNtVkFUbnBQeWRLOHN2ZG5PSUtxVTU1NWd3WlRpR1p6QTRvKzJKcGpEM2M9GlwKLFRv"
        "ZHBFMGg0WkJ5a0UzSldzZzJ2Z3JXYXBXclpNam1EQWVLZ3gzaEpkQXM9EixacnBCQklZR2dqdW9LdGZGQWVUdzhjen"
        "RDRzYzSW5RVGdjSm9ERmQvTWcwPRKVAQoERG9nZRKMARIsU25wMjB4OUw1d2t2RHpzblBHcW1kVDhuQjhBMUM3WENX"
        "aGZPeFd0NE9rQT0aXAoselpjcHhVcS9pSVB1K0w2bFlRcDZXc3JybVhUcGtxWHpCcEIxdVdPYnJRTT0SLElmejQxan"
        "p0QVo2ZFMzblBUNERzb0NIMVJpQWZ4WjNmajA2YUJjQUU3Z2M9EpYBCgVCdW5ueRKMARIsbU5kTEVNcGw3cTlzcXJF"
        "aVNvdDdMYkdwS2pCUEljdE5SaWRiU01pT2pTST0aXAosaG10bzU3enhpOWNFNStLSm83a2JWOHV0SUFuUDNXckVDdW"
        "xTeHNBVzRRcz0SLEJEb1lDT0R4eHFzNTlaRWhhUVNtL1V5M3dzQ0YwU3NZS2JhMEloNW1vd2c9EpcBCgZLaXR0ZW4S"
        "jAESLC9HQk5QMThTVXNXMGtWVzhMUGxQZ2pZMjc0c0l0VXI0VkF5aE51M1dnSFE9GlwKLCtYV3pxSjlFblVic251MU"
        "NqV0N2N3ZCQ282WlRTNVJQUFdpalc4UWltZ1k9EixQTXF1enljOEtnazRaVEJtRVJXRDFCSzZ5Nys2eUZUa2k2eno1"
        "L1RJUHdrPRKVAQoERG9nZRKMARIsMXUwTmR4Uk1PN2huRzdTMTBWeTJEWGU1VEgvRko5cTZlTS9CQnA0Y05uWT0aXA"
        "osZXdhZUdhMHc4bnlsSmFDS3JTaGYxQ1kzRWJWZ3cyMlN0OXZ6Z2pUd3Z3az0SLHNhcmJnNFhiRVNzc3kzTFI5SElQ"
        "bUlPdjIybitlUUtlSXhVV0grM3ZEUTg9EpYBCgVCdW5ueRKMARIsYkdQam8zUlNaTXZBTmkwUlo0Zi9JVWNQaFkwNE"
        "5GbGVhMWpYRjBOakFSQT0aXAosTXYrWk95OEk3RGtMZjZzVTh4cDIxa2hPbnpmbCtqc0QrTmJSbTAyMDBRWT0SLDZN"
        "QXBsUGRScTdvRkxjQlhLZ0FKQUduYWxkTWlmNXpJSjNmanVQV2xzd009EpcBCgZLaXR0ZW4SjAESLHNudXp1ZGZxWG"
        "RkU1RTWFZxQ1B4MGJPOU1iMzhJdGFZSzFJNDRaaE53MWc9GlwKLFV4QWJubEVVVm1vYmZVME5RZXpFQ2FYcUwxM3lj"
        "cE9ZeTRtTzRMemIrZ1E9EixkN3hmRnArS3VJUUE2TllTeGdKTUlQdHc2MHFuVUo1UkI1ekdkZlBpV1FjPRKVAQoERG"
        "9nZRKMARIsc3ZramwyTEIraTFjbXkycGJRSWQvR1EwSGludWhQT0I5YnBLK1VZSGhHcz0aXAosS1ord091ZmlJM1pO"
        "OVFzMEkwaG5EaXJVSDNmRE5UaGNvOGdkdys5WnB3Zz0SLEV6Q1RaME1NeElLSjFOS1h6UC9nM1BKQjk4WG80YXlkbW"
        "xaK0NibDZkQUE9EpYBCgVCdW5ueRKMARIseUdLOFQrZnl2bzBvbHNselJnb2FFUlZXeWprdEltZ3lDb21CNElCNkZn"
        "RT0aXAosVWVJU3dhNHdkMDd3bEZyMGRGWUpPeis5TU8wNnlLeS8xZGJacjliUjNRND0SLFFwZjE2YnVQSGNOaGt1ZW"
        "dWY1N0TUxBZWtnZFBZUlNuUlVtNjFOdFdVd1k9");
    BOOST_TEST(
        decryptedResultPartStorageSum ==
        "CjcKB2RlZmF1bHQSLGNvejZCbFpPWnNVSXZhTStDemtOZ3Z2V3VyTTIydTVEVVFIbVo0emF1MzQ9EjgKBktpdHRlbh"
        "IuEixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRI2CgREb2dlEi4SLEFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjcKBUJ1bm55Ei4SLEFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjgKBktpdHRlbhIuEixzbnV6dWRmcVhkZFNUU1hWcUNQeDBiTzlNYjM4"
        "SXRhWUsxSTQ0WmhOdzFnPRI2CgREb2dlEi4SLHN2a2psMkxCK2kxY215MnBiUUlkL0dRMEhpbnVoUE9COWJwSytVWU"
        "hoR3M9EjcKBUJ1bm55Ei4SLHlHSzhUK2Z5dm8wb2xzbHpSZ29hRVJWV3lqa3RJbWd5Q29tQjRJQjZGZ0U9EjgKBktp"
        "dHRlbhIuEixXUE01ZE9BWG5GYk9zbXJPL0Y1L3g4T0I5Q3F4WTY5RTNLSmRJM2RWYkU0PRI2CgREb2dlEi4SLFZMQk"
        "5qc1pNVktOWi9VR1FObmRMQVM1NVJ4aGE3YjJGSGNHTEhpNzhOR009EjcKBUJ1bm55Ei4SLDhyR3F3US96SlMzSzg4"
        "dWRIM3FIOGlRWjNkWWxaendYVHM0SW9nVitad0E9");
}

BOOST_AUTO_TEST_CASE(anonymousVotingAggregateHPoint)
{
    dev::eth::ContractABI abi;
    std::string hPointShare = "okC7xuEG7yVFHxY2pI+gxPdez9StkhxZrCs5diySZXI=";
    std::string hPointSum = "0PoMptw9RCM5U9D59bC76WVEOfzjYpxdHLAthYOATy8=";
    bytes param = abi.abiIn(API_ANONYMOUS_VOTING_AGGREGATE_HPOINT, hPointShare, hPointSum);
    // anonymousVotingAggregateHPoint(string hPointShare, string hPointSum)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));

    std::string newHPointSum;
    abi.abiOut(&out, newHPointSum);
    BOOST_TEST(newHPointSum == "vLQ720mMmUTJ4J+0tQKinhFrxO9MQEG58Zyxtmw0J1M=");
}

BOOST_AUTO_TEST_CASE(anonymousVotingVerifyVoteResult)
{
    dev::eth::ContractABI abi;
    std::string systemParameters =
        "Cix2TFE3MjBtTW1VVEo0SiswdFFLaW5oRnJ4TzlNUUVHNThaeXh0bXcwSjFNPRIGS2l0dGVuEgREb2dlEgVCdW5ueQ"
        "==";
    std::string voteStorageSum =
        "ElwKLG1Mc2UzSk8wTGtNR0UxeENvZFE2VXNMTlJNMVNxcDV0N0JSdXZsYkZDUU09Eix3a1FzbkQxclIwc25uc3dpV0"
        "hLaVptZk9oSy8rVzBpdU1saTVaNnZvMG1JPSJmCgZLaXR0ZW4SXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImQKBE"
        "RvZ2USXAosQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQT0SLEFBQUFBQUFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9ImUKBUJ1bm55ElwKLEFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUE9EixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPSJm"
        "CgZLaXR0ZW4SXAosQW55dHQwcWRQVUV6N2c3bmxrdnMzRU1uSGlreDh6VnB2VzVnMHFoSkEyND0SLFBDQlFWOW1RQX"
        "daZ2tHWkxYaDVsem1CWHd0RndtMWRJSElvNG83TzVyQm89ImQKBERvZ2USXAosWWhnWkF4NWxiMytwVjRsekV1MW1F"
        "NUpFYU01UlRZb3U0WEtJN3VRTEpBYz0SLEdLWDdRbE1Eai8wcUs2NE1OOGZNaVk2a2JjRkFvaFV1Mm5IbHhqWFFwd1"
        "k9ImUKBUJ1bm55ElwKLGNLczNqUzgxYllkTGs5d1ZETW5zSC8zdzY1N2NJMzBhR1k0aVRBL29Ld0E9EixPQ2RBWUl3"
        "ckQ0TGtPWXZMMmZ5cUQyOVdPbzRPWXp0Y1F4NHVqeFJFSWx3PSJmCgZLaXR0ZW4SXAosSER6SmlyNUU3V0NPR2o0ZG"
        "5RalhpMDQvM0Y5TUJoWW05ZEVma2w0NlJWYz0SLG9PZ2pRdkpEdjRpZVYwT0xJMGF5UEtnVWhUemRsaFZseTU3M28z"
        "SnRMSE09ImQKBERvZ2USXAosNkdNVHE5dlJmaWRaSWtTZ25wcDhGS05hcUxDYyt2NDc1MnBVREhTMG5qMD0SLEtCbl"
        "dMT3c1cjVoZ0hZM3ZDNDNEdHNJa0J2UVp4UHhMZDUrQ3JjMmhaQWc9ImUKBUJ1bm55ElwKLFZCb0k0ZEZVWlBwT0Nq"
        "NTFYWnB6cXZqUkUvRzU1dTdVSGI5NVRMYU5hUWs9Eixyc1VXU1BCLzFBdnQ0R3A5MDhLMzR5YWhFMlhxd3B1NmhRMl"
        "dHdW1wTndFPSJmCgZLaXR0ZW4SXAosK2xWWHpQQzFLVlo5bTBSVkY0R0lEVEtLY1c0MUViSXFXWWg0Q294ay9Sbz0S"
        "LDJuS3ppcmlvZWhTNW1LSFZGTTlWeE1pcTdGeUg1M1lQdnhLaHltcDIvalk9ImQKBERvZ2USXAosRVBydlBsL1RMS0"
        "p0b3QvcVBWTzBJcHVnUDA1aThVdENzLzhBMUpPSWZHOD0SLFZLTG9xVTlkbEUxcEMxZ3doZktKRFBiRjNQd3VKdzhu"
        "TGtkTGY0QVdMREk9ImUKBUJ1bm55ElwKLDNDR2toUERGVzF4Tm1LRER3bVlFZ1cveUE2QjdVYndoV3FnVmtmQ3NXeE"
        "E9Eiw1SWJ2VkF5ZisyVUoweFJNRE5ZcUEybm4zYW5IOU5xV05rZk5zZWM3VmlnPQ==";
    std::string decryptedResultPartStorageSum =
        "CjcKB2RlZmF1bHQSLFVOTkNTWVU5NXM2YW9oT3Npbms3SHdvSjRWcXZlZGNMYjBtMXlQN0orVnc9EjgKBktpdHRlbh"
        "IuEixBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBPRI2CgREb2dlEi4SLEFBQUFBQUFB"
        "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjcKBUJ1bm55Ei4SLEFBQUFBQUFBQUFBQUFBQUFBQU"
        "FBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE9EjgKBktpdHRlbhIuEixScHpFN3hXZEJ6bDJhZ1dGTytBK1VKYUI3UUpo"
        "elc0TWRCdGhKekg4TVRFPRI2CgREb2dlEi4SLHpJOFRIT1VRTlhaL290VzVZQVRWVU9KNmFsV2FzakRLRXBnMGdZaW"
        "duenc9EjcKBUJ1bm55Ei4SLElsK2pwZkhiUVE4MzNmRUNKa1N2NXd4SXpJd1h3ZzM4NURzM1hpZU1teUU9EjgKBktp"
        "dHRlbhIuEixTT3JORnVGS2JoRnNJbTMrRFMxaEc3ZHFaaVBRWGxud05Hb3crREN6ZXpjPRI2CgREb2dlEi4SLGh0Nm"
        "VBbm9PdW9vd2FrUkRMc28zb2kzbk5zNlpzZU9tNHFMUkRrS1BESHc9EjcKBUJ1bm55Ei4SLHhFektQL216djUza2Vq"
        "NTgzcFhwcVBLMHhoa2RwZlFzR2k0Sm12NitNRHM9";
    std::string voteResultRequest =
        "CkEKHgoaV2VkcHJfdm90aW5nX3RvdGFsX2JhbGxvdHMQPAoKCgZLaXR0ZW4QBgoICgREb2dlEAkKCQoFQnVubnkQDA"
        "==";
    bytes param = abi.abiIn(API_VERIFY_VOTE_RESULT, systemParameters, voteStorageSum,
        decryptedResultPartStorageSum, voteResultRequest);
    // anonymousVotingVerifyVoteResult(string systemParameters, string voteStorageSum, string
    // decryptedResultPartStorageSum, string voteResultRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));
    u256 result;
    abi.abiOut(&out, result);
    BOOST_TEST(result == WEDPR_SUCCESS);

    std::string errorSystemParameters = "123";
    std::string errorVoteStorageSum = "123";
    std::string errorDecryptedResultPartStorageSum = "123";
    std::string errorVoteResultRequest = "123";
    param = abi.abiIn(API_VERIFY_VOTE_RESULT, errorSystemParameters, errorVoteStorageSum,
        errorDecryptedResultPartStorageSum, errorVoteResultRequest);
    BOOST_CHECK_THROW(wedprPrecompiled->call(context, bytesConstRef(&param)), boost::exception);
}

BOOST_AUTO_TEST_CASE(anonymousVotingGetVoteResultFromRequest)
{
    dev::eth::ContractABI abi;
    std::string voteResultRequest =
        "CkEKHgoaV2VkcHJfdm90aW5nX3RvdGFsX2JhbGxvdHMQPAoKCgZLaXR0ZW4QBgoICgREb2dlEAkKCQoFQnVubnkQDA"
        "==";
    bytes param = abi.abiIn(API_GET_VOTE_RESULT_FROM_REQUEST, voteResultRequest);
    // anonymousVotingGetVoteResultFromRequest(string voteResultRequest)
    bytes out = wedprPrecompiled->call(context, bytesConstRef(&param));
    std::string voteResultStorage;
    abi.abiOut(&out, voteResultStorage);
    BOOST_TEST(
        voteResultStorage ==
        "Ch4KGldlZHByX3ZvdGluZ190b3RhbF9iYWxsb3RzEDwKCgoGS2l0dGVuEAYKCAoERG9nZRAJCgkKBUJ1bm55EAw=");
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
