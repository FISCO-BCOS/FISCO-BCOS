#include "Precompiled.h"
#include "bcos-utilities/Overloaded.h"
#include <gsl/pointers>

struct UnsupportedPrecompiledException : public bcos::Exception
{
};

bcos::transaction_executor::EVMCResult bcos::transaction_executor::Precompiled::call(
    evmc_message const& message) const
{
    return std::visit(
        bcos::overloaded{
            [&](executor::PrecompiledContract const& precompiled) {
                auto [success, output] =
                    precompiled.execute({message.input_data, message.input_size});
                auto gas = precompiled.cost({message.input_data, message.input_size});

                auto buffer = std::unique_ptr<uint8_t>(new uint8_t[output.size()]);
                std::copy(output.begin(), output.end(), buffer.get());
                EVMCResult result{evmc_result{
                    .status_code =
                        (evmc_status_code)(int32_t)(success ? protocol::TransactionStatus::None :
                                                              protocol::TransactionStatus::
                                                                  RevertInstruction),
                    .gas_left = message.gas - gas.template convert_to<int64_t>(),
                    .gas_refund = 0,
                    .output_data = buffer.release(),
                    .output_size = output.size(),
                    .release =
                        [](const struct evmc_result* result) { delete[] result->output_data; },
                    .create_address = {},
                    .padding = {},
                }};

                return result;
            },
            [](std::shared_ptr<precompiled::Precompiled> const& precompiled) {
                // precompiled->call(std::shared_ptr<executor::TransactionExecutive> _executive,
                // PrecompiledExecResult::Ptr _callParameters)

                BOOST_THROW_EXCEPTION(UnsupportedPrecompiledException());
                return EVMCResult{evmc_result{}};
            }},
        m_precompiled);
}