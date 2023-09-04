#include "Precompiled.h"
#include "StorageWrapper.h"
#include "bcos-utilities/Overloaded.h"

std::string bcos::transaction_executor::Precompiled::addressBytesStr2String(
    std::string_view receiveAddressBytes)
{
    std::string strAddress;
    strAddress.reserve(receiveAddressBytes.size() * 2);
    boost::algorithm::hex_lower(
        receiveAddressBytes.begin(), receiveAddressBytes.end(), std::back_inserter(strAddress));

    return strAddress;
}

std::string bcos::transaction_executor::Precompiled::evmAddress2String(const evmc_address& address)
{
    auto receiveAddressBytes = fromEvmC(address);
    return addressBytesStr2String(receiveAddressBytes);
}
