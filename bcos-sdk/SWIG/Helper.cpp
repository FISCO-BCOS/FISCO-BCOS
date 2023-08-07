#include "Helper.h"

bcos::bytes bcos::sdk::swig::toBytes(char* STRING, size_t LENGTH)
{
    return {(const bcos::byte*)STRING, (const bcos::byte*)STRING + LENGTH};
}
