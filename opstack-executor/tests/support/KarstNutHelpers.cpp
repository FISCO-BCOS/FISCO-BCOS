#include "KarstNutHelpers.h"

#include <string>

namespace opstack_test
{
using bcos::evm::opstack::OpForkSchedule;

std::shared_ptr<OpForkSchedule> karstOnlySchedule(uint64_t karstTs)
{
    return std::make_shared<OpForkSchedule>(
        OpForkSchedule::parse("0:jovian," + std::to_string(karstTs) + ":karst"));
}

std::shared_ptr<OpForkSchedule> isthmusThenJovian(uint64_t jovianTs)
{
    return std::make_shared<OpForkSchedule>(
        OpForkSchedule::parse("0:isthmus," + std::to_string(jovianTs) + ":jovian"));
}

std::shared_ptr<OpForkSchedule> legacySchedule(bool jovianActive)
{
    return std::make_shared<OpForkSchedule>(OpForkSchedule::legacy(jovianActive));
}
}  // namespace opstack_test
