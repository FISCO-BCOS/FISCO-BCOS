#include "TransactionExecutorImpl.h"

evmc_address bcos::transaction_executor::unhexAddress(std::string_view view)
{
    if (view.empty())
    {
        return {};
    }
    if (view.starts_with("0x"))
    {
        view = view.substr(2);
    }

    evmc_address address;
    if (view.empty())
    {
        std::uninitialized_fill(address.bytes, address.bytes + sizeof(address.bytes), 0);
    }
    else
    {
        boost::algorithm::unhex(view, address.bytes);
    }
    return address;
}
